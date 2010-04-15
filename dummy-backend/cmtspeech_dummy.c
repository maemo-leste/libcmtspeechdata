/*
 * This file is part of libcmtspeechdata.
 *
 * Copyright (C) 2008,2009,2010 Nokia Corporation.
 *
 * Contact: Kai Vehmanen <kai.vehmanen@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

/** @file cmtspeech_dummy.c
 *
 * Dummy user-space implementation of the CMT Speech Data
 * Backend interface.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

#include "cmtspeech.h"
#include "cmtspeech_msgs.h"
#include "cmtspeech_backend_common.h"

/* Build-time configuration */
/* -------------------------------------------------------------------- */

/* Select sampling rate to use */
/*#define USE_SAMPLE_RATE_16K 1*/
#define USE_SAMPLE_RATE_8K 1

/* Select active debugging traces */
#define BUILD_WITH_DEBUG 1
#define BUILD_WITH_DEBUG_TOKENS 1
#include "sal_debug.h"
#define DEBUG_PREFIX "dummy_backend: "

#include "cmtspeech_dummy.h"
#include "dummy_common.h"

#define SAL_BUFFER_SLOTS  5
#define SAL_MSG_BUFFER 256
#define SAL_TIMEOUT_LONG_MS 300000 /* 5min */

/* Definitions derived from build-time configuration */
/* -------------------------------------------------------------------- */

#if CMTSPEECH_BIG_ENDIAN_CMDS
#define BYTE0 0  /* MSB byte */
#define BYTE1 1  
#define BYTE2 2
#define BYTE3 3  /* LSB byte */
#elif CMTSPEECH_LITTLE_ENDIAN_CMDS
#define BYTE0 3  /* MSB byte */
#define BYTE1 2  
#define BYTE2 1
#define BYTE3 0  /* LSB byte */
#else
#error "Endianess must be set."
#endif

#ifdef USE_SAMPLE_RATE_16K
#define SAL_SAMPLE_RATE      16000
#define SAL_FRAME_SIZE_OCTETS  640       /* 20ms at mono/16kHz */
#elif USE_SAMPLE_RATE_8K
#define SAL_SAMPLE_RATE       8000
#define SAL_FRAME_SIZE_OCTETS  320       /* 20ms at mono/8kHz */
#else
#error "Sample rate not selected."
#endif

/* Data types */
/* -------------------------------------------------------------------- */

enum {
  STATE_IDLE = 0,                 /**< call server not active */
  STATE_INITIALIZED,              /**< call has been activated on CMT side */
  STATE_MEDIA_FLOWING,            /**< at least UL frames are being sent */
  STATE_TERMINATING,              /**< call termination has started */
};

enum {
  THREAD_NOT_STARTED = 0,
  THREAD_STARTED,
  THREAD_RUNNING,
  THREAD_EXIT_REQ,
  THREAD_TERMINATED
};

struct dummy_state_s {
  volatile int state;
  volatile bool dl_active;
  volatile bool call_server_status;
  pthread_mutex_t writelock;
};

struct cmtspeech_dummy_s {
  cmtspeech_bc_state_t bcstate; /**< protocol state */
  struct dummy_state_s s;
  int ul_buf_idx;
  int dl_buf_idx;
  uint16_t dl_frame_counter;
  int control_pipes[2];     /**< pipe for delivering events to client */
  int thread_pipes[2];      /**< pipe for delivering events to worker thread */
  pthread_t worker;
  volatile int thread_running;
  uint8_t pubbuf[SAL_MSG_BUFFER];  /**< used in client context only */
  int pubbuf_write;
  int pubbuf_read;
  dummy_buffer_t ul_buffers[SAL_BUFFER_SLOTS];
  dummy_buffer_t dl_buffers[SAL_BUFFER_SLOTS];
  dummy_tone_t *tone;
  dummy_poll_timer_t poll_timer;
};

#define DUMMY_DL_DATA_AVAIL         0x01
#define DUMMY_UL_DATA_SENT          0x02
#define DUMMY_CMT_ALIVE             0x03
#define DUMMY_EXIT_REQUEST          0x04

/* Function definitions */
/* -------------------------------------------------------------------- */

/**
 * Encodes a dummy backend message 'type' to buffer pointed
 * by 'buf'. Returns size of encoded data (in octets).
 *
 * @param type is one of DUMMY_DL_DATA_AVAIL, DUMMY_UL_DATA_SENT, DUMMY_CMT_ALIVE
 */
static int cmtspeech_msg_encode_dummy_internal_message(uint8_t *buf, int len, uint8_t type)
{
  if (len < 4)
    return -1;

  buf[BYTE0] = (type << 4) | CMTSPEECH_DOMAIN_INTERNAL;
  buf[BYTE1] = 0x0;
  buf[BYTE2] = 0x0;
  buf[BYTE3] = 0x0;
  
  return 4;
}

static int priv_change_state(cmtspeech_dummy_t *self, int new_state)
{
  int res = -1;

  if (self->s.state != new_state) {
    res = pthread_mutex_lock(&self->s.writelock);
    self->s.state = new_state;
    pthread_mutex_unlock(&self->s.writelock);
    ONTRACE(printf(DEBUG_PREFIX "changed state to %d.\n", new_state));
  }

  return res;
}

static int init_buffer_slot(dummy_buffer_t *slot, int index)
{
  slot->buf.type = CMTSPEECH_BUFFER_TYPE_PCM_S16_LE;
  slot->buf.count = 4 + SAL_FRAME_SIZE_OCTETS; /* 32bit header plus 20ms of 16bit audio */;
  slot->buf.pcount = SAL_FRAME_SIZE_OCTETS;
  slot->buf.size = slot->buf.count;
  slot->buf.frame_flags = 0;
  slot->buf.spc_flags = 0;
  slot->buf.data = malloc(slot->buf.size); 
  slot->buf.payload = slot->buf.data + 4;
  slot->buf.index = index;
  slot->locked = 0;

  if (!slot->buf.data)
    return -1;

  return 0;
}

static int free_buffer_slot(dummy_buffer_t *slot)
{
  free(slot->buf.data);
  memset(slot, 0, sizeof(dummy_buffer_t));
  return 0;
}

/**
 * Emits a SPEECH_CONFIG_REQ towards APE:
 */
static void priv_emit_speech_config_req(cmtspeech_dummy_t *priv, uint8_t speech_data, uint8_t user_connect)
{
  cmtspeech_cmd_t msg;
  int res =
    cmtspeech_msg_encode_speech_config_req(&msg, 
					   speech_data,
					   user_connect,
					   CMTSPEECH_CODEC_INFO_GSM_HR,
					   CMTSPEECH_CELLULAR_INFO_GSM,
#if USE_SAMPLE_RATE_16K
					   CMTSPEECH_SAMPLE_RATE_16KHZ,
#elif USE_SAMPLE_RATE_8K
					   CMTSPEECH_SAMPLE_RATE_8KHZ,
#else
#error "Sampling rate not set."
#endif
					   CMTSPEECH_DATA_FORMAT_S16LINPCM);
  assert(res > 0); 
  res = write(priv->control_pipes[1], msg.d.buf, res);
}

/**
 * Processes messages sent to the internal message queue (APE->CMT
 * messages).
 *
 * The client will trigger processing when the handle (cmtspeech_descriptor())
 * becomes readable.
 * 
 * @see priv_read_control_message()
 */ 
static int handle_inbound_control_message(cmtspeech_dummy_t *priv, const cmtspeech_cmd_t msg)
{
  int res = 0;
  uint8_t state = 0;
  uint8_t version = 0;
  int type =
    cmtspeech_msg_get_type(msg);
  int channel =
    cmtspeech_msg_get_domain(msg);
  cmtspeech_cmd_t tmpmsg;

  ONTRACE(printf(DEBUG_PREFIX "read APE->CMT message %s on channel %d.\n", cmtspeech_msg_type_to_string(msg), channel));

  /* note: these are the APE->CMT messages */
  
  if (channel == CMTSPEECH_DOMAIN_CONTROL) {

    switch(type)
      {
      case CMTSPEECH_SSI_CONFIG_REQ:
	/* step: parse received message */
	cmtspeech_msg_decode_ssi_config_req(msg, NULL, &version, &state);

	res = cmtspeech_msg_encode_ssi_config_resp(&tmpmsg, CMTSPEECH_SAMPLE_LAYOUT_INORDER_LE, 0);
	assert(res > 0);
	write(priv->control_pipes[1], tmpmsg.d.buf, res);
	if (!state)
	  priv_change_state(priv, STATE_IDLE);
	break;

      case CMTSPEECH_NEW_TIMING_CONFIG_REQ:
	if (priv->s.state == STATE_MEDIA_FLOWING) {
	  res = cmtspeech_msg_encode_uplink_config_ntf(&tmpmsg);
	  assert(res > 0);
	  write(priv->control_pipes[1], tmpmsg.d.buf, res);
	}
	res = cmtspeech_msg_encode_timing_config_ntf(&tmpmsg, 5, 0);
	assert(res > 0);
	write(priv->control_pipes[1], tmpmsg.d.buf, res);
	break;

      case CMTSPEECH_SPEECH_CONFIG_RESP:
	break;

      case CMTSPEECH_SSI_CONFIG_RESP:
      case CMTSPEECH_SPEECH_CONFIG_REQ:
      case CMTSPEECH_TIMING_CONFIG_NTF:
	ONINFO(fprintf(stderr, DEBUG_PREFIX "ERROR: CMT->APE message received by CMT, type %d (%02X:%02X:%02X:%02x).\n", 
		       type, msg.d.buf[0], msg.d.buf[1], msg.d.buf[2], msg.d.buf[3]));
	res = -1;
	break;

      default:
	ONINFO(fprintf(stderr, DEBUG_PREFIX "ERROR: unknown control message of type %d (%02X:%02X:%02X:%02x).\n", 
		       type, msg.d.buf[0], msg.d.buf[1], msg.d.buf[2], msg.d.buf[3]));
	res = -1;
      }

  }
  else if (channel == CMTSPEECH_DOMAIN_INTERNAL) {
    int type =
      cmtspeech_msg_get_type(msg);
    if (type == DUMMY_EXIT_REQUEST) {
      ONINFO(printf(DEBUG_PREFIX "exit request received, exiting worked thread (running=%d)\n",
		    priv->thread_running));
      res = 0;
    }
  }
  else
    res = -1;

  return res;
}


/**
 * Reads and processes one pending control message sent
 * to the internal queue (APE->CMT messages).
 *
 * @see handle_inbound_control_message()
 */  
static int priv_read_control_message(cmtspeech_dummy_t *priv, struct pollfd *fds)
{
  cmtspeech_cmd_t msg;
  int i;
  ONDEBUG_TOKENS(fprintf(stderr, "E"));
  if (fds->revents & POLLIN) 
    ONDEBUG(printf(DEBUG_PREFIX "thread wakeup - inbound control message\n"));

  /* note: SSI transfers are always at least 32bits (CMTSPEECH_CTRL_LEN) */  
  i = read(fds->fd, msg.d.buf, CMTSPEECH_CTRL_LEN);
  if (i == CMTSPEECH_CTRL_LEN)
    handle_inbound_control_message(priv, msg);

  return i;
}

/**
 * The main loop for the dummy backend.
 */ 
static void *worker_thread(void *context)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
  struct pollfd fds;
  cmtspeech_cmd_t msg;

  if (priv->thread_running == THREAD_STARTED)
    priv->thread_running = THREAD_RUNNING;

  ONDEBUG(printf(DEBUG_PREFIX "ssi-audio-dummy thread started\n"));

  while(1) {
    int res;
    int timeout;
    int cur_state = priv->s.state;

    if (priv->thread_running >= THREAD_EXIT_REQ)
      break;

    switch (cur_state) 
      {
      case STATE_IDLE:
      case STATE_INITIALIZED:
      case STATE_TERMINATING:
	/* step: wait for control messages (sleep time max 60000ms) */
	fds.fd = priv->thread_pipes[0];
	fds.events = POLLIN;
	timeout = SAL_TIMEOUT_LONG_MS;
	res = poll(&fds, 1, timeout);
	/* case: inbound control message */;
	if (res > 0)
	  priv_read_control_message(priv, &fds);
	else if (res < 0)
	  ONDEBUG(fprintf(stderr, DEBUG_PREFIX "Poll error %d (%d).\n", res, errno));
	else 
	  ONTRACE(printf(DEBUG_PREFIX "dummy backend thread idle (state=%d, poll=%d)\n", cur_state, res));
	break;

      case STATE_MEDIA_FLOWING:
	/* step: wake up at least every 20msec to generate DL frames  */
	fds.fd = priv->thread_pipes[0];
	fds.events = POLLIN;
	/* step: adjust poll timeout based on actual time elapsed */

	if (priv->s.dl_active == true) {
	  timeout = dummy_poll_timer_pre_poll(&priv->poll_timer);
	}
	else 
	  timeout = 100;

	ONDEBUG_TOKENS(fprintf(stderr, "i"));
	res = poll(&fds, 1, timeout);
	ONDEBUG_TOKENS(fprintf(stderr, "o"));

	/* case: inbound control message */;
	if (res > 0) {
	  ONDEBUG_TOKENS(fprintf(stderr, "C"));
	  priv_read_control_message(priv, &fds);
	}
	/* case: error */
	else if (res < 0) {

	  ONDEBUG_TOKENS(fprintf(stderr, "E"));

	  if (errno == EINTR) 
	    continue;
	  else {
	    fprintf(stderr, "ERROR: "DEBUG_PREFIX "error in cmtspeech_dummmy worker thread\n");
	    priv->thread_running = THREAD_EXIT_REQ;
	  }
	}

	/* case: generate DL frame if enough time has passed */
	if (priv->s.dl_active != true) {
	  ONDEBUG_TOKENS(fprintf(stderr, "K1"));
	}
	else if (dummy_poll_timer_is_elapsed(&priv->poll_timer, 5000000) != true) {
	  ONDEBUG_TOKENS(fprintf(stderr, "K2"));
	}
	else {
	  if (priv->dl_buffers[priv->dl_buf_idx].locked)
	    printf("WARNING: " DEBUG_PREFIX "buffer overrun in DL direction.\n");
	  ONDEBUG_TOKENS(fprintf(stderr, "T"));
	  priv->dl_buf_idx = (priv->dl_buf_idx + 1) % SAL_BUFFER_SLOTS;
	  dummy_tone_fill_buffer_slot(priv->tone, &priv->dl_buffers[priv->dl_buf_idx], priv->dl_frame_counter++);
	  cmtspeech_msg_encode_dummy_internal_message(msg.d.buf, CMTSPEECH_CTRL_LEN, DUMMY_DL_DATA_AVAIL);
	  res = write(priv->control_pipes[1], msg.d.buf, CMTSPEECH_CTRL_LEN);
	  assert(res == CMTSPEECH_CTRL_LEN);
	  assert(cmtspeech_msg_get_domain(msg) == 0);
          dummy_poll_timer_elapsed_fixed_period(&priv->poll_timer);
	}

	break;

      default:
	assert(0);
      }
  }
  ONDEBUG(printf(DEBUG_PREFIX "thread stopping\n"));
  priv->thread_running = THREAD_TERMINATED;
  return 0;
}


cmtspeech_t* cmtspeech_open(void)
{
  cmtspeech_dummy_t *priv = malloc(sizeof(cmtspeech_dummy_t));

  if (priv) {
    int i, j;
    for(i = 0; i < SAL_BUFFER_SLOTS; i++) {
      init_buffer_slot(&priv->ul_buffers[i], i);
      init_buffer_slot(&priv->dl_buffers[i], i);
    }
    cmtspeech_bc_open(&priv->bcstate);
    priv->s.state = STATE_IDLE;
    priv->s.call_server_status = false;
    pthread_mutex_init(&priv->s.writelock, NULL);
    priv->ul_buf_idx = 0;
    priv->dl_buf_idx = 0;
    priv->dl_frame_counter = 0;
    priv->pubbuf_write = 0;
    priv->pubbuf_read = 0;
    priv->tone = dummy_tone_initialize("sine");
    dummy_poll_timer_init(&priv->poll_timer, 20);
    i = pipe(priv->control_pipes);
    j = pipe(priv->thread_pipes);
    if (i || j || priv->tone == NULL) {
      ONINFO(fprintf (stderr, DEBUG_PREFIX "ERROR: unable to allocate resources (pipes %d+%d, tone=%p)\n",
		      i, j, priv->tone));
      free(priv);
      priv = NULL;
    }
    else {
      int pthres;

      fcntl(priv->control_pipes[0], F_SETFD, O_NONBLOCK);
      fcntl(priv->thread_pipes[0], F_SETFD, O_NONBLOCK);
      priv->thread_running = THREAD_STARTED;

      pthres = pthread_create(&priv->worker, NULL, worker_thread, priv);
      assert(pthres == 0);
    }
  }

  return (cmtspeech_t*)priv;
}

int cmtspeech_close(cmtspeech_t *context)
{
  int res = 0;

  if (context) {
    cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
    int i, *thres = NULL;
    void *retval = thres;
    cmtspeech_cmd_t msg;

    if (priv->thread_running >= THREAD_STARTED) {
      priv->thread_running = THREAD_EXIT_REQ;
    }

    cmtspeech_msg_encode_dummy_internal_message(msg.d.buf, CMTSPEECH_CTRL_LEN, DUMMY_EXIT_REQUEST);
    res = write(priv->thread_pipes[1], msg.d.buf, CMTSPEECH_CTRL_LEN);
    assert(res == CMTSPEECH_CTRL_LEN);

    if (priv->thread_running >= THREAD_STARTED) {
      pthread_join(priv->worker, &retval);
      assert(priv->thread_running == THREAD_TERMINATED);
    }

    /* close sockets */
    for(i = 0; i < SAL_BUFFER_SLOTS; i++) {
      free_buffer_slot(&priv->ul_buffers[i]);
      free_buffer_slot(&priv->dl_buffers[i]);
    }

    close(priv->control_pipes[0]);
    close(priv->control_pipes[1]);
    close(priv->thread_pipes[0]);
    close(priv->thread_pipes[1]);

    dummy_tone_release(priv->tone);
    free(priv);
  }
  else 
    res = -1;

  return res;
}

int cmtspeech_descriptor(cmtspeech_t *context)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
  return priv->control_pipes[0];
}

int cmtspeech_check_pending(cmtspeech_t *context, int *flags)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
  int res;

  if ((priv->pubbuf_write + CMTSPEECH_DATA_HEADER_LEN) % SAL_MSG_BUFFER
      == priv->pubbuf_read) {
    ONINFO(fprintf(stderr, DEBUG_PREFIX "ERROR: event queue overflow!\n"));
    if (flags)
      *flags = CMTSPEECH_EVENT_CONTROL;
    return 1;
  }

  res = read(priv->control_pipes[0], &priv->pubbuf[priv->pubbuf_write], CMTSPEECH_DATA_HEADER_LEN);

  if (flags)
    *flags = 0;

  if (res > 0) {
    cmtspeech_cmd_t msg;
    memcpy(msg.d.buf, &priv->pubbuf[priv->pubbuf_write], CMTSPEECH_CTRL_LEN);
    int channel =
      cmtspeech_msg_get_domain(msg);
    /* note: only queue control events (ignore data wakeups) */
    if (channel == CMTSPEECH_DOMAIN_CONTROL) {
      priv->pubbuf_write += res;
      priv->pubbuf_write %= SAL_MSG_BUFFER;
      if (flags)
	*flags = CMTSPEECH_EVENT_CONTROL;
    }
    else if (channel == CMTSPEECH_DOMAIN_INTERNAL) {
      int type =
	cmtspeech_msg_get_type(msg);
      if (flags && 
	  type == DUMMY_DL_DATA_AVAIL)
	*flags = CMTSPEECH_EVENT_DL_DATA;
    }

    return 1;
  }
  
  return 0;
}

int cmtspeech_read_event(cmtspeech_t *context, cmtspeech_event_t *event)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
  int res;
  cmtspeech_cmd_t msg;
  uint8_t *pubbuf = &priv->pubbuf[priv->pubbuf_read];

  assert(event);

  memcpy (msg.d.buf, pubbuf, CMTSPEECH_CTRL_LEN);

  /* note: these are the CMT->APE messages */

  res = cmtspeech_bc_handle_command(&priv->bcstate, context, msg, event);

  if (event->msg_type == CMTSPEECH_SSI_CONFIG_RESP) {
    /* step: enable DL */
    if (priv->bcstate.proto_state == CMTSPEECH_STATE_CONNECTED) {
      ONTRACE(printf(DEBUG_PREFIX "Emitting SPEECH_CONFIG_REQ(enable)\n"));
      priv_emit_speech_config_req(priv, 1, 1);
    }
  }
  else if (event->msg_type == CMTSPEECH_SPEECH_CONFIG_REQ) {
    uint8_t reply_result = 0;
    cmtspeech_cmd_t respcmd;
    cmtspeech_msg_encode_speech_config_resp(&respcmd,
                                            reply_result);
    cmtspeech_bc_post_command(&priv->bcstate, context, respcmd);

    priv->s.dl_active = true;

  }

  cmtspeech_bc_complete_event_processing(&priv->bcstate, context, event);

  if (event->state == CMTSPEECH_STATE_CONNECTED &&
      event->prev_state == CMTSPEECH_STATE_DISCONNECTED) {
    priv_change_state(priv, STATE_MEDIA_FLOWING);
  }

  ONDEBUG(printf(DEBUG_PREFIX "POP pubbuf r-old:%d, r-new:%d w:%d\n", 
		 priv->pubbuf_read, 
		 (priv->pubbuf_read + CMTSPEECH_CTRL_LEN) % SAL_MSG_BUFFER,
		 priv->pubbuf_write));

  priv->pubbuf_read += CMTSPEECH_CTRL_LEN;
  priv->pubbuf_read %= SAL_MSG_BUFFER;

  return res;
}

int cmtspeech_state_change_call_status(cmtspeech_t *context, bool status)
{
  cmtspeech_dummy_t *self = (cmtspeech_dummy_t*)context;
  int res =  cmtspeech_bc_state_change_call_status(context, status);

  ONTRACE(printf(DEBUG_PREFIX "ServerStatus - %d\n", status));
  if (!status) {
    self->s.dl_active = false;
  }

  if (self->s.call_server_status != status) {
    self->s.call_server_status = status;
    if (status) {
      priv_change_state(self, STATE_INITIALIZED);
    }
    else {
      priv_change_state(self, STATE_TERMINATING);
      if (self->bcstate.proto_state == CMTSPEECH_STATE_ACTIVE_DL ||
          self->bcstate.proto_state == CMTSPEECH_STATE_ACTIVE_DLUL) {
        ONTRACE(printf(DEBUG_PREFIX "Emitting SPEECH_CONFIG_REQ(disable)\n"));
        priv_emit_speech_config_req(self, 0, 0);
      }
    }
  }
  else
    ONTRACE(printf(DEBUG_PREFIX "No change, ignoring ServerStatus indication\n"));

  return res;
}

int cmtspeech_state_change_call_connect(cmtspeech_t *context, bool state)
{
  cmtspeech_dummy_t *self = (cmtspeech_dummy_t*)context;
  ONTRACE(printf(DEBUG_PREFIX "AudioConnect state:%d\n", state));
  self->s.dl_active = state;

  return cmtspeech_bc_state_change_call_connect(context, state);
}

int cmtspeech_state_change_error(cmtspeech_t *context)
{
  /* XXX: not implemented */
  return -1;
}

int cmtspeech_dl_buffer_acquire(cmtspeech_t *context, cmtspeech_buffer_t **buf)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;

  if (buf == NULL)
    return -EINVAL;

  SOFT_ASSERT(priv->dl_buffers[priv->dl_buf_idx].locked == 0);

  *buf = &priv->dl_buffers[priv->dl_buf_idx].buf;
  priv->dl_buffers[priv->dl_buf_idx].locked = 1;
  
  return 0;
}

int cmtspeech_dl_buffer_release(cmtspeech_t *context, cmtspeech_buffer_t *buf)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
  int res = 0;
  
  if (!priv->dl_buffers[buf->index].locked) {
    ONINFO(fprintf(stderr, DEBUG_PREFIX "ERROR: trying to release an unlocked DL buffer.\n"));
    res = -ENOENT;
  }
  else if (&priv->dl_buffers[buf->index].buf != buf) {
    ONINFO(fprintf(stderr, DEBUG_PREFIX "ERROR: trying to release on invalid UL buffer.\n"));
    res = -EINVAL;
  }
  else {
    priv->dl_buffers[buf->index].locked = 0;
    /* XXX: process UL audio (write to file or loop back */
  }

  return res;
}

cmtspeech_buffer_t *cmtspeech_dl_buffer_find_with_data(cmtspeech_t *context, uint8_t *data)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
  int i;

  for(i = 0; i < SAL_BUFFER_SLOTS; i++) {
    if (priv->dl_buffers[i].buf.data == data)
      return &priv->dl_buffers[i].buf;
  }
  return NULL;
}

cmtspeech_buffer_t *cmtspeech_dl_buffer_find_with_payload(cmtspeech_t *context, uint8_t *payload)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
  int i;

  for(i = 0; i < SAL_BUFFER_SLOTS; i++) {
    if (priv->dl_buffers[i].buf.payload == payload)
      return &priv->dl_buffers[i].buf;
  }
  return NULL;
}

int cmtspeech_test_data_ramp_req(cmtspeech_t *context, uint8_t rampstart, uint8_t ramplen)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
  return cmtspeech_bc_test_data_ramp_req(&priv->bcstate, context, priv->thread_pipes[1], CMTSPEECH_DOMAIN_CONTROL, CMTSPEECH_DOMAIN_DATA, rampstart, ramplen);
}

int cmtspeech_send_timing_request(cmtspeech_t *context)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
  return cmtspeech_bc_send_timing_request(&priv->bcstate, context, priv->thread_pipes[1]);
}

int cmtspeech_send_ssi_config_request(cmtspeech_t *context, bool state)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
  return cmtspeech_bc_send_ssi_config_request(&priv->bcstate, context, priv->thread_pipes[1], state);
}

int cmtspeech_ul_buffer_acquire(cmtspeech_t *context, cmtspeech_buffer_t **buf)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;

  if (buf == NULL)
    return -EINVAL;

  *buf = &priv->ul_buffers[priv->ul_buf_idx].buf;
  priv->ul_buffers[priv->ul_buf_idx].locked = 1;

  ++priv->ul_buf_idx;
  if (priv->ul_buf_idx == SAL_BUFFER_SLOTS)
    priv->ul_buf_idx = 0;

  return 0;
}

int cmtspeech_ul_buffer_release(cmtspeech_t *context, cmtspeech_buffer_t *buf)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
  int res = 0;

  if (!priv->ul_buffers[buf->index].locked) {
    ONINFO(fprintf(stderr, DEBUG_PREFIX "ERROR: trying to release an unlocked UL buffer.\n"));
    res = -EINVAL;
  }
  else if (&priv->ul_buffers[buf->index].buf != buf) {
    ONINFO(fprintf(stderr, DEBUG_PREFIX "ERROR: trying to release on invalid UL buffer.\n"));
    res = -EINVAL;
  }
  else {
    priv->ul_buffers[buf->index].locked = 0;
    /* XXX: store the sent UL buffer for verification */
  }

  return res;
}

const char* cmtspeech_backend_name(cmtspeech_t* context)
{
  return CMTSPEECH_DUMMY_BACKEND_ID;
}

int cmtspeech_backend_message(cmtspeech_t *self, int type, int args, ...)
{
  return -1;
}

cmtspeech_bc_state_t *cmtspeech_bc_state_object(cmtspeech_t *context)
{
  cmtspeech_dummy_t *priv = (cmtspeech_dummy_t*)context;
  return &priv->bcstate;
}
