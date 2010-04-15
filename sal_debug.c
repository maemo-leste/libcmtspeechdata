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
/** @file sal_debug.c
 *
 * Debugging and tracing macros for libcmtspeechdata
 * internal usage.
 *
 * Acronym SAL refers to the old name of the library
 * (SSI audio library).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "sal_debug.h"
#include "cmtspeech.h"

/* set default values */
int cmtspeech_trace_mask = TRACE_BIT_ERROR | TRACE_BIT_INFO;

cmtspeech_trace_handler_t cmtspeech_glob_trace_f = NULL;

int cmtspeech_initialize_tracing(void)
{
  const char *debstr = getenv("CMTSPEECHDEBUG");
  if (debstr) {

    if (strstr(debstr, "noinfo") != NULL)
      cmtspeech_trace_mask &= ~TRACE_BIT_INFO;
    else if (strstr(debstr, "info") != NULL)
      cmtspeech_trace_mask |= TRACE_BIT_INFO;

#if !defined(NDEBUG)
    if (strstr(debstr, "notrace") != NULL) {
      cmtspeech_trace_mask &= ~TRACE_BIT_STATE_CHANGE;
      cmtspeech_trace_mask &= ~TRACE_BIT_IO;
    }
    else if (strstr(debstr, "trace") != NULL) {
      cmtspeech_trace_mask |= TRACE_BIT_STATE_CHANGE;
      cmtspeech_trace_mask |= TRACE_BIT_IO;
    }

    if (strstr(debstr, "nodebug") != NULL)
      cmtspeech_trace_mask &= ~TRACE_BIT_DEBUG;
    else if (strstr(debstr, "debug") != NULL)
      cmtspeech_trace_mask |= TRACE_BIT_DEBUG;

    if (strstr(debstr, "notoken") != NULL)
      cmtspeech_trace_mask &= ~TRACE_BIT_TOKEN;
    else if (strstr(debstr, "token") != NULL)
      cmtspeech_trace_mask |= TRACE_BIT_TOKEN;

#endif /* !defined(NDEBUG) */

  }

  return 0;
} 

int cmtspeech_soft_assert(int v, const char* v_str, int line, const char *file)
{
#if !defined(NDEBUG)
  if (!v) {
    cmtspeech_trace_message(CMTSPEECH_TRACE_INFO, "ASSERT FAILED: %s:%d '%s'", file, line, v_str);
  }
#endif
  return v;
}

void cmtspeech_trace_message(int priority, const char *message, ...)
{
  va_list args;

  if (!((1 << priority) & cmtspeech_trace_mask))
    return;

  va_start(args, message);

  if (cmtspeech_glob_trace_f != NULL) {
    cmtspeech_glob_trace_f(priority, message, args);
  }
  else {
    if (priority == CMTSPEECH_TRACE_ERROR)
      printf("ERROR: ");
    printf("CMTSPEECH: ");
    vprintf(message, args);
    printf("\n");
  }

  va_end(args);
}

void cmtspeech_trace_toggle(int priority, bool enabled)
{
  if (enabled == true) 
    cmtspeech_trace_mask |= 1 << priority;
  else
    cmtspeech_trace_mask &= ~(1 << priority);
}

int cmtspeech_set_trace_handler(cmtspeech_trace_handler_t func)
{
  cmtspeech_glob_trace_f = func;
  return 1;
}
