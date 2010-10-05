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

/** @file send_server_status.c
 *
 * Tool that opens a libcmtspeechdata instance, sends a protocol
 * reset, and closes the instance.
 */

/**
 * General list of TODO items:
 *  - <none>
 */

#include <stdio.h>
#include <poll.h>

#include <cmtspeech.h>

int main(int argc, char *argv[]) {
  struct pollfd fds[1];
  cmtspeech_t *cmtspeech;
  int res = 0;
  
  cmtspeech_init();

  cmtspeech = cmtspeech_open();
  if (!cmtspeech) {
    fprintf(stderr, "ERROR: unable to open libcmtspeechdata instance\n");
    return -1;
  }

  cmtspeech_state_change_error(cmtspeech);

  fds[0].fd = cmtspeech_descriptor(cmtspeech);
  if (fds[0].fd >= 0) {
    int tries;
    int pollres = 0;
    for(tries = 0; tries < 3; tries++) {
      pollres =
	poll(fds, 1, 1);
      printf("poll for driver reset returned %d.\n", pollres);
      if (pollres == 1) {
	int flags = 0, res =
	  cmtspeech_check_pending(cmtspeech, &flags);      
	if (res > 0) {
	  cmtspeech_event_t event;
	  cmtspeech_read_event(cmtspeech, &event);
	  if (event.msg_type == CMTSPEECH_EVENT_RESET) {
	    printf("driver ack'ed the reset, exiting.\n");
	    break;
	  }
	}
      }
      else if (pollres < 0)
	break;
    }

    if (pollres < 0 || 
	tries == 3) {
      fprintf(stderr, "ERROR: no response received to libcmtspeechdata reset.\n");
      res = -1;
    }
  }
  else {
    fprintf(stderr, "ERROR: invalid libcmtspeechdata descriptor.\n");
    res = -2;
  }

  cmtspeech_close(cmtspeech);

  return res;
}
