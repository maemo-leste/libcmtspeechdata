/***
  This file is part of PulseAudio.
  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.
  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#define BUFSIZE 1024


int main(int argc, char*argv[]) {
    /* The sample type to use */
    static const pa_sample_spec ss = {
        .format = PA_SAMPLE_S16LE,
        .rate = 44100,
        .channels = 2
    };
    static const pa_buffer_attr attr = {
      .fragsize = 1024,
      .maxlength = 1024,
      .minreq = 1024,
      .prebuf = 1024,
      .tlength = 1024,
    };
    pa_simple *r = NULL;
    pa_simple *p = NULL;
    int ret = 1;
    int error;
    pa_buffer_attr *p_attr = NULL; // &attr;
    int opt = 0; // | PA_STREAM_ADJUST_LATENCY

    /* Create a new playback stream */
    if (!(p = pa_simple_new(NULL, argv[0], PA_STREAM_PLAYBACK | opt, NULL, "playback", &ss, NULL, p_attr, &error))) {
        fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
        goto finish;
    }
    

    /* Create the recording stream */
    if (!(r = pa_simple_new(NULL, argv[0], PA_STREAM_RECORD | opt, NULL, "record", &ss, NULL, p_attr, &error))) {
        fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
        goto finish;
    }

    for (;;) {
        uint8_t buf[BUFSIZE];
        /* Record some data ... */
        if (pa_simple_read(r, buf, sizeof(buf), &error) < 0) {
            fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n", pa_strerror(error));
            goto finish;
        }
	{
	  pa_usec_t latency_p, latency_r;
	  static pa_usec_t latency_p_avg, latency_r_avg;

	  if ((latency_p = pa_simple_get_latency(p, &error)) == (pa_usec_t) -1) {
            fprintf(stderr, __FILE__": pa_simple_get_latency() failed: %s\n", pa_strerror(error));
            goto finish;
	  }
	  if ((latency_r = pa_simple_get_latency(r, &error)) == (pa_usec_t) -1) {
            fprintf(stderr, __FILE__": pa_simple_get_latency() failed: %s\n", pa_strerror(error));
            goto finish;
	  }
	  latency_p_avg = (latency_p_avg * 0.9) + latency_p * 0.1; 
	  latency_r_avg = (latency_r_avg * 0.9) + latency_r * 0.1; 
	  fprintf(stderr, "\rplayback %7.0f usec avg %7.0f, record %7.0f usec avg %7.0f   ", 
		  (float)latency_p, (float)latency_p_avg, (float)latency_r, (float)latency_r_avg);
	  if (latency_r > 1330000) {
	    fprintf(stderr, "...flush\n");

#if 0
	    if (pa_simple_flush(r, &error) < 0) {
	      fprintf(stderr, __FILE__": pa_simple_flush() failed: %s\n", pa_strerror(error));
	      goto finish;
	    }
#endif
	  }
	}
        /* ... and play it */
        if (pa_simple_write(p, buf, sizeof(buf), &error) < 0) {
            fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
            goto finish;
        }
	

    }

    /* Make sure that every single sample was played */
    if (pa_simple_drain(p, &error) < 0) {
        fprintf(stderr, __FILE__": pa_simple_drain() failed: %s\n", pa_strerror(error));
        goto finish;
    }
    ret = 0;

finish:
    if (r)
        pa_simple_free(r);
    if (p)
        pa_simple_free(p);
    return ret;
}

