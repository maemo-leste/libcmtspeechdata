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

/** @file cmtspeech_ramp_test.c
 *
 * Tool that opens a libcmtspeechdata instance, performs
 * a loop of tests with the TEST_RAMP_PING interface, and
 * closes the instance.
 */

/**
 * General list of TODO items:
 *  - <none>
 */

#include <stdio.h>
#include <poll.h>

#include <cmtspeech.h>

static void priv_dump_ramp_frame(uint8_t *slotbuf, unsigned int slot_size)
{
  unsigned int i;
  /* note: unlike all other traces, this is printed directly to
     stdout */
  for(i = 0; i < slot_size; i += 4) {
    printf("%02X:%02X:%02X:%02X ", slotbuf[i], slotbuf[i+1], slotbuf[i+2], slotbuf[i+3]);
    /* note: print header on a separate line */
    if (i == 0)
      printf ("(32 bit frame header, payload follows)\n");
    else if ((i + 4) % 16 == 4)
      printf("\n");
    }
  if ((i + 4) % 16 != 4)
    printf("\n");
}    

int main(int argc, char *argv[]) {
  struct pollfd fds[1];
  cmtspeech_t *cmtspeech;
  int res = 0, i, fd;
  int pollres, test_msgs = 0;
  const int send_test_msgs = 3;
  const uint8_t test_frame_size = 81;
  
  cmtspeech_init();

  cmtspeech = cmtspeech_open();
  if (!cmtspeech) {
    fprintf(stderr, "ERROR: unable to open libcmtspeechdata instance\n");
    return -1;
  }

  fd = cmtspeech_descriptor(cmtspeech);
  if (fd < 0) {
    fprintf(stderr, "ERROR: invalid libcmtspeechdata descriptor.\n");
    return -2;
  }
    
  for (i = 0;; i++) {
    if (cmtspeech_protocol_state(cmtspeech) == CMTSPEECH_STATE_DISCONNECTED) {
      if (++test_msgs == send_test_msgs)
	break;

      cmtspeech_test_data_ramp_req(cmtspeech, 0xaa /*(uint8_t)i*/, test_frame_size);
    }

    fds[0].fd = fd;
    pollres = poll(fds, 1, 20);
    printf("poll returned %d (iter %d).\n", pollres, i);
    if (pollres == 1) {
      int flags = 0, res =
	cmtspeech_check_pending(cmtspeech, &flags);      
      if (res > 0) {
	if (flags & CMTSPEECH_EVENT_DL_DATA) {
	  cmtspeech_buffer_t *buf;
	  res = cmtspeech_dl_buffer_acquire(cmtspeech, &buf);
	  if (res == 0) {
	    printf("Received a test ramp packet (%u bytes). Dumping its contents.", buf->count);
	    priv_dump_ramp_frame(buf->data, buf->count);
	    res = cmtspeech_dl_buffer_release(cmtspeech, buf);
	  }
	}
	if (flags & CMTSPEECH_EVENT_CONTROL) {
	  cmtspeech_event_t event;
	  cmtspeech_read_event(cmtspeech, &event);
	  printf("read event %d.\n", event.msg_type);
	}
      }
    }
    else if (pollres < 0)
      break;
  }

  cmtspeech_close(cmtspeech);

  return res;
}
