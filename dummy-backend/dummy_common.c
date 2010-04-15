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

/** @file dummy_common.c
 *
 * Common functions for CMT emulation code.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <arpa/inet.h>

#include "cmtspeech_msgs.h"
#include "dummy_common.h"

#include "sal_debug.h"
#define DEBUG_PREFIX "dummy_common: "

/* #define BIG_ENDIAN_SAMPLES 1  */
#define LITTLE_ENDIAN_SAMPLES 1 

/**
 * Returns result of 'a - b'. Only accurate when the difference
 * within one second.
 */
static long rough_diff_ns(struct timespec *a, struct timespec *b)
{
  if (a->tv_sec == b->tv_sec)
    return a->tv_nsec - b->tv_nsec;
  else if (a->tv_sec == b->tv_sec + 1 &&
	   a->tv_nsec - b->tv_nsec < 0)
    return 999999999 + a->tv_nsec - b->tv_nsec;
  else if (a->tv_sec > b->tv_sec)
    return 999999999;
  else 
    return -999999999;
}

int dummy_tone_fill_buffer(dummy_tone_t *tone, uint8_t *buf, int bufsize)
{
  int i;

 /* note: i is index to 16bit frames in the buffer */
  for(i = 0; i < bufsize / 2; i++) {
    /* reverse byte order if needed (array LE) */
    int16_t value = 
#if BIG_ENDIAN_SAMPLES
      htons(tone->tone_buf[tone->tone_pos])
#else
      tone->tone_buf[tone->tone_pos]
#endif
      ;
    
    memcpy(&buf[i * 2], &value, sizeof(value));

    tone->tone_pos = (tone->tone_pos + 1) % (tone->tone_len / 2);
  }

  return 0;
}

int dummy_tone_fill_buffer_slot(dummy_tone_t *tone, dummy_buffer_t *slot, uint16_t counter)
{
  int i = 0, j;

  j = cmtspeech_msg_encode_dl_data_header(slot->buf.data, CMTSPEECH_DATA_HEADER_LEN, counter, CMTSPEECH_SPC_FLAGS_SPEECH, CMTSPEECH_DATA_LENGTH_20MS, CMTSPEECH_SAMPLE_RATE_16KHZ, CMTSPEECH_DATA_TYPE_VALID);
  assert(j == CMTSPEECH_DATA_HEADER_LEN);

  /* note: i is index to 16bit frames in the buffer */
  for(i = 2; i < slot->buf.size / 2; i++) {
    /* reverse byte order if needed (array LE) */
    int16_t value = 
#if BIG_ENDIAN_SAMPLES
      htons(tone->tone_buf[tone->tone_pos])
#else
      tone->tone_buf[tone->tone_pos]
#endif
      ;
    memcpy(&slot->buf.data[i * 2], &value, sizeof(value));
    tone->tone_pos = (tone->tone_pos + 1) % (tone->tone_len / 2);
  }
  slot->buf.count = slot->buf.size;
  slot->buf.pcount = slot->buf.size - CMTSPEECH_DATA_HEADER_LEN;

  return 0;
}

dummy_tone_t *dummy_tone_initialize(const char *typestr)
{
  int len;
  dummy_tone_t *tone = NULL;
  char *getenv_dummysrc = getenv("DUMMYSRC");
  const char *dummysrc = NULL;

  if (getenv_dummysrc)
    dummysrc = getenv_dummysrc;

  if (!dummysrc)
    dummysrc = typestr;

  if (!dummysrc)
    return tone;

  if (strcmp(dummysrc, "sine") == 0) {
    /*
     * 400 Hz sin wave as signed 16-bit array
     */
    static const int16_t sinwave[20] = {
      0, 2531, 4814, 6626, 7790, 8191, 7790, 6626, 4814, 2531, 
      0, -2531, -4814, -6626, -7790, -8191, -7790, -6626, -4814, -2531
    };

    tone = malloc(sizeof(*tone));

    tone->tone_len = sizeof(int16_t) * 20;
    tone->tone_buf = malloc(tone->tone_len);
    tone->tone_pos = 0;
    if (tone->tone_buf) {
      memcpy(tone->tone_buf, sinwave, tone->tone_len);
      ONTRACE(printf(DEBUG_PREFIX "tone buf created of length %d, sine tone\n", tone->tone_len));
    }
    else {
      free(tone);
      tone = NULL;
    }
  }
  else {
    FILE *f = fopen(dummysrc, "rw");
    int res;

    if (f) {
      res = fseek(f, 0, SEEK_END);
      len = ftell(f);
      res = fseek(f, 0, SEEK_SET);
      if (res == 0 && len > 0) {
	ssize_t read_bytes;

	tone = malloc(sizeof(*tone));

	tone->tone_len = len;
	tone->tone_buf = malloc(len);
	tone->tone_pos = 0;
	if (tone->tone_buf) {
	  read_bytes = fread(tone->tone_buf, 1, tone->tone_len, f);
	  if (len != read_bytes) {
	    free(tone);
	    tone = NULL;
	  }
	  ONTRACE(printf(DEBUG_PREFIX "tone buf created of length %d, file %s, len %d, res %d.\n", tone->tone_len, dummysrc, len, res));
	}
	else {
	  free(tone);
	  tone = NULL;
	}
      }
      fclose(f);
    }
    else {
      ONINFO(printf(DEBUG_PREFIX "unable to open tone file %s\n", dummysrc));
      free(tone);
      tone = NULL;
    }
  }

  return tone;
}

void dummy_tone_release(dummy_tone_t *tone)
{
  free(tone->tone_buf);
  tone->tone_buf = NULL;
  tone->tone_len = 0;
  tone->tone_pos = 0;
  free(tone);
}

void dummy_poll_timer_init(dummy_poll_timer_t *self, int period_ms)
{
  self->has_elapsed = 0;
  self->baseperiod_ns = period_ms * 1000000L;
  memset(&self->last_elapsed, 0, sizeof(self->last_elapsed));
}

/**
 * Calculates poll time out for timer 'self'.
 *
 * @return timeout in milliseconds
 */ 
int dummy_poll_timer_pre_poll(dummy_poll_timer_t *self)
{
  int timeout;
  clock_gettime(CLOCK_MONOTONIC, &self->prepoll);
  if (self->has_elapsed == 1) {
    long cycle = rough_diff_ns(&self->prepoll, &self->last_elapsed);
    if (cycle >= self->baseperiod_ns)
      timeout = 0;
    else
      timeout = (self->baseperiod_ns - cycle) / 1000000L + 1;
  }
  else
    timeout = self->baseperiod_ns / 1000000L;

  /* fprintf(stderr, "pre_poll timeout %d\n", timeout);     */

  return timeout;
}

/**
 * Checks whether event deadline has been reached.
 *
 * @see dummy_poll_timer_event_sent()
 * @see dummy_poll_timer_event_sent_fixed_period()
 * 
 * @return true or false
 */ 
bool dummy_poll_timer_is_elapsed(dummy_poll_timer_t *self, long margin_ns)
{
  struct timespec now;
  long cycle;
  clock_gettime(CLOCK_MONOTONIC, &now);
  cycle = rough_diff_ns(&now, &self->last_elapsed);

  /* fprintf(stderr, "cycle %lu, res %d\n", cycle, cycle >= self->baseperiod_ns); */

  if (cycle + margin_ns >= self->baseperiod_ns)
    return true;

  return false;
}

/**
 * Sets the time, when timer last elapsed, to current system time.
 * This is used to calculate when the timer should fire next.
 *
 * @see dummy_oll_timer_is_elapsed()
 * @see dummy_poll_timer_event_sent_fixed_period()
 */
void dummy_poll_timer_elapsed(dummy_poll_timer_t *self)
{
  clock_gettime(CLOCK_MONOTONIC, &self->last_elapsed);
  if (self->has_elapsed == 0)
    self->has_elapsed = 1;
}

/**
 * Sets the time, when timer last elapsed, by adding
 * timer base period to previous recorded timer
 * timestamp.
 *
 * @see dummy_oll_timer_is_elapsed()
 * @see dummy_poll_timer_event_sent()
 */
void dummy_poll_timer_elapsed_fixed_period(dummy_poll_timer_t *self)
{
  if (self->has_elapsed == 0) {
    self->has_elapsed = 1;
    clock_gettime(CLOCK_MONOTONIC, &self->last_elapsed);
  }
  else {
    self->last_elapsed.tv_nsec +=  self->baseperiod_ns;
    if (self->last_elapsed.tv_nsec > 999999999) {
      ++self->last_elapsed.tv_sec;
      self->last_elapsed.tv_nsec -= 1000000000;
    }
  }
}
