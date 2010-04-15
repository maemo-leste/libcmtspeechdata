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

/** @file cmtspeech_dummy.h
 *
 * Dummy user-space implementation of the CMT Speech Data
 * Backend interface.
 */

#ifndef INCLUDED_CMTSPEECH_DUMMY_H
#define INCLUDED_CMTSPEECH_DUMMY_H

#include <stdbool.h>

#define CMTSPEECH_DUMMY_BACKEND_ID "cmtspeech_dummy"

/**
 * General list of TODO items:
 *
 * - separate public dummy control interface from the
 *   internal context struct (to have a better interface
 *   for simple_csmediad and others)
 */


struct cmtspeech_dummy_s;
typedef struct cmtspeech_dummy_s cmtspeech_dummy_t;

#define CMTSPEECH_DUMMY_EVENT_AUDIO_CONNECT 0
#define CMTSPEECH_DUMMY_EVENT_SERVER_STATUS 1

#endif
