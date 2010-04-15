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
/** @file cmtspeech_msgs.h
 *
 * Helper functions for backend implementations of
 * CMT Speech Data library.
 *
 * Copyright (C) 2008,2009,2010 Nokia Corporation
 * Contact: Kai Vehmanen
 *
 */

#ifndef INCLUDED_CMTSPEECH_BACKEND_COMMON_H
#define INCLUDED_CMTSPEECH_BACKEND_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#if !defined(LIBCMTSPEECHDATA_PROTOCOL_VERSION) || \
    LIBCMTSPEECHDATA_PROTOCOL_VERSION != 2
#error "LIBCMTSPEECHDATA_PROTOCOL_VERSION not defined or does not match implementation."
#endif

#include "cmtspeech_msgs.h"

enum cmtspeech_bc_state {
  BC_STATE_IN_SYNC = 0,        /**< state describes 'proto_state' */
  BC_STATE_TIMING,             /**< a NEW_TIMING_CONFIG_REQ is
				    currently in progress */
  BC_STATE_RESET_BEFORE_CONNECT,
  BC_STATE_CONNECTING,         /**< SSI_CONFIG_REQ(1) has been sent */
  BC_STATE_DISCONNECTING,      /**< SSI_CONFIG_REQ(0) has been sent */
  BC_STATE_SSI_CONFIG_PEND,    /**< waiting for previous SSI_CONFIG_REQ */
  BC_STATE_CONFIG_ACT_PEND,    /**< SPEECH_CONFIG_REQ(1) currently
				    processed, reply not sent */
  BC_STATE_CONFIG_DEACT_PEND   /**< SPEECH_CONFIG_REQ(0) currently
				    processed, reply not sent */
};

struct cmtspeech_bc_state_s {
  bool call_server_active;         /**< call signaling: whether Call Server
				      is active or not */
  bool call_connected;             /**< call signaling whether call
				      has been connected */
  int proto_state;                 /**< PROTO_STATE_* */
  int priv_state;                  /**< private state, extension to 'proto_state' */
  int sample_layout;
  int io_errors;                   /**< counter of fatal i/o errors */
  int conf_proto_version;          /**< which protocol version to use */
};
typedef struct cmtspeech_bc_state_s cmtspeech_bc_state_t;

/* Backend helper functions
 * ------------------------ */

int cmtspeech_bc_open(cmtspeech_bc_state_t *state);
int cmtspeech_bc_handle_command(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, cmtspeech_cmd_t inbuf, cmtspeech_event_t *event);
void cmtspeech_bc_post_command(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, cmtspeech_cmd_t resp);
void cmtspeech_bc_complete_event_processing(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, cmtspeech_event_t *event);
int cmtspeech_bc_write_command(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, cmtspeech_cmd_t msg, int fd);
int cmtspeech_bc_send_timing_request(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, int fd);
int cmtspeech_bc_send_ssi_config_request(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, int fd, bool state_arg);
int cmtspeech_bc_test_data_ramp_req(cmtspeech_bc_state_t *state, cmtspeech_t *pcontext, int fd, uint8_t channel, uint8_t replychannel, uint8_t rampstart, uint8_t ramplen);
int cmtspeech_bc_test_sequence_received(cmtspeech_bc_state_t *state);
int cmtspeech_bc_state_change_call_connect(cmtspeech_t *context, bool connect_state);
int cmtspeech_bc_state_change_call_status(cmtspeech_t *context, bool server_state);
void cmtspeech_bc_state_change_reset(cmtspeech_t *context);

/* Stub functions that need to be implemented by all backends
 * ---------------------------------------------------------- */

cmtspeech_bc_state_t *cmtspeech_bc_state_object(cmtspeech_t *context);

#endif /* INCLUDED_CMTSPEECH_BACKEND_COMMON_H */
