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

/** @file sal_ring.h
 *
 * Ring buffer implementation for libcmtspeechdata.
 *
 * The implementation is a simplified ring buffer with
 * no fill count tracking, but instead one octet is kept
 * open in the buffer (maximum fill is thus 'ring->size - 1').
 *
 * Ring buffer size is not limited to power of two sizes.
 */

#ifndef INCLUDED_SAL_RING_H
#define INCLUDED_SAL_RING_H

#include <stdint.h>

struct ring_buffer_s {
  int read_idx;
  int write_idx;
  int size;         /**< octets of allocated space */
  uint8_t *data;    /**< pointer to a buffer of 'size' octets */
};

typedef struct ring_buffer_s ring_buffer_t;

/**
 * Initializes the ring buffer for use.
 *
 * @param ring self
 * @param buf pointer to buffer data area
 * @param size size of the data area in octets
 */
static inline void ring_buffer_init(ring_buffer_t *ring, uint8_t *buf, int size)
{
  ring->read_idx = 0;
  ring->write_idx = 0;
  ring->size = size;
  ring->data = buf;
}

/**
 * Resets the ring buffer state.
 *
 * @param ring self
 */
static inline void ring_buffer_reset(ring_buffer_t *ring)
{
  ring->read_idx = 0;
  ring->write_idx = 0;
}

/**
 * Returns number of octets that can be read (between 0 and
 * ring->size-1).
 */
static inline int ring_buffer_avail_for_read(ring_buffer_t *ring)
{
  return (ring->write_idx - ring->read_idx + ring->size) % ring->size;
}

/**
 * Returns number of octets that can be read from a continuous
 * buffer segment (between 0 and ring->size-1).
 */
static inline int ring_buffer_cavail_for_read(ring_buffer_t *ring)
{
  if (ring->write_idx >= ring->read_idx)
    return ring->write_idx - ring->read_idx;
  else
    return ring->size - ring->read_idx;
}

/**
 * Returns number of octets that can be written (between 0
 * and ring->size-1).
 */
static inline int ring_buffer_avail_for_write(ring_buffer_t *ring)
{
  if (ring->read_idx == ring->write_idx)
    return ring->size - 1;
  else
    return (ring->read_idx - ring->write_idx - 1 + ring->size) % ring->size;
}

/**
 * Returns number of octets that can be written to
 * a continuous buffer segment (between 0 and ring->size-1).
 */
static inline int ring_buffer_cavail_for_write(ring_buffer_t *ring)
{
  if (ring->read_idx > ring->write_idx)
    return ring_buffer_avail_for_write(ring);
  else if (ring->read_idx == 0)
    return ring->size - ring->write_idx -1;
  else
    return ring->size - ring->write_idx;
}

/**
 * Moves read pointer ahead for 'n' octets. Does not care about
 * possible overrun.
 */
static inline void ring_buffer_move_read(ring_buffer_t *ring, int n)
{
  ring->read_idx = (ring->read_idx + n) % ring->size;
}

/**
 * Moves write pointer ahead for 'n' octets. Does not care about
 * possible overrun.
 */
static inline void ring_buffer_move_write(ring_buffer_t *ring, int n)
{
  ring->write_idx = (ring->write_idx + n) % ring->size;
}

#endif /* INCLUDED_SAL_RING_H */
