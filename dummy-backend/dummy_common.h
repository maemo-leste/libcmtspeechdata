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

/** @file dummy_common.h
 *
 * Common functions for CMT emulation code.
 */

#ifndef INCLUDED_DUMMY_COMMON_H
#define INCLUDED_DUMMY_COMMON_H

#include <stdint.h>
#include <time.h>

#include "cmtspeech.h"

struct dummy_buffer_s {
  cmtspeech_buffer_t buf;
  int locked;
};
typedef struct dummy_buffer_s dummy_buffer_t;

struct dummy_tone_s {
  int16_t *tone_buf;
  int tone_len;
  int tone_pos;
};
typedef struct dummy_tone_s dummy_tone_t;

struct dummy_poll_timer_s {
  int has_elapsed;
  struct timespec prepoll;
  struct timespec last_elapsed;
  long baseperiod_ns;
};
typedef struct dummy_poll_timer_s dummy_poll_timer_t;

int dummy_tone_fill_buffer(dummy_tone_t *tone, uint8_t *buf, int bufsize);
int dummy_tone_fill_buffer_slot(dummy_tone_t *tone, dummy_buffer_t *slot, uint16_t counter);
dummy_tone_t *dummy_tone_initialize(const char *typestr);
void dummy_tone_release(dummy_tone_t *tone);

void dummy_poll_timer_init(dummy_poll_timer_t *self, int period_ms);
int dummy_poll_timer_pre_poll(dummy_poll_timer_t *self);
void dummy_poll_timer_elapsed(dummy_poll_timer_t *self);
void dummy_poll_timer_elapsed_fixed_period(dummy_poll_timer_t *self);
bool dummy_poll_timer_is_elapsed(dummy_poll_timer_t *self, long margin_ns);

int dummy_tone_calc_timeout(int base_cycle_ms, struct timespec *prev);

#endif /* INCLUDED_DUMMY_COMMON_H */
