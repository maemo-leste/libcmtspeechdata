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

/** @file sal_debug.h
 *
 * Debugging and tracing macros for libcmtspeechdata
 * internal usage. Applications using libcmtspeechdata
 * should NOT include this file.
 *
 * Acronym SAL refers to the old name of the library
 * (SSI audio library).
 */

#ifndef INCLUDED_SAL_DEBUG_H
#define INCLUDED_SAL_DEBUG_H

#include "cmtspeech.h" 

/* Define which traces are compiled in */

#ifndef BUILD_WITH_INFO
#  define BUILD_WITH_INFO 1
#endif

#ifndef BUILD_WITH_TRACE
#  define BUILD_WITH_TRACE 1
#endif

#ifndef BUILD_WITH_DEBUG
#  define BUILD_WITH_DEBUG 0
#endif

#ifndef BUILD_WITH_DEBUG_RING
#  define BUILD_WITH_DEBUG_RING 0
#endif

#ifndef BUILD_WITH_DEBUG_TOKENS
#  define BUILD_WITH_DEBUG_TOKENS 0
#endif

/* note: trace bits derived from public tracing levels */
#define TRACE_BIT_ERROR         (1 << CMTSPEECH_TRACE_ERROR)
#define TRACE_BIT_INFO          (1 << CMTSPEECH_TRACE_INFO)
#define TRACE_BIT_STATE_CHANGE  (1 << CMTSPEECH_TRACE_STATE_CHANGE)
#define TRACE_BIT_IO            (1 << CMTSPEECH_TRACE_IO)
#define TRACE_BIT_DEBUG         (1 << CMTSPEECH_TRACE_DEBUG)

/* note: trace bits that are implementation internal */
#define INTERNAL_TRACE_TOKEN    CMTSPEECH_TRACE_INTERNAL
#define TRACE_BIT_TOKEN         (1 << INTERNAL_TRACE_TOKEN)

extern int cmtspeech_trace_mask;

/* Traces used by test applications */
/* ---------------------------------*/

#define TRACE_ERROR(x, ...) cmtspeech_trace_message(CMTSPEECH_TRACE_ERROR, x, ##__VA_ARGS__)

#if BUILD_WITH_INFO
#define ONINFO(x) do { if (cmtspeech_trace_mask & TRACE_BIT_INFO) { x; } } while(0)
#define TRACE_INFO(x, ...) cmtspeech_trace_message(CMTSPEECH_TRACE_INFO, x, ##__VA_ARGS__)
#else
#define ONINFO(x) 
#define TRACE_INFO(x, ...) 
#endif

#if BUILD_WITH_TRACE && !defined(NDEBUG)
#define ONTRACE(x) do { if (cmtspeech_trace_mask & (TRACE_BIT_STATE_CHANGE|CMTSPEECH_TRACE_IO)) { x; } } while(0)
#define TRACE_STATE_CHANGE(x, ...) cmtspeech_trace_message(CMTSPEECH_TRACE_STATE_CHANGE, x, ##__VA_ARGS__)
#define TRACE_IO(x, ...) cmtspeech_trace_message(CMTSPEECH_TRACE_IO, x, ##__VA_ARGS__)
#else
#define ONTRACE(x) 
#define TRACE_STATE_CHANGE(x, ...) 
#define TRACE_IO(x, ...) 
#endif

#if BUILD_WITH_DEBUG && !defined(NDEBUG)
#define ONDEBUG(x) do { if (cmtspeech_trace_mask & TRACE_BIT_DEBUG) { x; } } while(0)
#define TRACE_DEBUG(x, ...) cmtspeech_trace_message(CMTSPEECH_TRACE_DEBUG, x, ##__VA_ARGS__)
#else
#define ONDEBUG(x) 
#define TRACE_DEBUG(x, ...) 
#endif

#if BUILD_WITH_DEBUG_TOKENS
#define ONDEBUG_TOKENS(x) do { if (cmtspeech_trace_mask & TRACE_BIT_TOKEN) { x; } } while(0)
#else
#define ONDEBUG_TOKENS(x) 
#endif

#if !defined(NDEBUG)
#define SOFT_ASSERT(v) cmtspeech_soft_assert(v, #v, __LINE__, __FILE__)
#else
#define SOFT_ASSERT(v)
#endif

void cmtspeech_trace_message(int priority, const char *message, ...);
void cmtspeech_trace_toggle(int priority, bool enabled);
int cmtspeech_initialize_tracing(void);
int cmtspeech_soft_assert(int v, const char* v_str, int line, const char *file);

#endif /* INCLUDED_SAL_DEBUG_H */
