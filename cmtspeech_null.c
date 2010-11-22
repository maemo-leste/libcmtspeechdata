/*
 * This file is part of libcmtspeechdata.
 *
 * Copyright (C) 2010 Nokia Corporation.
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

/** @file cmtspeech_null.c
 *
 * A libcmtspeechdata backend implementation that demonstrates
 * how to add a new modem backend to the build scripts. This
 * 'null' backend does not do anything (it can be built, but
 * has not functionality).
 *
 * See 'dummy-backend/' for a backend that simulates a modem.
 */

#include <errno.h>

#include "cmtspeech.h"

/* Interfaces: Core I/O
 * -------------------- */

void cmtspeech_init(void)
{
}

cmtspeech_t* cmtspeech_open(void)
{
  return NULL;
}

int cmtspeech_close(cmtspeech_t *context)
{
  return -1;
}

int cmtspeech_descriptor(cmtspeech_t *context)
{
  return -1;
}

int cmtspeech_check_pending(cmtspeech_t *context, int *flags)
{
  return -1;
}

int cmtspeech_read_event(cmtspeech_t *context, cmtspeech_event_t *event)
{
  return -1;
}

/* Interfaces: Event parsing
 * ------------------------- */

int cmtspeech_event_to_state_transition(const cmtspeech_t *context, const cmtspeech_event_t *event)
{
  return CMTSPEECH_TR_INVALID;
}

/* Interfaces: Runtime configuration
 * --------------------------------- */

int cmtspeech_set_wb_preference(cmtspeech_t *context, bool enabled)
{
  return -1;
}

/* Interfaces: State management
 * ---------------------------- */

int cmtspeech_protocol_state(cmtspeech_t* context)
{
  return CMTSPEECH_STATE_INVALID;
}

bool cmtspeech_is_ssi_connection_enabled(cmtspeech_t *context)
{
  return false;
}

bool cmtspeech_is_active(cmtspeech_t *context)
{
  return false;
}

int cmtspeech_state_change_call_status(cmtspeech_t *context, bool state)
{
  return false;
}

int cmtspeech_state_change_call_connect(cmtspeech_t *context, bool state)
{
  return false;
}

int cmtspeech_state_change_error(cmtspeech_t *context)
{
  return false;
}

/* Interfaces: Buffer management
 * -------------------------------------- */

int cmtspeech_ul_buffer_acquire(cmtspeech_t *context, cmtspeech_buffer_t **buf)
{
  return -EINVAL;
}

int cmtspeech_ul_buffer_release(cmtspeech_t *context, cmtspeech_buffer_t *buf)
{
  return -EINVAL;
}

int cmtspeech_dl_buffer_acquire(cmtspeech_t *context, cmtspeech_buffer_t **buf)
{
  return -EINVAL;
}

int cmtspeech_dl_buffer_release(cmtspeech_t *context, cmtspeech_buffer_t *buf)
{
  return -EINVAL;
}

cmtspeech_buffer_t *cmtspeech_dl_buffer_find_with_data(cmtspeech_t *context, uint8_t *data)
{
  return NULL;
}

cmtspeech_buffer_t *cmtspeech_dl_buffer_find_with_payload(cmtspeech_t *context, uint8_t *payload)
{
  return NULL;
}

/* Interfaces: Implementation version information
 * ----------------------------------------------------  */

const char* cmtspeech_version_str(void)
{
  return VERSION;
}

int cmtspeech_protocol_version(void)
{
  return LIBCMTSPEECHDATA_PROTOCOL_VERSION;
}

/* Interfaces: Access to backend specific functionality
 * ----------------------------------------------------  */

const char* cmtspeech_backend_name(cmtspeech_t* context)
{
  return "null";
}

int cmtspeech_backend_message(cmtspeech_t *self, int type, int args, ...)
{
  return -1;
}

/* Interfaces: Low level message handling
 * -------------------------------------- */

int cmtspeech_send_timing_request(cmtspeech_t *context)
{
  return -1;
}

int cmtspeech_send_ssi_config_request(cmtspeech_t *context, bool active)
{
  return -1;
}

/* Interfaces: Test interface
 * -------------------------- */

int cmtspeech_test_data_ramp_req(cmtspeech_t *context, uint8_t rampstart, uint8_t ramplen)
{
  return -1;
}
