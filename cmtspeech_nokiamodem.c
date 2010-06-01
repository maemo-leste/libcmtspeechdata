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

/** @file cmtspeech_nokiamodem.c
 *
 * A libcmtspeechdata backend implementation for Nokia
 * hardware (e.g. for Nokia N900). The implementation uses
 * the /dev/cmt_speech Linux kernel interface to communicate
 * with the cellular modem.
 *
 * This interface is available e.g. in Maemo5 and Maemo6
 * kernels.
 */

/**
 * General list of TODO items (in priority order):
 *  - none
 * Longer term:
 *  - low-prio: close the wakeline if no response
 *    is received to RESET
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

#include "cmtspeech.h"
#include "cmtspeech_msgs.h"
#include "cmtspeech_backend_common.h"
#include "sal_ring.h"

#define CS_COMMAND(x)  ((x >> CS_CMD_SHIFT) & 0xf)
#define CMD_PARAM_MASK 0xff

/* note: the driver kernel interface */
#if HAVE_LINUX_CS_PROTOCOL_H
#include <linux/cs-protocol.h>
#else
#include "kernel-headers/linux/cs-protocol.h"
#endif

/* Build-time configuration */
/* -------------------------------------------------------------------- */

#define EVENT_BUFFER_SIZE       64
#define PCM_SAMPLE_SIZE         2   /* mono/16bit */
#define MAX_SLOT_SIZE           (PCM_SAMPLE_SIZE*320+CMTSPEECH_DATA_HEADER_LEN)
#define UL_SLOTS                2
#define DL_SLOTS                3
#define SHARED_MEMORY_AREA_PAGE 4096
#define MAX_UL_ERRORS_PAUSE     5  /* pause UL after this many errors */

  /* reference:
   * - linux/arch/arm/plat-omap/include/mach/omap34xx.h */
#define PM_VDD2_LOCK_TO_OPP3   3
#define PM_VDD2_UNLOCK         0
#define PM_VDD2_LOCK_INTERFACE "/sys/power/vdd2_lock"

/* support use of 'swapped little endian' sample layout
 * for transfering speech data frames */
#define PROTOCOL_SUPPORT_SAMPLE_SWAP 1

#include "sal_debug.h"
#define DEBUG_PREFIX "nokiamodem_backend: "

/* Data types */
/* -------------------------------------------------------------------- */

enum {
  STATE_IDLE,                   /**< call server not active */
  STATE_INITIALIZED,            /**< call has been activated on CMT side */
  STATE_MEDIA_FLOWING,          /**< at least DL frame are being sent */
};

enum {
  BUF_LOCKED   = 1,
  BUF_INVALID  = 1 << 1,
  BUF_XRUN     = 1 << 2,
  BUF_LAST     = 1 << 3,
};

enum {
  WAKELINE_CALL           = 1 << 0,
  WAKELINE_RESET          = 1 << 1,
  WAKELINE_TEST_RAMP_PING = 1 << 2,

};

enum DriverFeatures {
  DRIVER_FEAT_ROLLING_RX_PTR = 1 << 0,
};

struct nokiamodem_buffer_desc_s {
  cmtspeech_buffer_t bd;
  int flags;
};
typedef struct nokiamodem_buffer_desc_s nokiamodem_buffer_desc_t;

struct nokiamodem_driver_state_s {
  int fd;                       /**< driver-io: cmt_speech driver handle */
  int wakeline_users;           /**< driver-io: bitmask of wakeline users */
  int flags;                    /**< driver-io: bitmask of DriverFeatures enum */
  uint8_t *buf;                 /**< driver-io: mmap()'ed driver buffer  */
  size_t buflen;                /**< driver-io: size of 'buf' */
  uint8_t *dlswapbuf;           /**< driver-io: temporary buffer for public DL buffers */
  ring_buffer_t evbuf;          /**< driver-io: queued received events */
  uint32_t rx_offsets[DL_SLOTS];
  uint32_t tx_offsets[UL_SLOTS];
  uint32_t tstamp_rx_ctrl_offset;
};
typedef struct nokiamodem_driver_state_s nokiamodem_driver_state_t;

struct cmtspeech_nokiamodem_s {
  nokiamodem_driver_state_t d;        /**< driver/ext-resource state */
  cmtspeech_bc_state_t bcstate; /**< backend-common protocol state */
  bool speech_config_resp_pend; /**< buf state: response waiting for to be sent */
  unsigned int slot_size;       /**< buf state: data frame slot (header+payload) size in octets */
  uint8_t conf_sample_rate;     /**< buf state: CMTSPEECH_SAMPLE_RATE_* */
  uint8_t conf_data_length;     /**< buf state: CMTSPEECH_DATA_LENGTH_* */
  int ul_errors;                /**< buf state: number of consecutive UL errors */
  int rx_ptr_hw;                /**< buf state: next ptr hw driver will
				   write to, -1 if buffer not yet configured */
  int rx_ptr_appl;              /**< buf state: next ptr to give out to
				   the app, -1 if no driver buffer is
				   yet available */
  int ul_slot_app;              /**< buf state: next slot to give to
				   the app, -1 if buffer not yet
				   configured */
  nokiamodem_buffer_desc_t dlbufdesc[DL_SLOTS]; /**< buf state: DL buffer descs */
  nokiamodem_buffer_desc_t ulbufdesc[UL_SLOTS]; /**< buf state: DL buffer descs */
};
typedef struct cmtspeech_nokiamodem_s cmtspeech_nokiamodem_t;

#define CMTSPEECH_BACKEND_ID "cmtspeech_nokiamodem"

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

static int priv_setup_driver_bufconfig(cmtspeech_nokiamodem_t *priv);
static void priv_reset_buf_state_to_disconnected(cmtspeech_nokiamodem_t *priv);
static void priv_initialize_after_peer_reset(cmtspeech_nokiamodem_t *priv);

/* Function definitions */
/* -------------------------------------------------------------------- */

/**
 * Encodes a driver internal message 'type' to buffer pointed
 * by 'cmd', and param of 'param'. Returns size of encoded data (in octets).
 *
 * @param type is one of UL_DATA_READY, ...
 */
static int priv_msg_encode_driver_message(cmtspeech_cmd_t *cmd, uint8_t type, uint8_t param)
{
  cmd->d.buf[BYTE0] = (type << 4) | CMTSPEECH_DOMAIN_INTERNAL;
  cmd->d.buf[BYTE1] = 0x0;
  cmd->d.buf[BYTE2] = 0x0;
  cmd->d.buf[BYTE3] = param & CMD_PARAM_MASK;
  
  return 4;
}

static int priv_write_data(cmtspeech_nokiamodem_t *priv, cmtspeech_cmd_t msg)
{
  int res =
    write(priv->d.fd, msg.d.buf, sizeof(msg));
  TRACE_DEBUG(DEBUG_PREFIX "wrote %s, fd %d, res %d.", 
	      cmtspeech_msg_type_to_string(msg), priv->d.fd, res);

  /* note: priv->bcstate.io_errors are not updated for data 
   *       i/o for performance reasons */

  return res;
}

static int priv_write(cmtspeech_nokiamodem_t *priv, cmtspeech_cmd_t msg)
{
  return cmtspeech_bc_write_command(&priv->bcstate, priv, msg, priv->d.fd);
}

/**
 * Lock/unlocks system resources so that no changes in DVFS mods
 * that would impact SSI driver clocking will be initiated.
 */
static void priv_set_ssi_lock(cmtspeech_nokiamodem_t *priv, bool enabled) 
{
  int fd;

  fd =
    open(PM_VDD2_LOCK_INTERFACE, O_WRONLY, 0);

  if (fd >= 0) {
    char buf[2];
    int res;
    snprintf(buf, sizeof(buf), "%hu", 
	     enabled == true ? PM_VDD2_LOCK_TO_OPP3 : PM_VDD2_UNLOCK);
    res = write(fd, buf, sizeof(buf));
    TRACE_IO("setting VDD2 lock to '%s', res %d.", buf, res);
    close(fd);
  }
  else {
    TRACE_IO(DEBUG_PREFIX "Unable to lock VDD2, dev %s ('%s').", PM_VDD2_LOCK_INTERFACE, strerror(errno));
  }
}

/**
 * Allocates SSI wakeline from the driver.
 *
 * Note: this function requires a kernel driver that uses 
 *       the SSI control channel for wakeline signaling.
 *
 * @param priv context
 * @param id bitmask identifying the wakeline user (see WAKELINE_*)

 * @return zero on success, non-zero if errors in raising 
 *         the wakeline
 */
static int priv_acquire_wakeline(cmtspeech_nokiamodem_t *priv, int id)
{
  int res = 0;
  if (priv->d.wakeline_users == 0) {
    unsigned int status = 1;
    res = ioctl (priv->d.fd, CS_SET_WAKELINE, &status);  
    TRACE_IO(DEBUG_PREFIX "Toggled SSI wakeline to %u by id %02x (res %d).", status, id, res);

    /* step: lock VDD2 whenever modem needs to be able to send
     *       messages towards us */
    priv_set_ssi_lock(priv, true);

  }
  priv->d.wakeline_users |= id;
  return res;
}

/**
 * Releases SSI driver wakeline.
 *
 * @param priv context
 * @param id bitmask identifying the wakeline user (see WAKELINE_*)
 * @return zero on success, non-zero if errors in raising 
 *         the wakeline
 */
static int priv_release_wakeline(cmtspeech_nokiamodem_t *priv, int id)
{
  int res = 0;
  SOFT_ASSERT(priv->d.wakeline_users != 0);
  if (priv->d.wakeline_users != 0) {
    priv->d.wakeline_users &= ~id;
    if (priv->d.wakeline_users == 0) {
      unsigned int status = 0;
      res = ioctl (priv->d.fd, CS_SET_WAKELINE, &status);  
      TRACE_IO(DEBUG_PREFIX "Toggled SSI wakeline to %u by id %02x (res %d).", status, id, res);

      /* step: unlock VDD2 whenever we are sure modem no longer needs
       *       needs to send messages towards us */
      priv_set_ssi_lock(priv, false);
    }
  }
  return res;
}

static void priv_reset_wakeline_state(cmtspeech_nokiamodem_t *priv)
{
  unsigned int status = 0;
  int res;

  TRACE_IO(DEBUG_PREFIX "Reseting SSI wakeline state (user mask %x at reset).", priv->d.wakeline_users);

  /* step: make sure VDD2 is unlocked */
  if (priv->d.wakeline_users != 0) {
    priv_set_ssi_lock(priv, false);
  }

  res = ioctl (priv->d.fd, CS_SET_WAKELINE, &status);
  SOFT_ASSERT(res == 0);
  priv->d.wakeline_users = 0;

}

/**
 * Sends a RESET_CONN_REQ message to the modem.
 *
 * As modem is currently not able to toggle the SSI wakeline
 * by itself, we need to keep the wakeline up until we get
 * a response. Otherwise the modem would not be able to respond.
 *
 * See also priv_handle_reset_response() and priv_handle_reset_conn_req().
 */
static int priv_send_reset(cmtspeech_nokiamodem_t *priv)
{
  int res = 0;

  res = priv_acquire_wakeline(priv, WAKELINE_RESET);
  if (res == 0) {
    cmtspeech_cmd_t msg;
    cmtspeech_msg_encode_reset_conn_req(&msg);

    /* note: priv_write() causes a state transition by 
    *        call cmtspeech_bc_post_command() */

    res = priv_write(priv, msg);
    if (res < 0) {
      res = priv_release_wakeline(priv, WAKELINE_RESET);
    }
  }

  
  /* step: Reset the driver buffer configuration to initial 
   *       state. This is done already after sending the
   *       request as it is possible that CMT has become 
   *       unavailable and there will be no response. 
   *
   *       Note that we must still keep the wakeline up.
   *       Otherwise CMT would not be able to send a response.
   *       See priv_handle_reset_response().
   */
  priv->slot_size = 0;
  priv_setup_driver_bufconfig(priv);

  return res;
}

/**
 * Handles a received RESET_CONN_RESP.
 *
 * See also priv_send_reset() and priv_handle_reset_conn_req().
 */
static void priv_handle_reset_response(cmtspeech_nokiamodem_t *priv)
{
  priv_release_wakeline(priv, WAKELINE_RESET);
  return;
}

static void priv_initialize_buffer_descriptor(nokiamodem_buffer_desc_t *bd, uint8_t *slotbuf, unsigned int slot_size, int frame_flags, int index, int desc_flags)
{
  bd->bd.type = CMTSPEECH_BUFFER_TYPE_PCM_S16_LE;
  bd->bd.count = slot_size;
  bd->bd.pcount = slot_size - CMTSPEECH_DATA_HEADER_LEN;
  bd->bd.size = slot_size;
  bd->bd.frame_flags = frame_flags;
  bd->bd.data = slotbuf;
  bd->bd.payload = slotbuf + CMTSPEECH_DATA_HEADER_LEN;
  bd->bd.index = index;
  bd->flags = desc_flags;
}

static void priv_initialize_tx_buffer_descriptors_mmap(cmtspeech_nokiamodem_t *priv, int desc_flags)
{
  int i;
  struct cs_mmap_config_block *mmap_cfg = 
    (struct cs_mmap_config_block *)priv->d.buf;

  for(i = 0; i < UL_SLOTS; i++) {
    uint8_t *slotbuf_i =
      priv->d.buf + mmap_cfg->tx_offsets[i];

    priv_initialize_buffer_descriptor(&priv->ulbufdesc[i], slotbuf_i, priv->slot_size, 0, i, desc_flags);
  }
}

static void priv_initialize_rx_buffer_descriptors_mmap(cmtspeech_nokiamodem_t *priv, int desc_flags)
{
  int i;
  struct cs_mmap_config_block *mmap_cfg = 
    (struct cs_mmap_config_block *)priv->d.buf;

  for(i = 0; i < DL_SLOTS; i++) {
    uint8_t *slotbuf_i =
      priv->d.buf +  mmap_cfg->rx_offsets[i];

    priv_initialize_buffer_descriptor(&priv->dlbufdesc[i], slotbuf_i, priv->slot_size, 0, i, desc_flags);
  }
}

/**
 * Reinitializes the DL buffer descriptors in case the sample
 * layout has changed.
 * 
 * Note: priv_initialize_dl_buffer_descriptors*() must be called
 * at least once before this function.
 */
static void priv_update_dl_buffer_descriptors(cmtspeech_nokiamodem_t *priv)
{
  int i;

  for(i = 0; i < DL_SLOTS; i++) {
    uint8_t *slotbuf_i;

    if (priv->bcstate.sample_layout == CMTSPEECH_SAMPLE_LAYOUT_SWAPPED_LE) {
      slotbuf_i = 
	priv->d.dlswapbuf + i * priv->slot_size;
    }
    else {
      slotbuf_i = 
	priv->d.buf + priv->d.rx_offsets[i];
    }
    
    priv_initialize_buffer_descriptor(&priv->dlbufdesc[i],
				      slotbuf_i,
				      priv->slot_size,
				      priv->dlbufdesc[i].bd.frame_flags,
				      i, 0);
  }
}

static void priv_handle_ssi_config_resp(cmtspeech_nokiamodem_t *priv)
{
  /* step: release SSI wakeline */
  if (priv->bcstate.proto_state == CMTSPEECH_STATE_DISCONNECTED &&
      priv->bcstate.priv_state != BC_STATE_CONNECTING) {
    /* note: only disable the wakeline when call is terminated,
     *       not after each transaction */
    priv_release_wakeline(priv, WAKELINE_CALL);
  }
  else {
    /* note: In case sample buffers are already set up, reinitialize
     *       the DL buffers in case modem has requested a different
     *       sample layout. For UL, reinitialization is not
     *       needed as sample-swapping is done in place. */
    if (priv->slot_size > 0)
      priv_update_dl_buffer_descriptors(priv);
  }
  
  return;
}


static inline int priv_locked_bufdescs(cmtspeech_nokiamodem_t *priv, bool verbose)
{
  int i, locked = 0;

  for(i = 0; i < UL_SLOTS; i++)
    if (priv->ulbufdesc[i].flags & BUF_LOCKED) {
      ++locked;
      if (verbose == true)
	TRACE_IO(DEBUG_PREFIX "UL buf %i(%p,data:%p) locked.", 
		 i, &priv->ulbufdesc[i].bd, priv->ulbufdesc[i].bd.data);
    }

  for(i = 0; i < DL_SLOTS; i++)
    if (priv->dlbufdesc[i].flags & BUF_LOCKED) {
      ++locked;
      if (verbose == true)
	TRACE_IO(DEBUG_PREFIX "DL buf %i(%p,data:%p) locked.", 
		 i, &priv->dlbufdesc[i].bd, priv->dlbufdesc[i].bd.data);
    }

  return locked;
}

/**
 * Marks all buffer descriptors as invalid. This is needed
 * when buffer layout needs to be changed, but application
 * is holding on to some buffer descriptors. The actual 
 * change in layout has to be postponed until all invalid
 * buffers are released back to the library.
 *
 * @see priv_drvbuf_layout_change_buffer_released()
 */
static int priv_invalidate_buffer_slots(cmtspeech_nokiamodem_t *priv)
{
  int i;

  for(i = 0; i < UL_SLOTS; i++)
    priv->ulbufdesc[i].flags |= BUF_INVALID;

  for(i = 0; i < DL_SLOTS; i++)
    priv->dlbufdesc[i].flags |= BUF_INVALID;

  return 0;
}

/**
 * Resets state related to data path configuration. Should be
 * only called from priv_setup_driver_bufconfig().
 */ 
static void priv_reset_buf_state_to_disconnected(cmtspeech_nokiamodem_t *priv)
{
  priv->slot_size = 0;
  priv->conf_sample_rate = (uint8_t)-1;
  priv->conf_data_length = (uint8_t)-1;
  priv->speech_config_resp_pend = false;
  priv->ul_errors = 0;
  priv->rx_ptr_hw = -1;
  priv->rx_ptr_appl = -1;
  priv->ul_slot_app = -1;
  memset(priv->dlbufdesc, 0, DL_SLOTS * sizeof(nokiamodem_buffer_desc_t));
  memset(priv->ulbufdesc, 0, UL_SLOTS * sizeof(nokiamodem_buffer_desc_t));
  priv_invalidate_buffer_slots(priv);
  SOFT_ASSERT(priv_locked_bufdescs(priv, true) == 0);
}

cmtspeech_t* cmtspeech_open(void)
{
  cmtspeech_nokiamodem_t *priv =
    malloc(sizeof(cmtspeech_nokiamodem_t));
  int fd =
    open(CS_DEV_FILE_NAME, O_RDWR);
  size_t ringbufsize = EVENT_BUFFER_SIZE * sizeof(cmtspeech_event_t);
  uint8_t *ringbufdata =
    malloc(ringbufsize);
  uint8_t *mmap_buf = NULL;

  TRACE_INFO(DEBUG_PREFIX "Opening device, libcmtspeechdata v%s.", VERSION);

  if (fd >= 0)
    mmap_buf = (uint8_t*)mmap(NULL, SHARED_MEMORY_AREA_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  if (priv && fd >= 0 && ringbufdata && mmap_buf != MAP_FAILED) {

    TRACE_IO(DEBUG_PREFIX "Succesfully opened device %s (fd %d).", CS_DEV_FILE_NAME, fd);

    cmtspeech_bc_open(&priv->bcstate);

    priv->d.wakeline_users = 0;
    priv->d.fd = fd;
    priv->d.flags = 0;
    ring_buffer_init(&priv->d.evbuf, ringbufdata, ringbufsize);

    /* note: we define the memory layout */
    priv->d.buf = mmap_buf;
    priv->d.buflen = SHARED_MEMORY_AREA_PAGE;
    TRACE_IO(DEBUG_PREFIX "Mapped memory area of %u octets at %p.", priv->d.buflen, priv->d.buf); 
#if PROTOCOL_SUPPORT_SAMPLE_SWAP
    priv->d.dlswapbuf = malloc(MAX_SLOT_SIZE * DL_SLOTS);
#else
    priv->d.dlswapbuf = NULL;
#endif
    priv_reset_buf_state_to_disconnected(priv);
  }
  else {
    TRACE_ERROR(DEBUG_PREFIX "unable to open device %s ('%s').", CS_DEV_FILE_NAME, strerror(errno));
    if (mmap_buf)
      munmap(mmap_buf, SHARED_MEMORY_AREA_PAGE);
    if (fd >= 0)
      close(fd);
    if (priv)
      free(priv);
    if (ringbufdata)
      free(ringbufdata);
    priv = 0;
  }

  return (cmtspeech_t*)priv;
}

int cmtspeech_close(cmtspeech_t *context)
{
  int res = 0;

  if (context) {
    cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;

    priv_reset_wakeline_state(priv);

    if (priv->d.buf)
      munmap(priv->d.buf, priv->d.buflen);

    close(priv->d.fd);

    if (priv->d.evbuf.data)
      free(priv->d.evbuf.data);

    if (priv->d.dlswapbuf)
      free(priv->d.dlswapbuf);

    /* step: finally free the context pointer itself*/
    free(priv);
  }
  else 
    res = -1;

  return res;
}

int cmtspeech_descriptor(cmtspeech_t *context)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;
  return priv->d.fd;
}

/**
 * Queues a control event to internal buffer.
 *
 * If the queue gets full, the new event will overwrite
 * the older event currently in the queue.
 *
 * @return 0 on success, -1 if queue was full and existing
 *         old data was overwritten
 */
static int priv_queue_control_event(cmtspeech_nokiamodem_t *priv, const cmtspeech_event_t *event)
{
  int cavail =
    ring_buffer_cavail_for_write(&priv->d.evbuf);
  uint8_t *to = priv->d.evbuf.data + priv->d.evbuf.write_idx;
  int res = 0;
  int eventsize = sizeof(*event);

  if (cavail < eventsize) {
    uint8_t *from = priv->d.evbuf.data + priv->d.evbuf.read_idx;
    const cmtspeech_event_t *oldev = (const cmtspeech_event_t *)from;
    int avail =
      ring_buffer_avail_for_write(&priv->d.evbuf);

    TRACE_ERROR(DEBUG_PREFIX
		"control event queue overflow "
		"(lostmsg:%d, newmsg:%d, avail=%u, cavail=%u, event=%u)",
		oldev->msg_type, event->msg_type, avail, cavail, eventsize);

    ring_buffer_move_read(&priv->d.evbuf, eventsize);
    cavail = ring_buffer_cavail_for_write(&priv->d.evbuf);
    res = -1;
  }

  SOFT_ASSERT(cavail >= eventsize);

  memcpy(to, event, eventsize);
  ring_buffer_move_write(&priv->d.evbuf, eventsize);

  return res;
}

/**
 * Dequeues a control event
 *
 * @return 0 on success, -1 if queue is empty and no event
 *         can be read
 */
static int priv_dequeue_control_event(cmtspeech_nokiamodem_t *priv, cmtspeech_event_t *event)
{
  uint8_t *from = priv->d.evbuf.data + priv->d.evbuf.read_idx;
  int cavail =
    ring_buffer_cavail_for_read(&priv->d.evbuf);
  int eventsize = sizeof(*event);

  if (cavail < eventsize) {
    int avail =
      ring_buffer_avail_for_read(&priv->d.evbuf);

    TRACE_IO(DEBUG_PREFIX
	     "notice: control event dequeue with no data "
	     "(avail=%u, cavail=%u, event=%u)",
	     avail, cavail, eventsize);

    return -1;
  }

  memcpy(event, from, eventsize);
  ring_buffer_move_read(&priv->d.evbuf, eventsize);

  return 0;
}

static int priv_setup_driver_bufconfig_v2api(cmtspeech_nokiamodem_t *priv)
{
  struct cs_buffer_config drvcfg;
  int res, desc_flags = 0;
  unsigned int if_ver;

  memset(&drvcfg, 0, sizeof(drvcfg));
     
  /* step: fill the driver config struct */
  drvcfg.buf_size = priv->slot_size;
  drvcfg.rx_bufs = DL_SLOTS;
  drvcfg.tx_bufs = UL_SLOTS;
  drvcfg.flags = CS_FEAT_TSTAMP_RX_CTRL | CS_FEAT_ROLLING_RX_COUNTER;

  res = ioctl(priv->d.fd, CS_GET_IF_VERSION, &if_ver);
  if (res < 0) {
    if_ver = 0;
  }

  res = ioctl(priv->d.fd, CS_CONFIG_BUFS, &drvcfg);
  TRACE_IO(DEBUG_PREFIX "Initialized driver buffer: res %d, params size=%u.",
	   res, drvcfg.buf_size);
  if (res == 0) {
    struct cs_mmap_config_block *mmap_cfg = 
      (struct cs_mmap_config_block *)priv->d.buf;
    int i;

    TRACE_IO(DEBUG_PREFIX "mmap_cfg: ver=%u, buf_size=%u, rxbufs=%u, txbufs=%u",
	     if_ver, mmap_cfg->buf_size, mmap_cfg->rx_bufs, mmap_cfg->tx_bufs);

    /* note: rolling rx pointer feature introduced in v1 */
    if (if_ver > 0)
      priv->d.flags |= DRIVER_FEAT_ROLLING_RX_PTR;

    /* note: run following only when activating */
    if (priv->slot_size > 0) {
      for(i = 0; i < DL_SLOTS; i++) 
	TRACE_IO(DEBUG_PREFIX "mmap_cfg: rxbuf #%u = %u",
		 i, mmap_cfg->rx_offsets[i]);

      for(i = 0; i < UL_SLOTS; i++) 
	TRACE_IO(DEBUG_PREFIX "mmap_cfg: txbuf #%u = %u",
		 i, mmap_cfg->tx_offsets[i]);

      priv_initialize_rx_buffer_descriptors_mmap(priv, desc_flags);
      priv_initialize_tx_buffer_descriptors_mmap(priv, desc_flags);

      priv->d.tstamp_rx_ctrl_offset =
	offsetof(struct cs_mmap_config_block, tstamp_rx_ctrl);
      TRACE_IO(DEBUG_PREFIX "mmap_cfg: rx-ctrl-tstamp=%u",
	       priv->d.tstamp_rx_ctrl_offset);
    }
  }
  else {
    TRACE_ERROR(DEBUG_PREFIX "CS_CONFIG_BUFS returned an error (%d): %s",
		errno, strerror(errno), drvcfg.buf_size);
  }

  return res;
}

/**
 * Configures the driver for data frame exchange.
 */
static int priv_setup_driver_bufconfig(cmtspeech_nokiamodem_t *priv)
{
  int res;

  if (priv->slot_size == 0) {
    /* note: speech data transfer terminated, reset buffer state */
    priv_reset_buf_state_to_disconnected(priv);
  }
  else {
    /* note: driver speech frame layout changed */
    priv->ul_errors = 0;
    priv->rx_ptr_hw = -1;
    priv->rx_ptr_appl = -1;
    priv->ul_slot_app = 0;
  }

  /* step: pass new parameters to the driver */
  res = priv_setup_driver_bufconfig_v2api(priv);
  if (res < 0) {
    TRACE_ERROR(DEBUG_PREFIX "Unable to set up buffer config for call");
  }

  return res;
}

/**
 * Sets up a new buffer configuration to driver and
 * sends a reply to the CMT (SPEECH_CONFIG_RESP).
 *
 * @param priv context pointer
 *
 * @return 0 on success, non-zero error otherwise
 */
static int priv_setup_and_send_speech_config_reply(cmtspeech_nokiamodem_t *priv)
{
  int res;
  uint8_t reply_result = 0;
  cmtspeech_cmd_t respcmd;

  priv->speech_config_resp_pend = false;
  res = priv_setup_driver_bufconfig(priv);

  /* step: convert result into protocol syntax */
  reply_result = (res == 0 ? 0 : 1);

  /* step: encode and send the reply to CMT */
  res = cmtspeech_msg_encode_speech_config_resp(&respcmd, 
						reply_result);
    
  /* step: send the response */
  res = priv_write(priv, respcmd);
  if (res == CMTSPEECH_CTRL_LEN) {
    res = 0;
  }
  else
    res = -1;

  return res;
}

/**
 * Completes processing of SPEECH_CONFIG_REQ. 
 *
 * This function should be called when SPEECH_CONFIG_REQ
 * processing is blocked due to locked buffers, and when
 * one or more buffers have been released by 
 * the application. When the last locked buffer has
 * been released, this function will complete 
 * transaction and send a SPEECH_CONFIG_RESP. This 
 * process also involves resetting the driver buffer 
 * layout and restarting the DMA requests.
 */
static int priv_drvbuf_layout_change_buffer_released(cmtspeech_nokiamodem_t *priv, nokiamodem_buffer_desc_t*descs, cmtspeech_buffer_t *buf)
{
  nokiamodem_buffer_desc_t *desc = &descs[buf->index];

  SOFT_ASSERT(priv->speech_config_resp_pend == true);
  SOFT_ASSERT(desc->flags & BUF_LOCKED);
  SOFT_ASSERT(desc->flags & BUF_INVALID);

  desc->flags &= ~BUF_LOCKED;

  int locked = priv_locked_bufdescs(priv, true);

  TRACE_IO(DEBUG_PREFIX "Buffer release during layout change, locked %d.", locked);

  if (locked == 0) {
    int res = priv_setup_and_send_speech_config_reply(priv);
    if (res != 0) {
      TRACE_INFO(DEBUG_PREFIX "Sending SPEECH_CONFIG_RESP (delayed) failed with %d.", res);
      priv_invalidate_buffer_slots(priv);
    }
    SOFT_ASSERT(priv->speech_config_resp_pend != true);
  }

  return 0;
}

/**
 * Handles an incoming SPEECH_CONFIG_REQ message.
 */
static int priv_handle_speech_config(cmtspeech_nokiamodem_t *priv, cmtspeech_event_t *event)
{
  int res = 0;
  int frame_size;

  if (event->msg.speech_config_req.speech_data_stream) {
    /* case: call is started or updated */

    priv->conf_sample_rate = event->msg.speech_config_req.sample_rate;
    priv->conf_data_length = CMTSPEECH_DATA_LENGTH_20MS;

    if (priv->conf_sample_rate == CMTSPEECH_SAMPLE_RATE_8KHZ) {
      frame_size = 160 * PCM_SAMPLE_SIZE;
    }
    else if (priv->conf_sample_rate == CMTSPEECH_SAMPLE_RATE_16KHZ) {
      frame_size = 320 * PCM_SAMPLE_SIZE;
    }
    else {
      TRACE_ERROR(DEBUG_PREFIX "Invalid sample rate (%u) in SPEECH_CONFIG_REQ.", 
		 event->msg.speech_config_req.sample_rate);
      frame_size = -1;
      priv->slot_size = 0;
      res = 1;
    }

    if (frame_size > 0) 
      priv->slot_size = frame_size + CMTSPEECH_DATA_HEADER_LEN;
  }
  else {
    /* case: call terminated */

    SOFT_ASSERT(event->msg.speech_config_req.speech_data_stream == 0);

    frame_size = 0;
    priv->slot_size = 0;

    TRACE_DEBUG(DEBUG_PREFIX "Parsing SPEECH_CONFIG_REQ, call terminated.");
  }

  /* note: DMA reconfiguration is needed in all cases, so 
   *       layout is marked as changed even in cases where 
   *       the frame size remains the same. */
  event->msg.speech_config_req.layout_changed = true;

  if (priv_locked_bufdescs(priv, false) == 0) {
    /* note: If no buffers are currently locked by the application,
     *       continue to execute the change immediately. This
     *       action will cancel all pending DMA transfers (both UL 
     *       and DL), as well as reset the mmap area state. */
    res = priv_setup_and_send_speech_config_reply(priv);
  }
  else {
    TRACE_IO(DEBUG_PREFIX "Buffer layout changed, but application is holding to %d locked buffers. Postponing SPEECH_CONFIG_RESP reply.", priv_locked_bufdescs(priv, true));

    priv->speech_config_resp_pend = true;
    priv_invalidate_buffer_slots(priv);

    res = 0;
  }

  return res;
}


static void priv_initialize_after_peer_reset(cmtspeech_nokiamodem_t *priv)
{
  TRACE_IO(DEBUG_PREFIX "Peer reset, initializing local state.");
  priv->slot_size = 0;
  priv_setup_driver_bufconfig(priv);
  cmtspeech_bc_state_change_reset(priv);
  priv_reset_wakeline_state(priv);
}

/**
 * Handles an incoming SPEECH_CONFIG_REQ message.
 *
 * See also priv_handle_reset_response() and priv_send_reset().
 */
static int priv_handle_reset_conn_req(cmtspeech_nokiamodem_t *priv)
{
  cmtspeech_cmd_t respcmd;
  int res = -1, i;

  i = cmtspeech_msg_encode_reset_conn_resp(&respcmd);
  SOFT_ASSERT(i == CMTSPEECH_CTRL_LEN);
  i = priv_write(priv, respcmd);
  if (i == CMTSPEECH_CTRL_LEN) {
    res = 0;
  }
  
  /* step: Reset driver state. See notes in priv_send_reset().
   *       As reset has been initiated by CMT, we can close
   *       the wakeline already at this point.
   */ 
  priv_initialize_after_peer_reset(priv);

  return res;
}

static int priv_init_bufconfig_for_test_ramp_ping(cmtspeech_nokiamodem_t *priv, uint8_t ramplen)
{
  int res = 0;

  TRACE_DEBUG(DEBUG_PREFIX "Initializing driver for TEST_RAMP_PING (ramplen %u words).", ramplen);

  /* note: in case DMA has not been setup yet, do it now with
   *       the test ramp parameters (possible race conditions
   *       between subsequent new SPEECH_CONFIG_REQ messages
   *       are not handled) */

  unsigned int new_slot_size = CMTSPEECH_DATA_HEADER_LEN + ramplen * 4;

  if (priv->slot_size != new_slot_size) {
    priv->slot_size = new_slot_size;
    res = priv_setup_driver_bufconfig(priv);
    if (res < 0)
      return res;
  }

  return res;
}

/**
 * Handles an incoming TEST_RAMP_PING message.
 */
static int priv_handle_test_ramp_ping(cmtspeech_nokiamodem_t *priv, const cmtspeech_cmd_t cmd)
{
  cmtspeech_cmd_t respcmd;
  int res = 0, i;
  uint8_t channel, replychannel, rampstart, ramplen;
  const unsigned int ul_slot = 0;
  uint8_t *slotbuf = priv->d.buf + 
    priv->d.tx_offsets[ul_slot];

  SOFT_ASSERT(cmtspeech_msg_get_domain(cmd) == CMTSPEECH_DOMAIN_CONTROL);
  SOFT_ASSERT(cmtspeech_msg_get_type(cmd) == CMTSPEECH_TEST_RAMP_PING);

  cmtspeech_msg_decode_test_ramp_ping(cmd, 
				      &channel,
				      &replychannel,
				      &rampstart,
				      &ramplen);

  TRACE_IO(DEBUG_PREFIX "Handling inbound TEST_RAMP_PING (ch:%u, replych:%u, start-val:0x%02x, ramplen %u words).", 
		 channel, replychannel, rampstart, ramplen);
  
  res = priv_acquire_wakeline(priv, WAKELINE_TEST_RAMP_PING);
  SOFT_ASSERT(res == 0);

  res = cmtspeech_msg_encode_ul_data_header(slotbuf, 4, 0xffff, CMTSPEECH_DATA_LENGTH_NONE, CMTSPEECH_SAMPLE_RATE_NONE, CMTSPEECH_DATA_TYPE_INVALID);
  SOFT_ASSERT(res == CMTSPEECH_DATA_HEADER_LEN);

  /* note: override some UL_SPEECH_DATA_FRAME header bits */
  slotbuf[BYTE1] = ramplen;
  slotbuf[BYTE2] = 0x80;

  res = priv_init_bufconfig_for_test_ramp_ping(priv, ramplen);
  if (res < 0)
    return res;

  SOFT_ASSERT(priv->slot_size == CMTSPEECH_DATA_HEADER_LEN + (unsigned int)ramplen * 4);

  /* step: generate the ramp */
  slotbuf += 4;
  for(i = 0; i < ramplen*4; i++)
    slotbuf[i] = rampstart++;

  /* notify driver that slot '0' is ready for sending */    
  res = priv_msg_encode_driver_message(&respcmd, CS_COMMAND(CS_TX_DATA_READY), ul_slot & CMD_PARAM_MASK);
  if (res == CMTSPEECH_CTRL_LEN)
    res = priv_write(priv, respcmd);

  if (res == CMTSPEECH_CTRL_LEN) 
    return 0;

  return -1;
}

/**
 * Returns diff between the buffer position, to which hardware driver
 * will write next (reported via mmap segment), and the application
 * buffer position.
 *
 * In normal conditions, the delay should vary between zero
 * and 'DL_SLOTS-1'.
 */
static inline int priv_rx_hw_delay(cmtspeech_nokiamodem_t *priv)
{
  struct cs_mmap_config_block *mmap_cfg =
    (struct cs_mmap_config_block *)priv->d.buf;

  return ((mmap_cfg->rx_ptr_boundary +
	   mmap_cfg->rx_ptr - priv->rx_ptr_appl) %
	  mmap_cfg->rx_ptr_boundary);
}

/**
 * Returns the number of available RX buffers.
 *
 * Note that this number does not include possible frames
 * queued in the driver (see priv_rx_hw_delay()).
 */
static inline int priv_rx_ptr_avail(const cmtspeech_nokiamodem_t *priv)
{
 struct cs_mmap_config_block *mmap_cfg =
    (struct cs_mmap_config_block *)priv->d.buf;

 return ((mmap_cfg->rx_ptr_boundary +
	  priv->rx_ptr_hw - priv->rx_ptr_appl) %
	 mmap_cfg->rx_ptr_boundary);
}

static void handle_inbound_rx_data_received(cmtspeech_nokiamodem_t *priv, const cmtspeech_cmd_t cmd, int *flags)
{
  int last_slot;
  int next_slot;

  TRACE_DEBUG(DEBUG_PREFIX "internal event DL_DATA_RECEIVED.");

  /* step: queue event for application */
  *flags |= CMTSPEECH_EVENT_DL_DATA;
  priv->rx_ptr_hw = (CS_PARAM_MASK & cmd.d.cmd);

  if (priv->rx_ptr_appl < 0)
    priv->rx_ptr_appl = priv->rx_ptr_hw;

  /* step: perform overrun checking */
  last_slot = (priv->rx_ptr_hw) % DL_SLOTS;
  next_slot = (last_slot + 1) % DL_SLOTS;

  if ((priv->d.flags & DRIVER_FEAT_ROLLING_RX_PTR) &&
      priv_rx_hw_delay(priv) >= DL_SLOTS) {
    struct cs_mmap_config_block *mmap_cfg =
      (struct cs_mmap_config_block *)priv->d.buf;

    /* xrun case 1:
     *     We have not reacted to driver wakeups fast enough and
     *     driver has overrun the rx buffer at this point. */

    TRACE_INFO(DEBUG_PREFIX "DL buffer overrun (mmaphw %d, hw %d, appl %d, slot %u, count %u, hwdelay %d).",
	       mmap_cfg->rx_ptr,
	       priv->rx_ptr_hw, priv->rx_ptr_appl, last_slot, DL_SLOTS,
	       priv_rx_hw_delay(priv));

    priv->dlbufdesc[last_slot].flags |= BUF_XRUN;
    *flags |= CMTSPEECH_EVENT_XRUN;
  }

  if (priv->dlbufdesc[next_slot].flags & BUF_LOCKED) {
    /* xrun case 2:
     *     The slot, to which driver will write next, is still owned
     *     by application - overrun is not certain, but data
     *     coherency cannot be guaranteed, so reporting as an XRUN */

    TRACE_INFO(DEBUG_PREFIX "possible DL buffer overrun (hw %d, appl %d, slot %u, count %u).", 
	       priv->rx_ptr_hw, priv->rx_ptr_appl, next_slot, DL_SLOTS);

    priv->dlbufdesc[next_slot].flags |= BUF_XRUN;
    *flags |= CMTSPEECH_EVENT_XRUN;
  }

  if (priv->dlbufdesc[last_slot].flags & BUF_LOCKED) {
    /* xrun case 3:
     *     The slot last used by driver is still owned by application */

    TRACE_INFO(DEBUG_PREFIX "DL buffer overrun (hw %d, appl %d, slot %u, count %u).", 
	       priv->rx_ptr_hw, priv->rx_ptr_appl, last_slot, DL_SLOTS);

    /* note: mark the overrun buffer and raise an event bit */
    priv->dlbufdesc[last_slot].flags |= BUF_XRUN;
    *flags |= CMTSPEECH_EVENT_XRUN;
  }

  /* step: reenable UL if paused (as DL path is now working) */
  if (priv->ul_errors > 0) {
    priv->ul_errors = 0;
    TRACE_IO(DEBUG_PREFIX "DL frame received, reactivating UL transfers.");
  }
}

/**
 * Processes messages sent by the modem and internal events
 * generated by the driver. The messages are parsed and
 * stored to an internal event queue. Application can fetch
 * the events with cmtspeech_read_event().
 *
 * @see cmtspeech_check_pending(), cmtspeech_read_event()
 *
 * @return 1 if events available, 0 if not, or a negative error code
 */
static int handle_inbound_control_message(cmtspeech_nokiamodem_t *priv, const cmtspeech_cmd_t cmd, int *flags)
{
  int res = 0;
  int type =
    cmtspeech_msg_get_type(cmd);
  int channel =
    cmtspeech_msg_get_domain(cmd);

  TRACE_DEBUG(DEBUG_PREFIX "handling bytes %02X:%02X:%02X:%02X, on channel %d.", 
	      cmd.d.buf[0], cmd.d.buf[1], cmd.d.buf[2], cmd.d.buf[3], cmtspeech_msg_get_domain(cmd));

  if (channel == CMTSPEECH_DOMAIN_CONTROL) {

    cmtspeech_event_t cmtevent;
    int retval;

    TRACE_IO(DEBUG_PREFIX "read bytes %02X:%02X:%02X:%02X, control channel message (%s).", 
	     cmd.d.buf[0], cmd.d.buf[1], cmd.d.buf[2], cmd.d.buf[3], cmtspeech_msg_type_to_string(cmd));

    retval = cmtspeech_bc_handle_command(&priv->bcstate, priv, cmd, &cmtevent);
    if (retval != 0) {
      res = -1;
    }
    else {
      switch(type)
	{
	case CMTSPEECH_RESET_CONN_REQ:
	  priv_handle_reset_conn_req(priv);
	  break;
	  
	case CMTSPEECH_RESET_CONN_RESP:
	  priv_handle_reset_response(priv);
	  break;
	  
	case CMTSPEECH_SSI_CONFIG_RESP:
	  priv_handle_ssi_config_resp(priv);
	  break;
	  
	case CMTSPEECH_SPEECH_CONFIG_REQ:
	  priv_handle_speech_config(priv, &cmtevent);
	  break;
	  
	case CMTSPEECH_TIMING_CONFIG_NTF:
	  {
	    /* note: copy the kernel timestamp for the message */
	    struct timespec *s = (struct timespec *)(priv->d.buf + priv->d.tstamp_rx_ctrl_offset); 
	    memcpy(&cmtevent.msg.timing_config_ntf.tstamp, s, sizeof(*s));
	  }
	  break;
	  
	case CMTSPEECH_UPLINK_CONFIG_NTF:
	  break;
	  
	case CMTSPEECH_TEST_RAMP_PING:
	  priv_handle_test_ramp_ping(priv, cmd);
	  break;
	  
	default:
	  TRACE_ERROR(DEBUG_PREFIX "ERROR: unknown control message of type %d.", type);
	  SOFT_ASSERT(false);
	}
      
      ONDEBUG(
	      struct timespec *s = (struct timespec *)(priv->d.buf + priv->d.stamp_rx_ctrl_offset); 
	      if (priv->d.stamp_rx_ctrl_offset > 0) 
		cmtspeech_trace_message(CMTSPEECH_TRACE_DEBUG, 
					DEBUG_PREFIX " control message received at %ld:%ldns.", s->tv_sec, s->tv_nsec)
	      );
      

      cmtspeech_bc_complete_event_processing(&priv->bcstate, priv, &cmtevent);

      if (type != CMTSPEECH_TEST_RAMP_PING) {
	priv_queue_control_event(priv, &cmtevent);
	*flags |= CMTSPEECH_EVENT_CONTROL;
	res = 1;
      }
    }
  }
  else if (channel == CMTSPEECH_DOMAIN_INTERNAL) {
    /* note: internal messages are generated by the backend 
     *       and are not sent by the CMT; they are primarily 
     *       used for wake-ups */

    switch(type)
      {
      case CS_COMMAND(CS_RX_DATA_RECEIVED):
	handle_inbound_rx_data_received(priv, cmd, flags);
	res = 1;
	break;

#ifdef CS_TX_DATA_SENT
      case CS_COMMAND(CS_TX_DATA_SENT):
	TRACE_DEBUG(DEBUG_PREFIX "internal event UL_DATA_SENT.");
	break;
#endif

#ifdef CS_CDSP_RESET_DONE
      case CS_COMMAND(CS_CDSP_RESET_DONE):
	{
	  cmtspeech_event_t cmtevent;
          TRACE_ERROR(DEBUG_PREFIX "ERROR: PEER_RESET received, reseting state");
	  cmtevent.msg_type = CMTSPEECH_EVENT_RESET;
	  cmtevent.prev_state = priv->bcstate.proto_state;
	  cmtevent.msg.reset_done.cmt_sent_req = 1;
	  priv_initialize_after_peer_reset(priv);
	  cmtevent.state = priv->bcstate.proto_state;
	  priv_queue_control_event(priv, &cmtevent);
	  *flags |= CMTSPEECH_EVENT_CONTROL;
	  res = 1;
	}
	break;
#endif

      default:
	TRACE_ERROR(DEBUG_PREFIX "ERROR: unknown control message of type %d (%02X:%02X:%02X:%02x).", 
		    type, cmd.d.buf[0], cmd.d.buf[1], cmd.d.buf[2], cmd.d.buf[3]);
      }
  }
  else
    res = -1;

  return res;
}

int cmtspeech_check_pending(cmtspeech_t *context, int *flags)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;
  cmtspeech_cmd_t cmd;
  int i, res = 0;

  if (!flags)
    return -1;

  *flags = 0;

  i = read(priv->d.fd, cmd.d.buf, CMTSPEECH_CTRL_LEN);
  if (i >= CMTSPEECH_CTRL_LEN) {
    res = handle_inbound_control_message(priv, cmd, flags);
    TRACE_DEBUG(DEBUG_PREFIX "read %d from cmtspeech device, handle res %d.", i, res);
  }
  else 
    TRACE_ERROR(DEBUG_PREFIX "read returned %d.", i);

  return res;
}

int cmtspeech_read_event(cmtspeech_t *context, cmtspeech_event_t *event)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;

  return priv_dequeue_control_event(priv, event);
}

int cmtspeech_state_change_call_status(cmtspeech_t *context, bool state)
{
  return cmtspeech_bc_state_change_call_status(context, state);
}

int cmtspeech_state_change_call_connect(cmtspeech_t *context, bool state)
{
  return cmtspeech_bc_state_change_call_connect(context, state);
}

int cmtspeech_state_change_error(cmtspeech_t *context)
{
  cmtspeech_nokiamodem_t *priv =
    (cmtspeech_nokiamodem_t*)context;

  return priv_send_reset(priv);
}

#if PROTOCOL_SUPPORT_SAMPLE_SWAP
/**
 * In-place swap of every 32bit word in buffer 'buf' (0xaabbccdd
 * is swapped to 0xccddaabb).
 */
static void priv_inplace_halfword_swap(uint8_t *buf, int n)
{
  uint32_t *wbuf = (uint32_t*)buf;
  int i;

  assert(n % 4 == 0);

  for(i = 0; i < n / 4; i++) {
    uint32_t tmp = *wbuf;
    *wbuf = (tmp << 16) | (tmp >> 16);
    ++wbuf;
  }

  return;
}
#endif /* PROTOCOL_SUPPORT_SAMPLE_SWAP */

/**
 * Returns the RX slot to give out to application
 *
 * If application has fallen behind the driver, negative
 * error code is returned for each missed slot.
 */
static int priv_rx_appl_slot(cmtspeech_nokiamodem_t *priv)
{
  if (priv->d.flags & DRIVER_FEAT_ROLLING_RX_PTR) {
    struct cs_mmap_config_block *mmap_cfg =
      (struct cs_mmap_config_block *)priv->d.buf;
    int avail = priv_rx_ptr_avail(priv);
    int delay = priv_rx_hw_delay(priv);

    if (avail == mmap_cfg->rx_ptr_boundary - 1) {
      TRACE_INFO(DEBUG_PREFIX "no frames available (hw %d, appl %d, avail %d, count %u, boundary %u).",
		 priv->rx_ptr_hw, priv->rx_ptr_appl, avail, DL_SLOTS, mmap_cfg->rx_ptr_boundary);
      return -ENODATA;
    }
    else if (delay >= DL_SLOTS) {
      TRACE_INFO(DEBUG_PREFIX "late appl wakeup (hw %d, appl %d, delay %d, count %u, boundary %u).",
		 priv->rx_ptr_hw, priv->rx_ptr_appl, delay, DL_SLOTS, mmap_cfg->rx_ptr_boundary);
      return -EPIPE;
    }
  }

  return priv->rx_ptr_appl % DL_SLOTS;
}

static void priv_bump_rx_ptr_appl(cmtspeech_nokiamodem_t *priv)
{
  struct cs_mmap_config_block *mmap_cfg = 
    (struct cs_mmap_config_block *)priv->d.buf;

  ++priv->rx_ptr_appl;

  if (priv->d.flags & DRIVER_FEAT_ROLLING_RX_PTR)
    priv->rx_ptr_appl %= mmap_cfg->rx_ptr_boundary;
  else
    priv->rx_ptr_appl %= DL_SLOTS;
}

/**
 * Resync buffer pointers after an RX buffer overrun. 
 * Returns a valid 
 */
static void priv_rx_ptr_appl_handle_xrun(cmtspeech_nokiamodem_t *priv)
{
  assert(priv->d.flags & DRIVER_FEAT_ROLLING_RX_PTR);

  TRACE_IO(DEBUG_PREFIX "DL xrun, reset hw/appl at %d", priv->rx_ptr_hw);

  priv->rx_ptr_appl = priv->rx_ptr_hw;
}

int cmtspeech_dl_buffer_acquire(cmtspeech_t *context, cmtspeech_buffer_t **buf)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;
  nokiamodem_buffer_desc_t *desc;
  uint16_t frame_counter;
  uint8_t spc_flags, data_length, data_type, sample_rate;
  int res;
  int slot;

  if (priv->rx_ptr_appl < 0)
    return -EINVAL;

  slot = priv_rx_appl_slot(priv);

  if (slot == -EPIPE) {
    priv_rx_ptr_appl_handle_xrun(priv);
    slot = priv->rx_ptr_appl % DL_SLOTS;
  }
  else if (slot < 0) {
    SOFT_ASSERT(slot == -ENODATA);
    return slot;
  }

  assert(slot >= 0);
  assert(slot < DL_SLOTS);

  desc = &priv->dlbufdesc[slot];

  /* note: must be bumped before the following checks
   *       that might return with -EINVAL */
  priv_bump_rx_ptr_appl(priv);

  if (desc->flags & BUF_INVALID)
    return -EINVAL;

  if (buf == NULL)
    return -EINVAL;

  SOFT_ASSERT(desc == &priv->dlbufdesc[slot]);

  res = cmtspeech_msg_decode_dl_data_header(desc->bd.data, CMTSPEECH_DATA_HEADER_LEN, &frame_counter, &spc_flags, &data_length, &sample_rate, &data_type);
  SOFT_ASSERT(res == 0);

  TRACE_DEBUG(DEBUG_PREFIX "DL frame received (hw %d, appl %d, slot %u, %u bytes, frame-counter %u, type %d):", priv->rx_ptr_hw, priv->rx_ptr_appl, slot, priv->slot_size, frame_counter, data_type);

  /* note: decode frame header and fill dlbufdesc fields appropriately */
  desc->bd.frame_flags = CMTSPEECH_DATA_TYPE_VALID;
  desc->bd.spc_flags = spc_flags;

  desc->flags |= BUF_LOCKED;
  *buf = &(desc->bd);

#if PROTOCOL_SUPPORT_SAMPLE_SWAP
  if (priv->bcstate.sample_layout == CMTSPEECH_SAMPLE_LAYOUT_SWAPPED_LE) {
    uint8_t *mmap_slot;

    mmap_slot = priv->d.buf + priv->d.rx_offsets[slot];

    /* note: Copy and swap frames from mmap area to a heap buffer. The buffer
     *       descriptor given to application points to the heap
     *       buffer. */

    SOFT_ASSERT(desc->bd.data != mmap_slot);
    memcpy(desc->bd.data, mmap_slot, desc->bd.count);

    if (desc->bd.pcount > 0) 
      priv_inplace_halfword_swap(desc->bd.payload, desc->bd.pcount);
  }
#endif

  /* note: some fields are set at buffer setup time in 
   *       priv_setup_driver_bufconfig() */
  SOFT_ASSERT(desc->bd.type == CMTSPEECH_BUFFER_TYPE_PCM_S16_LE);
  SOFT_ASSERT(desc->bd.count == (int)priv->slot_size);
  SOFT_ASSERT(desc->bd.pcount == desc->bd.count - CMTSPEECH_DATA_HEADER_LEN);
  SOFT_ASSERT(desc->bd.size == desc->bd.count);
  SOFT_ASSERT(desc->bd.payload == desc->bd.data + CMTSPEECH_DATA_HEADER_LEN);
  SOFT_ASSERT(desc->bd.index == slot);

  if (priv->bcstate.proto_state == CMTSPEECH_STATE_TEST_RAMP_PING_ACTIVE) {
    cmtspeech_bc_test_sequence_received(&priv->bcstate);
  }

  return 0;
}

int cmtspeech_dl_buffer_release(cmtspeech_t *context, cmtspeech_buffer_t *buf)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;
  int ret = 0;

  SOFT_ASSERT(buf != NULL);

  if ((priv->dlbufdesc[buf->index].flags & BUF_LOCKED) == 0)
    return -ENOENT;

  /* note: special case handling if layout change is pending */
  if (priv->speech_config_resp_pend == true) {
    priv_drvbuf_layout_change_buffer_released(priv, priv->dlbufdesc, buf);
    SOFT_ASSERT(priv->dlbufdesc[buf->index].flags == 0);
  }
  else {
    SOFT_ASSERT(priv->dlbufdesc[buf->index].bd.data == buf->data);
    SOFT_ASSERT(priv->dlbufdesc[buf->index].bd.payload == buf->payload);

    priv->dlbufdesc[buf->index].flags &= ~BUF_LOCKED;

    if (priv->dlbufdesc[buf->index].flags & BUF_XRUN) {
      priv->dlbufdesc[buf->index].flags &= ~BUF_XRUN;
      ret = -EPIPE;
    }
  }

  return ret;
}

cmtspeech_buffer_t *cmtspeech_dl_buffer_find_with_data(cmtspeech_t *context, uint8_t *data)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;
  int i;

  for(i = 0; i < DL_SLOTS; i++) {
    if (priv->dlbufdesc[i].bd.data == data)
      return &priv->dlbufdesc[i].bd;
  }
  return NULL;
}

cmtspeech_buffer_t *cmtspeech_dl_buffer_find_with_payload(cmtspeech_t *context, uint8_t *payload)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;
  int i;
  for(i = 0; i < DL_SLOTS; i++) {
    if (priv->dlbufdesc[i].bd.payload == payload)
      return &priv->dlbufdesc[i].bd;
  }
  return NULL;
}

int cmtspeech_test_data_ramp_req(cmtspeech_t *context, uint8_t rampstart, uint8_t ramplen)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;
  int res;

  res = priv_init_bufconfig_for_test_ramp_ping(priv, ramplen);
  if (res < 0)
    return res;
  
  res = priv_acquire_wakeline(priv, WAKELINE_TEST_RAMP_PING);
  if (res < 0)
    return res;

  return cmtspeech_bc_test_data_ramp_req(&priv->bcstate, context, priv->d.fd, CMTSPEECH_DOMAIN_CONTROL, CMTSPEECH_DOMAIN_DATA, rampstart, ramplen);
}

int cmtspeech_send_timing_request(cmtspeech_t *context)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;
  return cmtspeech_bc_send_timing_request(&priv->bcstate, context, priv->d.fd);
}

int cmtspeech_send_ssi_config_request(cmtspeech_t *context, bool state)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;

  if (state == true) {
    /* step: enable SSI wakeline */
    priv_acquire_wakeline(priv, WAKELINE_CALL);
  }

  return cmtspeech_bc_send_ssi_config_request(&priv->bcstate, context, priv->d.fd, state);
}

int cmtspeech_ul_buffer_acquire(cmtspeech_t *context, cmtspeech_buffer_t **buf)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;
  nokiamodem_buffer_desc_t *desc;

  if (cmtspeech_is_active(context) != true)
    return -EINVAL;

  if (priv->ul_slot_app < 0)
    return -EINVAL;

  desc = &priv->ulbufdesc[priv->ul_slot_app];

  SOFT_ASSERT(desc == &priv->ulbufdesc[priv->ul_slot_app]);

  if (desc->flags & BUF_INVALID)
    return -EINVAL;

  if (desc->flags & BUF_LOCKED)
    return -ENOBUFS;

  if (buf == NULL)
    return -EINVAL;

  desc->bd.frame_flags = 0;
  desc->flags |= BUF_LOCKED;

  /* note: some fields are set at buffer setup time in
   *       priv_setup_driver_bufconfig() */
  SOFT_ASSERT(desc->bd.type == CMTSPEECH_BUFFER_TYPE_PCM_S16_LE);
  SOFT_ASSERT(desc->bd.count == (int)priv->slot_size);
  SOFT_ASSERT(desc->bd.pcount == desc->bd.count - CMTSPEECH_DATA_HEADER_LEN);
  SOFT_ASSERT(desc->bd.size == desc->bd.count);
  SOFT_ASSERT(desc->bd.payload == desc->bd.data + CMTSPEECH_DATA_HEADER_LEN);
  SOFT_ASSERT(desc->bd.index == priv->ul_slot_app);

  *buf = &priv->ulbufdesc[priv->ul_slot_app].bd;

  ++priv->ul_slot_app;
  priv->ul_slot_app %= UL_SLOTS;

  return 0;
}

int cmtspeech_ul_buffer_release(cmtspeech_t *context, cmtspeech_buffer_t *buf)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;
  cmtspeech_cmd_t msg;
  int res;

  SOFT_ASSERT(cmtspeech_protocol_state(context) == CMTSPEECH_STATE_ACTIVE_DLUL);

  if (buf == NULL)
    return -EINVAL;

  SOFT_ASSERT(buf->index >= 0);

  /* note: special case handling if layout change is pending */
  if (priv->speech_config_resp_pend == true) {
    priv_drvbuf_layout_change_buffer_released(priv, priv->ulbufdesc, buf);
    SOFT_ASSERT(priv->ulbufdesc[buf->index].flags == 0);
    return 0;
  }

  SOFT_ASSERT(priv->ulbufdesc[buf->index].bd.data == buf->data);
  SOFT_ASSERT(priv->ulbufdesc[buf->index].bd.payload == buf->payload);

  if (priv->ul_errors >= MAX_UL_ERRORS_PAUSE) {
    if (priv->bcstate.io_errors > 0) {
      /* note: with some CMT firmware releases, the uplink DMA
	 transfers may fail due to loss of network coverage. To make
	 sure we don't return -EIO unless the connection is really
	 broken, we use 'io_errors' count as a trigger. */
      res = -EIO;
    }
    else  {
      /* note: if enough consecutive errors, do not send more
       *       UL frames to the modem */
      res = -EBUSY;
    }
  }
  /* note: only send frames when protocol state allows it */
  else if (cmtspeech_protocol_state(context) == CMTSPEECH_STATE_ACTIVE_DLUL) {
    static int16_t ul_counter = 0;

    TRACE_DEBUG(DEBUG_PREFIX "filling UL slot %u, size %u.",
		buf->index & CMD_PARAM_MASK, buf->pcount);

#if PROTOCOL_SUPPORT_SAMPLE_SWAP
    if (priv->bcstate.sample_layout == CMTSPEECH_SAMPLE_LAYOUT_SWAPPED_LE)
      priv_inplace_halfword_swap(buf->payload, buf->pcount);
#endif

    res = cmtspeech_msg_encode_ul_data_header(buf->data, CMTSPEECH_DATA_HEADER_LEN, ul_counter, priv->conf_data_length, priv->conf_sample_rate, buf->frame_flags);
    SOFT_ASSERT(res == CMTSPEECH_DATA_HEADER_LEN);

    /* note: send a CS_CS_UL_DATA_READY message to the driver */
    priv_msg_encode_driver_message(&msg, CS_COMMAND(CS_TX_DATA_READY), buf->index & CMD_PARAM_MASK);
    res = priv_write_data(priv, msg);
    if (res == CMTSPEECH_CTRL_LEN) {
      ul_counter += 4; /* increment of 4*5ms */
      res = 0;
    }
    else {
      TRACE_IO("UL frame send failed with %d (%d: %s)", res, errno, strerror(errno));

      if (res < 0 &&
	  errno == EBUSY) {
	res = -EBUSY;
	++priv->ul_errors;
      }
      else {
	/* note: SSI subsystem in invalid state, stop sending more UL
	 *       frames immediately */
	res = -EINVAL;
	priv->ul_errors = MAX_UL_ERRORS_PAUSE;
      }
    }
  }
  else {
    SOFT_ASSERT(cmtspeech_protocol_state(context) != CMTSPEECH_STATE_ACTIVE_DLUL);
    res = -EPIPE;
  }

  priv->ulbufdesc[buf->index].flags &= ~BUF_LOCKED;

  return res;
}

const char* cmtspeech_backend_name(cmtspeech_t* context)
{
  return CMTSPEECH_BACKEND_ID;
}

int cmtspeech_backend_message(cmtspeech_t *self, int type, int args, ...)
{
  /* no-op */
  return -1;
}

cmtspeech_bc_state_t *cmtspeech_bc_state_object(cmtspeech_t *context)
{
  cmtspeech_nokiamodem_t *priv = (cmtspeech_nokiamodem_t*)context;
  return &priv->bcstate;
}
