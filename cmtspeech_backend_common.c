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

/** @file cmspeech_backend_common.c
 *
 * Helper functions for CMT Speech Data library backend
 * implementations.
 */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "cmtspeech.h"
#include "cmtspeech_msgs.h"
#include "cmtspeech_backend_common.h"

#include "sal_debug.h"
#define DEBUG_PREFIX "backend_common: "

const char* priv_state_to_str(int state)
{
  const char *ret = NULL;
  ONTRACE((
  {
    static const char *states[] =
      { "INVALID", 
	"DISCONNECTED", 
	"CONNECTED", 
	"ACTIVE_DL", 
	"ACTIVE_DLUL",
	"TEST_RAMP_PING_ACTIVE"
      };
    ret = states[state];
  }
  ));
  return ret;
}

#if !defined(NDEBUG)
#define STATE_ASSERT(v) priv_state_assert(v, state->proto_state, #v, __LINE__)
#else
#define STATE_ASSERT(v)
#endif

static int priv_state_assert(int v, int state, const char* str, int line)
{
  if (!v) {
    TRACE_ERROR(DEBUG_PREFIX "FAILED PROTOCOL ASSERT (line %d, state '%s'):\n\t'%s'", line, priv_state_to_str(state), str);
  }
  return v;
}

static void priv_state_change_to(cmtspeech_bc_state_t *state, int newstate, int priv_state)
{
  if (newstate < 0) {
    TRACE_STATE_CHANGE(DEBUG_PREFIX "PROTOCOL_STATE <%s> (%d->%d)", 
		       priv_state_to_str(state->proto_state), 
		       state->priv_state, 
		       priv_state);
  }
  else {
    TRACE_STATE_CHANGE(DEBUG_PREFIX "PROTOCOL_STATE <%s> --> <%s> (%d->%d)", 
		       priv_state_to_str(state->proto_state), 
		       priv_state_to_str(newstate),
		       state->priv_state, 
		       priv_state);
    state->proto_state = newstate;
  }

  state->priv_state = priv_state;
}

static void priv_reset_state_to_disconnected(cmtspeech_bc_state_t *state)
{
  state->call_server_active = false;
  state->call_connected = false;
  state->sample_layout = -1;
  state->proto_state = CMTSPEECH_STATE_INVALID;
  state->priv_state = -1;
  state->io_errors = 0;

  /* state machine init */
  priv_state_change_to(state, CMTSPEECH_STATE_DISCONNECTED, BC_STATE_IN_SYNC);
}

int cmtspeech_bc_open(cmtspeech_bc_state_t *state)
{
  priv_reset_state_to_disconnected(state);

  /* CMT Speech Data protocol versions:
   * - v1: 8kHz/NB support only 
   * - v2: like v1, but transfer always 16kHz/WB independently 
   *       of the active codec */
  state->conf_proto_version = 1;

  return 0;
}

/* Public cmtspeech interface functions (common implementation
 * for all backends):
 * ----------------------------------------------------------- */

void cmtspeech_init(void)
{
  cmtspeech_initialize_tracing();
}

int cmtspeech_set_wb_preference(cmtspeech_t *context, bool enabled)
{
  cmtspeech_bc_state_t *state
    = cmtspeech_bc_state_object(context);

  if (cmtspeech_is_ssi_connection_enabled(context) == true)
    return -1;

  if (enabled)
    state->conf_proto_version = 2;
  else
    state->conf_proto_version = 1;

  return 0;
}

int cmtspeech_event_to_state_transition(const cmtspeech_t *context, const cmtspeech_event_t *cev)
{
  if (cev->prev_state == CMTSPEECH_STATE_DISCONNECTED &&
      cev->state == CMTSPEECH_STATE_CONNECTED)

    return CMTSPEECH_TR_1_CONNECTED;

  else if (cev->prev_state == CMTSPEECH_STATE_CONNECTED &&
	   cev->state == CMTSPEECH_STATE_DISCONNECTED)

    return CMTSPEECH_TR_2_DISCONNECTED;

  else if (cev->prev_state == CMTSPEECH_STATE_CONNECTED &&
	   cev->state == CMTSPEECH_STATE_ACTIVE_DL)

    return CMTSPEECH_TR_3_DL_START;

  else if ((cev->prev_state == CMTSPEECH_STATE_ACTIVE_DL ||
	    cev->prev_state == CMTSPEECH_STATE_ACTIVE_DLUL) &&
	   cev->state == CMTSPEECH_STATE_CONNECTED)

    return CMTSPEECH_TR_4_DLUL_STOP;

  else if (cev->prev_state == CMTSPEECH_STATE_ACTIVE_DL &&
	   cev->state == CMTSPEECH_STATE_ACTIVE_DL)

    return CMTSPEECH_TR_5_PARAM_UPDATE;

  else if (cev->msg_type == CMTSPEECH_TIMING_CONFIG_NTF)

    /* note: we currently cannot distinguish between TR_6 and TR_7,
             i.e. whether the timing update is a response to our
	     explicit request of new timing, or modem iniated update
	     of timing */
    return CMTSPEECH_TR_6_TIMING_UPDATE;

  else if (cev->msg_type == CMTSPEECH_EVENT_RESET)

    return CMTSPEECH_TR_10_RESET;

  else if (cev->prev_state == CMTSPEECH_STATE_ACTIVE_DLUL &&
	   cev->state == CMTSPEECH_STATE_ACTIVE_DL)

    return CMTSPEECH_TR_11_UL_STOP;

  else if (cev->prev_state == CMTSPEECH_STATE_ACTIVE_DL &&
	   cev->state == CMTSPEECH_STATE_ACTIVE_DLUL)

    return CMTSPEECH_TR_12_UL_START;

  return CMTSPEECH_TR_INVALID;
}

int cmtspeech_protocol_state(cmtspeech_t* context)
{
  cmtspeech_bc_state_t *state 
    = cmtspeech_bc_state_object(context);
  return state->proto_state;
}

int cmtspeech_bc_state_change_call_status(cmtspeech_t *context, bool server_state)
{
  cmtspeech_bc_state_t *state 
    = cmtspeech_bc_state_object(context);

  int res = 0;

  if (server_state != true) {
    /* case 1.a): State transition to CONNECTED is not complete
     *         as SPEECH_CONFIG_RESP has not been yet sent (due to
     *         locked buffers). This can not happen in normal conditions,
     *         so record an I/O error (possible CMT reset).
     */
    if (state->priv_state == BC_STATE_CONFIG_DEACT_PEND) {
      SOFT_ASSERT(state->proto_state == CMTSPEECH_STATE_CONNECTED);
      ++state->io_errors;
      TRACE_INFO(DEBUG_PREFIX "Call termination blocked due to pending SPEECH_CONFIG_RESP.");
    }
    /* case 1.b-i): If protocol state CONNECTED, continue to 
     *         deactivate the SSI path immediately. */
    else if (state->proto_state == CMTSPEECH_STATE_CONNECTED) {
      cmtspeech_send_ssi_config_request(context, false);
    }
    /* case 1.b-i): CONNECTING, wait until previous transaction
     *              completes. */
    else if (state->priv_state == BC_STATE_CONNECTING) {
      priv_state_change_to(state, -1, BC_STATE_SSI_CONFIG_PEND);       
    }
    /* notes for other possible states:
     *  case 1.c) DISCONNECTED => nothing to do
     *  case 1.d/e) ACTIVE_DL/DLUL => modem is about to send SPEECH_CONFIG_REQ(0)
     */
  }
  else {
    /* note: server_state == true */
    
    /* case: 2.a-i) Protocol state DISCONNECTED (normal case) */
    if (state->proto_state == CMTSPEECH_STATE_DISCONNECTED) {
      cmtspeech_send_ssi_config_request(context, true);
    }
    /* case: 2.a-ii) Protocol state DISCONNECTING, we have to
     *               wait until previous SSI_CONFIG transaction
     *               is completed before reconnecting */
    else if (state->proto_state == CMTSPEECH_STATE_CONNECTED &&
	     state->priv_state == BC_STATE_DISCONNECTING) {
      priv_state_change_to(state, -1, BC_STATE_SSI_CONFIG_PEND); 
    }
    /* case: 2.b) Protocol state INVALID or TEST_RAMP_PING_ACTIVE, 
     *       continue by performing a reset. */
    else if (state->proto_state == CMTSPEECH_STATE_INVALID ||
	     state->proto_state == CMTSPEECH_STATE_TEST_RAMP_PING_ACTIVE) {
      cmtspeech_state_change_error(context);
      priv_state_change_to(state, -1, BC_STATE_RESET_BEFORE_CONNECT); 
    }
    /* case: 2.c) Protocol state CONNECTED, do nothing. */
    /* case: 2.d) Protocol state ACTIVE* */
    else if (state->proto_state == CMTSPEECH_STATE_ACTIVE_DL ||
	     state->proto_state == CMTSPEECH_STATE_ACTIVE_DLUL) {
      /* case: 2.d-i) Spurious active-to-active call status
       *       change during call, ignoring. */
      if (state->call_server_active == true) {
	TRACE_DEBUG("Spurious call status change during active call, ignoring..");	
      }
      /* case: 2.d-ii) The CMT has not acked the pending call
       *       termination. Either CMT is alive, but an ack will be
       *       coming shortly, or CMT has crashed. Unfortunately we
       *       don't know for sure which is the case. */
      else {
	if (state->io_errors > 0)  {
	  TRACE_ERROR(DEBUG_PREFIX "CMT reset detected, continuing from DISCONNECTED state (prev %s/%d)",
		      priv_state_to_str(state->proto_state), state->priv_state);
	  priv_reset_state_to_disconnected(state);
	  cmtspeech_send_ssi_config_request(context, true);
	}
      }
    }
  }

  state->call_server_active = server_state;

  return res;
}

int cmtspeech_bc_state_change_call_connect(cmtspeech_t *context, bool connect_state)
{
  cmtspeech_bc_state_t *state 
    = cmtspeech_bc_state_object(context);

  state->call_connected = connect_state;

  return 0;
}

/** 
 * Resets state. This function must be call if it is 
 * detected that the peer endpoint has reset its state.
 */ 
void cmtspeech_bc_state_change_reset(cmtspeech_t *context)
{
  cmtspeech_bc_state_t *state 
    = cmtspeech_bc_state_object(context);

  priv_reset_state_to_disconnected(state);
}

bool cmtspeech_is_ssi_connection_enabled(cmtspeech_t *context)
{
  cmtspeech_bc_state_t *state 
    = cmtspeech_bc_state_object(context);

  if (state->proto_state == CMTSPEECH_STATE_CONNECTED ||
      state->proto_state == CMTSPEECH_STATE_ACTIVE_DL ||
      state->proto_state == CMTSPEECH_STATE_ACTIVE_DLUL)
    return true;

  return false;
}

bool cmtspeech_is_active(cmtspeech_t *context)

{
  cmtspeech_bc_state_t *state 
    = cmtspeech_bc_state_object(context);

  if (state->proto_state == CMTSPEECH_STATE_ACTIVE_DL ||
      state->proto_state == CMTSPEECH_STATE_ACTIVE_DLUL)
    return true;

  return false;
}

/* Backend helper functions
 * ------------------------ */

/**
 * Handles a CMT Speech Data control message and creates 
 * a cmtspeech event as result.
 *
 * @param inbuf a four-octet control message
 * @param event a pointer to event structure
 * 
 * @see cmtspeech_bc_post_command()
 *
 * @return 0 on success, -1 on error (unknown message, error parsing, ...)
 */
int cmtspeech_bc_handle_command(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, cmtspeech_cmd_t inbuf, cmtspeech_event_t *event)
{
  int type, channel;
  int res = 0;

  type = cmtspeech_msg_get_type(inbuf);
  channel = cmtspeech_msg_get_domain(inbuf);

  SOFT_ASSERT(event != NULL);

  event->msg_type = type;
  event->prev_state = state->proto_state;

  if (type == CMTSPEECH_SPEECH_CONFIG_REQ) {
    cmtspeech_msg_decode_speech_config_req(inbuf, 
					   &event->msg.speech_config_req.speech_data_stream, 
					   &event->msg.speech_config_req.call_user_connect_ind, 
					   &event->msg.speech_config_req.codec_info, 
					   &event->msg.speech_config_req.cellular_info, 
					   &event->msg.speech_config_req.sample_rate, 
					   &event->msg.speech_config_req.data_format);
    event->msg.speech_config_req.layout_changed = false;

    TRACE_DEBUG(DEBUG_PREFIX "Generating event: SPEECH_CONFIG_REQ (conn %d)", event->msg.speech_config_req.call_user_connect_ind);

    /* state machine assertion */
    STATE_ASSERT((state->proto_state == CMTSPEECH_STATE_CONNECTED) ||
		 (state->proto_state == CMTSPEECH_STATE_ACTIVE_DL) ||
		 (state->proto_state == CMTSPEECH_STATE_ACTIVE_DLUL));

    if (event->msg.speech_config_req.speech_data_stream) 
      priv_state_change_to(state, -1, BC_STATE_CONFIG_ACT_PEND);
    else
      priv_state_change_to(state, -1, BC_STATE_CONFIG_DEACT_PEND);
  }
  else if (type == CMTSPEECH_UPLINK_CONFIG_NTF) {
    TRACE_DEBUG(DEBUG_PREFIX "Generating event: UPLINK_CONFIG_NTF");

    /* state machine assertion */
    STATE_ASSERT(state->proto_state == CMTSPEECH_STATE_ACTIVE_DL);

    if (state->proto_state == CMTSPEECH_STATE_ACTIVE_DL)
      /* state machine transition (TR12) */
      priv_state_change_to(state, CMTSPEECH_STATE_ACTIVE_DLUL, BC_STATE_IN_SYNC);
    
  }
  else if (type == CMTSPEECH_TIMING_CONFIG_NTF) {
    TRACE_DEBUG(DEBUG_PREFIX "Generating event: TIMING_CONFIG_NTF");

    /* XXX: should we have a kernel timestamp? */
    cmtspeech_msg_decode_timing_config_ntf(inbuf, 
					   &event->msg.timing_config_ntf.msec,
					   &event->msg.timing_config_ntf.usec);

    /* state machine assertion */
    STATE_ASSERT(state->proto_state == CMTSPEECH_STATE_ACTIVE_DL ||
		 state->proto_state == CMTSPEECH_STATE_ACTIVE_DLUL);


    /* XXX: support legacy CMT firmwares */
    if (state->proto_state == CMTSPEECH_STATE_ACTIVE_DL) {
      priv_state_change_to(state, CMTSPEECH_STATE_ACTIVE_DLUL, BC_STATE_IN_SYNC);
      TRACE_INFO(DEBUG_PREFIX "XXX detected an old CMT firmware that does not send UPLINK_CONFIG_NTF. Support for old versions will be dropped in later versions.");
    }

    if (state->priv_state == BC_STATE_TIMING)
      priv_state_change_to(state, -1, BC_STATE_IN_SYNC);
    
  }
  else if (type == CMTSPEECH_SSI_CONFIG_RESP) {
    cmtspeech_msg_decode_ssi_config_resp(inbuf, 
					 &event->msg.ssi_config_resp.layout,
					 &event->msg.ssi_config_resp.result);
    event->msg.ssi_config_resp.version = 0; /* deprecated */
    
    TRACE_IO(DEBUG_PREFIX "Generating event: SSI_CONFIG_RESP (layout %u, res %u)", 
	     event->msg.ssi_config_resp.layout,
	     event->msg.ssi_config_resp.result);

    /* state machine assertions */
    /* note: it is possible that we go through DISCONNECTED ->
     *       CONNECTING -> DISCONNECTING -> DISCONNECTED without
     *       being CONNECTED at any point */
    STATE_ASSERT(state->proto_state == CMTSPEECH_STATE_CONNECTED ||
		 state->proto_state == CMTSPEECH_STATE_DISCONNECTED);

    if (event->msg.ssi_config_resp.result == CMTSPEECH_SSI_CONFIG_RES_SUCCESS) {
      if (state->priv_state == BC_STATE_CONNECTING) {
	priv_state_change_to(state, CMTSPEECH_STATE_CONNECTED, BC_STATE_IN_SYNC);
	
	state->sample_layout = event->msg.ssi_config_resp.layout;
	
	/* step: default to swapped configuration */
	if (state->sample_layout == CMTSPEECH_SAMPLE_LAYOUT_NO_PREF)
	  state->sample_layout = CMTSPEECH_SAMPLE_LAYOUT_SWAPPED_LE;
      }
      else if (state->priv_state == BC_STATE_DISCONNECTING) {
	priv_reset_state_to_disconnected(state);

	TRACE_INFO(DEBUG_PREFIX "CMT Speech Data state machine deactivated.");
      }
      else if (state->priv_state == BC_STATE_SSI_CONFIG_PEND) {
	if (state->call_server_active) {
	  cmtspeech_send_ssi_config_request(pcontext, 1);
	  priv_state_change_to(state, CMTSPEECH_STATE_DISCONNECTED, BC_STATE_CONNECTING);
	}
	else {
	  cmtspeech_send_ssi_config_request(pcontext,0);
	  priv_state_change_to(state, CMTSPEECH_STATE_CONNECTED, BC_STATE_DISCONNECTING);
	}
      }

    }
    else {
      TRACE_ERROR(DEBUG_PREFIX "ERROR: SSI_CONFIG_RESP returned an error %d", event->msg.ssi_config_resp.result);
      /* note: do not reset internal state unless it was set
       *       for SSI_CONFIG_REQ */
      if (state->priv_state == BC_STATE_CONNECTING ||
	  state->priv_state == BC_STATE_DISCONNECTING) {
	priv_state_change_to(state, -1, BC_STATE_IN_SYNC);
      }
      /* note: no other way to recover from this case than to
       *       ask for a protocol reset */
      cmtspeech_state_change_error(pcontext);
    }
  }
  else if (type == CMTSPEECH_RESET_CONN_REQ) {
    TRACE_IO(DEBUG_PREFIX "Generating event: CMTSPEECH_EVENT_RESET (CMT initiated)");
    event->msg_type = CMTSPEECH_EVENT_RESET;
    event->msg.reset_done.cmt_sent_req = 1;

    /* state machine assertion */
    STATE_ASSERT(state->proto_state != CMTSPEECH_STATE_DISCONNECTED);

    /* state machine transition, to DISCONNECTED (TR10) */
    priv_reset_state_to_disconnected(state);
  }
  else if (type == CMTSPEECH_RESET_CONN_RESP) {
    int cached_priv_state = state->priv_state;
    TRACE_IO(DEBUG_PREFIX "Generating event: CMTSPEECH_EVENT_RESET (APE initiated)");
    event->msg_type = CMTSPEECH_EVENT_RESET;
    event->msg.reset_done.cmt_sent_req = 0;

    /* state machine assertion */
    STATE_ASSERT(state->proto_state != CMTSPEECH_STATE_DISCONNECTED);

    /* state machine transition, to DISCONNECTED (TR10) */
    priv_reset_state_to_disconnected(state);

    if (cached_priv_state == BC_STATE_RESET_BEFORE_CONNECT) {
      cmtspeech_send_ssi_config_request(pcontext, 1);
    }
  }
  else if (type == CMTSPEECH_TEST_RAMP_PING) {
    /* state machine assertion */
    STATE_ASSERT(state->proto_state == CMTSPEECH_STATE_DISCONNECTED);

    /* note: as we send the response immediately on the data path, we 
     *       do not need to track the state for inbound
     *       TEST_RAMP_PINGs */
  }
  else {
    TRACE_ERROR(DEBUG_PREFIX "ERROR: Unknown protocol message %d", type);
    res = -1;
  }

  /* note: see also cmtspeech_bc_complete_event_processing */

  return res;
}

/**
 * Updates state machine after command 'cmd' has been sent.
 *
 * @see cmtspeech_bc_handle_command()
 * @see cmtspeech_bc_complete_event_processing()
 */
void cmtspeech_bc_post_command(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, cmtspeech_cmd_t cmd)
{
  int type =
    cmtspeech_msg_get_type(cmd);
  int channel =
    cmtspeech_msg_get_domain(cmd);

  if (channel == CMTSPEECH_DOMAIN_CONTROL) {
    if (type == CMTSPEECH_RESET_CONN_REQ) {
      /* note: RESET_CONN_REQ is only sent on error, so our current
       *       state has no significange any more */
      priv_state_change_to(state, CMTSPEECH_STATE_INVALID, BC_STATE_IN_SYNC);
    }
    else if (type == CMTSPEECH_SPEECH_CONFIG_RESP) {
      uint8_t resp;
      cmtspeech_msg_decode_speech_config_resp(cmd, &resp);

      /* state machine assertions */
      SOFT_ASSERT(state->priv_state == BC_STATE_CONFIG_ACT_PEND ||
		  state->priv_state == BC_STATE_CONFIG_DEACT_PEND);

      /* state machine transitions */
      if (resp != 0) {
	/* note: transaction has failed, do not change the state */
	TRACE_ERROR(DEBUG_PREFIX "unable to change %s state due to local error",
		    (state->priv_state == BC_STATE_CONFIG_ACT_PEND) ?
		    "to ACTIVE_DL" : "back to CONNECTED");
	priv_state_change_to(state, -1, BC_STATE_IN_SYNC);
      }
      else {
	if (state->proto_state == CMTSPEECH_STATE_CONNECTED) {
	  /* state machine transition (TR3) */
	  if (state->priv_state == BC_STATE_CONFIG_ACT_PEND) {
	    priv_state_change_to(state, CMTSPEECH_STATE_ACTIVE_DL, BC_STATE_IN_SYNC);
	    cmtspeech_send_timing_request(pcontext);
	  }
	}
	else if (state->proto_state == CMTSPEECH_STATE_ACTIVE_DL ||
		 state->proto_state == CMTSPEECH_STATE_ACTIVE_DLUL) {
	  if (state->priv_state == BC_STATE_CONFIG_ACT_PEND) {
	    if (state->proto_state == CMTSPEECH_STATE_ACTIVE_DLUL) {
	      /* state machine transition (TR11) */
	      priv_state_change_to(state, CMTSPEECH_STATE_ACTIVE_DL, BC_STATE_IN_SYNC);
	    }
	    else {
	      /* state machine transition (TR5) */
	      /* <NOCHANGE> */
	      ;
	    }
	  }
	  else if (state->priv_state == BC_STATE_CONFIG_DEACT_PEND) {
	    /* state machine transition (TR4) */
	    priv_state_change_to(state, CMTSPEECH_STATE_CONNECTED, BC_STATE_IN_SYNC);
	    
	    if (state->call_server_active != true) {
	      TRACE_DEBUG(DEBUX_PREFIX "Call Server already inactive, closing SSI connection.");
	      /* note: send request in cmtspeech_bc_post_command() */
	      cmtspeech_send_ssi_config_request(pcontext, 0);
	    }
	  }
	}
      }
    }
  }
}

/**
 * Completes command processing and fills remaining fields of 
 * the event object.
 *
 * @see cmtspeech_bc_handle_command()
 * @see cmtspeech_bc_post_command()
 */
void cmtspeech_bc_complete_event_processing(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, cmtspeech_event_t *event)
{
  event->state = state->proto_state;
}

int cmtspeech_bc_write_command(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, cmtspeech_cmd_t msg, int fd)
{
  int res =
    write(fd, msg.d.buf, sizeof(msg));

  TRACE_IO(DEBUG_PREFIX "wrote %s (%02X:%02X:%02X:%02X), fd %d, res %d.", 
	   cmtspeech_msg_type_to_string(msg), 
	   msg.d.buf[0], msg.d.buf[1], msg.d.buf[2], msg.d.buf[3], 
	   fd, res);
  if (res == sizeof(msg)) {
    int channel =
      cmtspeech_msg_get_domain(msg);
    if (channel != CMTSPEECH_DOMAIN_INTERNAL)
      cmtspeech_bc_post_command(state, pcontext, msg);
    state->io_errors = 0;
  }
  else {
    TRACE_ERROR(DEBUG_PREFIX "ERROR: sending cmd %s failed, res %d",
		cmtspeech_msg_type_to_string(msg), res);
    ++state->io_errors;
  }
  return res;
}

int cmtspeech_bc_test_data_ramp_req(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, int fd, uint8_t channel, uint8_t replychannel, uint8_t rampstart, uint8_t ramplen)
{
  int res;
  cmtspeech_cmd_t msg;

  /* state machine assertion */
  STATE_ASSERT(state->proto_state == CMTSPEECH_STATE_DISCONNECTED);

  if (state->proto_state != CMTSPEECH_STATE_DISCONNECTED) {
    TRACE_ERROR(DEBUG_PREFIX "ERROR: call ongoing, cannot send TEST_RAMP_PING!");
    return -1;
  }

  res = cmtspeech_msg_encode_test_ramp_ping(&msg, channel, replychannel, rampstart, ramplen);
  if (res == CMTSPEECH_CTRL_LEN) 
    res = cmtspeech_bc_write_command(state, pcontext, msg, fd);
  
  if (res != CMTSPEECH_CTRL_LEN)
    return -1;

  /* state machine transition (TR12) */
  priv_state_change_to(state, CMTSPEECH_STATE_TEST_RAMP_PING_ACTIVE, BC_STATE_IN_SYNC);

  return 0;
}

int cmtspeech_bc_test_sequence_received(cmtspeech_bc_state_t *state)
{
  /* state machine assertion */
  STATE_ASSERT(state->proto_state == CMTSPEECH_STATE_TEST_RAMP_PING_ACTIVE);

  /* state machine transition (TRE1) */
  priv_state_change_to(state, CMTSPEECH_STATE_DISCONNECTED, BC_STATE_IN_SYNC);

  return 0;
}

int cmtspeech_bc_send_timing_request(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, int fd)
{
  int res;
  cmtspeech_cmd_t msg;

  res = cmtspeech_msg_encode_new_timing_config_req(&msg);
  if (res == CMTSPEECH_CTRL_LEN) 
    res = cmtspeech_bc_write_command(state, pcontext, msg, fd);
  
  if (res != CMTSPEECH_CTRL_LEN)
    return -1;

  priv_state_change_to(state, -1, BC_STATE_TIMING);

  return 0;
}

int cmtspeech_bc_send_ssi_config_request(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, int fd, bool state_arg)
{
  cmtspeech_cmd_t msg;
  uint8_t layout = CMTSPEECH_SAMPLE_LAYOUT_INORDER_LE;
  int res = 
    cmtspeech_msg_encode_ssi_config_req(&msg, 
					layout,
					state->conf_proto_version,
					state_arg == true ? 1 : 0);

  TRACE_DEBUG(DEBUG_PREFIX "Trying to send SSI_CONFIG_REQ with layout %d, version %d and status %d.", 
	      layout,
	      state->conf_proto_version,
	      state_arg == true ? 1 : 0);

  if (state_arg == false) {

    /* state machine assertion */
    STATE_ASSERT(state->proto_state == CMTSPEECH_STATE_CONNECTED ||
		 state->priv_state == BC_STATE_SSI_CONFIG_PEND);

    if (state->priv_state == BC_STATE_DISCONNECTING) {
      TRACE_ERROR(DEBUG_PREFIX "ERROR: SSI_CONFIG_REQ(state_arg=0) already pending!");
      return -1;
    }

    priv_state_change_to(state, -1, BC_STATE_DISCONNECTING);
  }
  else {

    STATE_ASSERT(state->proto_state == CMTSPEECH_STATE_DISCONNECTED ||
		 state->priv_state == BC_STATE_SSI_CONFIG_PEND);

    TRACE_INFO(DEBUG_PREFIX "CMT Speech Data state machine activated with SSI_CONFIG_REQ.");
    priv_state_change_to(state, -1, BC_STATE_CONNECTING);
  }

  if (res == CMTSPEECH_CTRL_LEN) {
    res = cmtspeech_bc_write_command(state, pcontext, msg, fd);
    TRACE_IO(DEBUG_PREFIX "Sent SSI_CONFIG_REQ with layout %d, version %d and status %d.", 
	     layout,
	     state->conf_proto_version,
	     state_arg == true ? 1 : 0);
  }

  if (res != CMTSPEECH_CTRL_LEN) {
    priv_state_change_to(state, -1, BC_STATE_IN_SYNC);
    return -1;
  }

  return 0;
}


const char* cmtspeech_version_str(void)
{
  return VERSION;
}

int cmtspeech_protocol_version(void)
{
  /* note: this is the highest protocol version implemented,
   *       older versions might */
  return LIBCMTSPEECHDATA_PROTOCOL_VERSION;
}
