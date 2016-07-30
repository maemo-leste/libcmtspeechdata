/* -*- c-file-style: "linux" -*- */
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

/** @file cmtspeech_ofono_test.c
 *
 * Tool that listens to org.ofono DBus messages for voice call
 * state, and when call server actives (i.e. a voice call is
 * established), the tool sets up a loopback for voice data path,
 * routing call downlink to uplink.
 *
 * Note: due to protocol timing limitations, this test does not work
 *       in 2G/GSM mode.
 */

/**
 * General list of TODO items:
 *  - <none>
 */

#include <assert.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmtspeech.h>
#include <dbus/dbus.h>

#include <unistd.h>
#include <fcntl.h>

#include <pulse/simple.h>
#include <pulse/error.h>

#ifndef PULSE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <getopt.h>
#include <alsa/asoundlib.h>
#include <sys/time.h>
#include <math.h>

/*
 *  Latency test program
 *
 *     Author: Jaroslav Kysela <perex@perex.cz>
 *
 *  This small demo program can be used for measuring latency between
 *  capture and playback. This latency is measured from driver (diff when
 *  playback and capture was started). Scheduler is set to SCHED_RR.
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */
#endif

struct test_ctx {
	DBusConnection* dbus_conn;
	int dbus_fd;
	DBusWatch *dbus_watch;
	bool call_server_status;
	int verbose;
	cmtspeech_t *cmtspeech;
#ifndef PULSE
	snd_pcm_t *source;
	snd_pcm_t *sink;
#else
	pa_simple *source;
	pa_simple *sink;
#endif	
	int latency;
	int data_through;
};

#define PREFIX "cmtspeech_ofono_test: "

static sig_atomic_t global_exit_request = 0;

#define INFO(x) \
  do { if (ctx->verbose > 0) x; } while(0)
#define DEBUG(x) \
  do { if (ctx->verbose > 1) x; } while(0)

static dbus_bool_t priv_add_cb(DBusWatch *watch, void *data)
{
  struct test_ctx *ctx = (struct test_ctx*)data;
  int fd =
    dbus_watch_get_unix_fd(watch);

  ctx->dbus_fd = fd;
  ctx->dbus_watch = watch;

  DEBUG(fprintf(stderr, PREFIX "priv_add_cb: socket %d, watch %p (tracking %p).\n",
	       fd, watch, ctx->dbus_watch));

  return TRUE;
}

static void priv_remove_cb(DBusWatch *watch, void *data)
{
  struct test_ctx *ctx = (struct test_ctx*)data;

  DEBUG(fprintf(stderr, PREFIX "priv_remove_cb: (%p).\n", (void*)watch));

  if (ctx->dbus_watch == watch) {
    ctx->dbus_watch = NULL;
    ctx->dbus_fd = -1;
  }
}

static void priv_toggled_cb(DBusWatch *watch, void *data)
{
  struct test_ctx *ctx = (struct test_ctx*)data;
  dbus_bool_t enabled =
    dbus_watch_get_enabled(watch);

  DEBUG(fprintf(stderr, PREFIX "priv_toggled_cb: (%p) enabled=%d.\n", (void*)watch, enabled));

  if (ctx->dbus_watch == watch) {
    if (enabled == TRUE)
      ctx->dbus_fd = dbus_watch_get_unix_fd(watch);
    else
      ctx->dbus_fd = -1;
  }
}

static void priv_free_data_cb(void *data)
{
  /* no-op */
}

static inline void priv_add_match(DBusConnection *conn, const char* match, int *errs)
{
  DBusError dbus_error;
  dbus_error_init(&dbus_error);
  dbus_bus_add_match(conn, match, &dbus_error);
  if (dbus_error_is_set(&dbus_error) == TRUE)
    ++(*errs);
}

DBusConnection *test_dbus_make_connection(struct test_ctx *ctx, DBusBusType dbus_type)
{
  DBusConnection *conn;
  DBusError dbus_error;

  dbus_error_init(&dbus_error);

  conn = dbus_bus_get(dbus_type, &dbus_error);
  if (dbus_error_is_set(&dbus_error) != TRUE) {
    DEBUG(fprintf(stderr, PREFIX "Connection established to DBus (%d).\n", (int)dbus_type));
  }
  else {
    fprintf(stderr, PREFIX "ERROR: unable to connect to DBus\n");
  }

  return conn;
}

/**
 * Sets up the DBus connection.
 */
static int test_dbus_init(struct test_ctx *ctx, DBusBusType dbus_type)
{
  DBusConnection *conn;
  int add_match_errs = 0;

  ctx->call_server_status = 0;

  conn = test_dbus_make_connection(ctx, dbus_type);
  if (conn == NULL)
    return -1;

  dbus_connection_set_watch_functions(conn,
				      priv_add_cb,
				      priv_remove_cb,
				      priv_toggled_cb,
				      ctx,
				      priv_free_data_cb);

  priv_add_match(conn, "type='signal',interface='org.ofono.AudioSettings'", &add_match_errs);

  if (add_match_errs) {
    dbus_connection_unref(conn);
    return -1;
  }

  dbus_connection_flush(conn);
  ctx->dbus_conn = conn;

  return 0;

}

static void test_dbus_release(struct test_ctx *ctx)
{
  if (ctx->dbus_conn) {
      dbus_connection_unref(ctx->dbus_conn);
      ctx->dbus_conn = NULL;
  }
}

#ifdef PULSE
#if 0
static void flush_input(struct test_ctx *ctx)
{
  char scratch[10240];
  int total = 0;

  fprintf(stderr, "Flushing input...\n");
  if (!ctx->sink_fd)
    return;
  while(1) {
    int num;
    num = read(ctx->source_fd, scratch, 10240);
    if (num == -1)
      break;
    total += num;
  }
  fprintf(stderr, "Flushing input (%d)\n", total);
}
#endif

/* FIXME: Hmm. That makes no sense. Is it rate = 8000, channels = 1? */
/* FIXME: requesting 8000 crashes pulseaudio. */
static const pa_sample_spec ss = {
	.format = PA_SAMPLE_S16LE,
	.rate = 4000,
	.channels = 2
};
static const pa_buffer_attr pa_attr = {
	.fragsize = (uint32_t) 1024,
	.maxlength = (uint32_t)1,
	.minreq = (uint32_t) 1024,
	.prebuf = (uint32_t)1,
	.tlength = (uint32_t) 1024,
	/* fragsize / tlength can be 4096> pulseaudio CPU drops from 33% CPU to 10%, but latency can be heard */
};

static void start_sink(struct test_ctx *ctx)
{
	int error;
	/* The sample type to use */
	if (!(ctx->sink = pa_simple_new(NULL, "libcmtspeech_ofono", PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, &pa_attr, &error))) {
		fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
		exit(1);
	}
}

static void start_source(struct test_ctx *ctx)
{
	/* Create the recording stream */
	if (!(ctx->source = pa_simple_new(NULL, "libcmtspeech_ofono", PA_STREAM_RECORD, NULL, "record", &ss, NULL, &pa_attr, &error))) {
		fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_str
			error(error));
		exit(1);
	}
}

static void stop_source(struct test_ctx *ctx)
{
	if (ctx->source)
		pa_simple_free(ctx->source);
	ctx->source = NULL;
}

static void stop_sink(struct test_ctx *ctx)
{
	if (ctx->sink)
		pa_simple_free(ctx->sink);
	ctx->sink = NULL;
}

#else
char *pdevice = "plughw:0,0";
char *cdevice = "plughw:0,0";
snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
int rate = 8000;
int channels = 1;
int buffer_size = 0;		/* auto */
int period_size = 0;		/* auto */
int latency_min = 640;		/* in frames / 2 */
int latency_max = 2048;		/* in frames / 2 */
int loop_sec = 90;		/* seconds */
int block = 0;			/* block mode */
int use_poll = 0;
int resample = 1;
unsigned long loop_limit;

snd_output_t *output = NULL;

int setparams_stream(snd_pcm_t *handle,
		     snd_pcm_hw_params_t *params,
		     const char *id)
{
	int err;
	unsigned int rrate;

	err = snd_pcm_hw_params_any(handle, params);
	if (err < 0) {
		printf("Broken configuration for %s PCM: no configurations available: %s\n", snd_strerror(err), id);
		return err;
	}
	err = snd_pcm_hw_params_set_rate_resample(handle, params, resample);
	if (err < 0) {
		printf("Resample setup failed for %s (val %i): %s\n", id, resample, snd_strerror(err));
		return err;
	}
	err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		printf("Access type not available for %s: %s\n", id, snd_strerror(err));
		return err;
	}
	err = snd_pcm_hw_params_set_format(handle, params, format);
	if (err < 0) {
		printf("Sample format not available for %s: %s\n", id, snd_strerror(err));
		return err;
	}
	err = snd_pcm_hw_params_set_channels(handle, params, channels);
	if (err < 0) {
		printf("Channels count (%i) not available for %s: %s\n", channels, id, snd_strerror(err));
		return err;
	}
	rrate = rate;
	err = snd_pcm_hw_params_set_rate_near(handle, params, &rrate, 0);
	if (err < 0) {
		printf("Rate %iHz not available for %s: %s\n", rate, id, snd_strerror(err));
		return err;
	}
	if ((int)rrate != rate) {
		printf("Rate doesn't match (requested %iHz, get %iHz)\n", rate, err);
		return -EINVAL;
	}
	return 0;
}

int setparams_bufsize(snd_pcm_t *handle,
		      snd_pcm_hw_params_t *params,
		      snd_pcm_hw_params_t *tparams,
		      snd_pcm_uframes_t bufsize,
		      const char *id)
{
	int err;
	snd_pcm_uframes_t periodsize;

	snd_pcm_hw_params_copy(params, tparams);
	periodsize = bufsize * 2;
	err = snd_pcm_hw_params_set_buffer_size_near(handle, params, &periodsize);
	if (err < 0) {
		printf("Unable to set buffer size %li for %s: %s\n", bufsize * 2, id, snd_strerror(err));
		return err;
	}
	if (period_size > 0)
		periodsize = period_size;
	else
		periodsize /= 2;
	err = snd_pcm_hw_params_set_period_size_near(handle, params, &periodsize, 0);
	if (err < 0) {
		printf("Unable to set period size %li for %s: %s\n", periodsize, id, snd_strerror(err));
		return err;
	}
	return 0;
}

int setparams_set(snd_pcm_t *handle,
		  snd_pcm_hw_params_t *params,
		  snd_pcm_sw_params_t *swparams,
		  const char *id)
{
	int err;
	snd_pcm_uframes_t val;

	err = snd_pcm_hw_params(handle, params);
	if (err < 0) {
		printf("Unable to set hw params for %s: %s\n", id, snd_strerror(err));
		return err;
	}
	err = snd_pcm_sw_params_current(handle, swparams);
	if (err < 0) {
		printf("Unable to determine current swparams for %s: %s\n", id, snd_strerror(err));
		return err;
	}
	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, 0x7fffffff);
	if (err < 0) {
		printf("Unable to set start threshold mode for %s: %s\n", id, snd_strerror(err));
		return err;
	}
	if (!block)
		val = 4;
	else
		snd_pcm_hw_params_get_period_size(params, &val, NULL);
	err = snd_pcm_sw_params_set_avail_min(handle, swparams, val);
	if (err < 0) {
		printf("Unable to set avail min for %s: %s\n", id, snd_strerror(err));
		return err;
	}
	err = snd_pcm_sw_params(handle, swparams);
	if (err < 0) {
		printf("Unable to set sw params for %s: %s\n", id, snd_strerror(err));
		return err;
	}
	return 0;
}

void showstat(snd_pcm_t *handle, size_t frames)
{
	int err;
	snd_pcm_status_t *status;

	snd_pcm_status_alloca(&status);
	if ((err = snd_pcm_status(handle, status)) < 0) {
		printf("Stream status error: %s\n", snd_strerror(err));
		exit(0);
	}
	printf("*** frames = %li ***\n", (long)frames);
	snd_pcm_status_dump(status, output);
}

void showlatency(size_t latency)
{
	double d;
	latency *= 2;
	d = (double)latency / (double)rate;
	printf("Trying latency %li frames, %.3fus, %.6fms (%.4fHz)\n", (long)latency, d * 1000000, d * 1000, (double)1 / d);
}

void showinmax(size_t in_max)
{
	double d;

	printf("Maximum read: %li frames\n", (long)in_max);
	d = (double)in_max / (double)rate;
	printf("Maximum read latency: %.3fus, %.6fms (%.4fHz)\n", d * 1000000, d * 1000, (double)1 / d);
}

void gettimestamp(snd_pcm_t *handle, snd_timestamp_t *timestamp)
{
	int err;
	snd_pcm_status_t *status;

	snd_pcm_status_alloca(&status);
	if ((err = snd_pcm_status(handle, status)) < 0) {
		printf("Stream status error: %s\n", snd_strerror(err));
		exit(0);
	}
	snd_pcm_status_get_trigger_tstamp(status, timestamp);
}

void setscheduler(void)
{
	struct sched_param sched_param;

	if (sched_getparam(0, &sched_param) < 0) {
		printf("Scheduler getparam failed...\n");
		return;
	}
	sched_param.sched_priority = sched_get_priority_max(SCHED_RR);
	if (!sched_setscheduler(0, SCHED_RR, &sched_param)) {
		printf("Scheduler set to Round Robin with priority %i...\n", sched_param.sched_priority);
		fflush(stdout);
		return;
	}
	printf("!!!Scheduler set to Round Robin with priority %i FAILED!!!\n", sched_param.sched_priority);
}

long timediff(snd_timestamp_t t1, snd_timestamp_t t2)
{
	signed long l;

	t1.tv_sec -= t2.tv_sec;
	l = (signed long) t1.tv_usec - (signed long) t2.tv_usec;
	if (l < 0) {
		t1.tv_sec--;
		l = 1000000 + l;
		l %= 1000000;
	}
	return (t1.tv_sec * 1000000) + l;
}

long readbuf(snd_pcm_t *handle, char *buf, long len, size_t *frames, size_t *max)
{
	long r;

	if (!block) {
		do {
			r = snd_pcm_readi(handle, buf, len);
		} while (r == -EAGAIN);
		if (r > 0) {
			*frames += r;
			if ((long)*max < r)
				*max = r;
		}
		// printf("read = %li\n", r);
	} else {
		int frame_bytes = (snd_pcm_format_width(format) / 8) * channels;
		do {
			r = snd_pcm_readi(handle, buf, len);
			if (r > 0) {
				buf += r * frame_bytes;
				len -= r;
				*frames += r;
				if ((long)*max < r)
					*max = r;
			}
			// printf("r = %li, len = %li\n", r, len);
		} while (r >= 1 && len > 0);
	}
	// showstat(handle, 0);
	return r;
}

long read_bytes(struct test_ctx *ctx, void *buf, int len)
{
	size_t a = 0, b = 0;

	return readbuf(ctx->source, buf, len, &a, &b);
}

long writebuf(snd_pcm_t *handle, char *buf, long len, size_t *frames)
{
	long r;

	while (len > 0) {
		r = snd_pcm_writei(handle, buf, len);
		if (r == -EAGAIN)
			continue;
		// printf("write = %li\n", r);
		if (r < 0)
			return r;
		// showstat(handle, 0);
		buf += r * 4;
		len -= r;
		*frames += r;
	}
	return 0;
}

long write_bytes(struct test_ctx *ctx, void *buf, int len)
{
	size_t a = 0;

	return writebuf(ctx->sink, buf, len, &a);
}

static void start_sink(struct test_ctx *ctx)
{
	int err;
	snd_pcm_hw_params_t *pt_params, *ct_params;	/* templates with rate, format and channels */
	snd_pcm_hw_params_t *p_params, *c_params;
	snd_pcm_sw_params_t *p_swparams, *c_swparams;
	snd_pcm_uframes_t p_size, c_size, p_psize, c_psize;
	unsigned int p_time, c_time;
	unsigned int val;
	int size = 1024;
	int *bufsize = &size;
	
	/* The sample type to use */
	if ((err = snd_pcm_open(&ctx->sink, pdevice, SND_PCM_STREAM_PLAYBACK, block ? 0 : SND_PCM_NONBLOCK)) < 0) {
		printf("Playback open error: %s\n", snd_strerror(err));
		exit(1);
	}

	snd_pcm_hw_params_alloca(&p_params);
	snd_pcm_hw_params_alloca(&pt_params);
	snd_pcm_sw_params_alloca(&p_swparams);

	snd_pcm_t *phandle = ctx->sink;
	
	if ((err = setparams_stream(phandle, pt_params, "playback")) < 0) {
		printf("Unable to set parameters for playback stream: %s\n", snd_strerror(err));
		exit(0);
	}

	if ((err = setparams_bufsize(phandle, p_params, pt_params, *bufsize, "playback")) < 0) {
		printf("Unable to set sw parameters for playback stream: %s\n", snd_strerror(err));
		exit(0);
	}

	snd_pcm_hw_params_get_period_size(p_params, &p_psize, NULL);
	if (p_psize > (unsigned int)*bufsize)
		*bufsize = p_psize;
	snd_pcm_hw_params_get_period_time(p_params, &p_time, NULL);

	snd_pcm_hw_params_get_buffer_size(p_params, &p_size);
	if (p_psize * 2 < p_size) {
                snd_pcm_hw_params_get_periods_min(p_params, &val, NULL);
                if (val > 2) {
			printf("playback device does not support 2 periods per buffer\n");
			exit(0);
		}
		exit(1);
	}

		if ((err = setparams_set(phandle, p_params, p_swparams, "playback")) < 0) {
		printf("Unable to set sw parameters for playback stream: %s\n", snd_strerror(err));
		exit(0);
	}

	if ((err = snd_pcm_prepare(phandle)) < 0) {
		printf("Prepare error: %s\n", snd_strerror(err));
		exit(0);
	}

	snd_pcm_dump(phandle, output);

}

static void start_source(struct test_ctx *ctx)
{
	int err;
	snd_pcm_hw_params_t *pt_params, *ct_params;	/* templates with rate, format and channels */
	snd_pcm_hw_params_t *p_params, *c_params;
	snd_pcm_sw_params_t *p_swparams, *c_swparams;
	snd_pcm_uframes_t p_size, c_size, p_psize, c_psize;
	unsigned int p_time, c_time;
	unsigned int val;
	int size = 1024;
	int *bufsize = &size;
	
	/* Create the recording stream */
	if ((err = snd_pcm_open(&ctx->source, cdevice, SND_PCM_STREAM_CAPTURE, block ? 0 : SND_PCM_NONBLOCK)) < 0) {
		printf("Record open error: %s\n", snd_strerror(err));
		exit(1);
	}

	snd_pcm_hw_params_alloca(&c_params);
	snd_pcm_hw_params_alloca(&ct_params);
	snd_pcm_sw_params_alloca(&c_swparams);

	snd_pcm_t *chandle = ctx->source;

	if ((err = setparams_stream(chandle, ct_params, "capture")) < 0) {
		printf("Unable to set parameters for playback stream: %s\n", snd_strerror(err));
		exit(0);
	}

	if ((err = setparams_bufsize(chandle, c_params, ct_params, *bufsize, "capture")) < 0) {
		printf("Unable to set sw parameters for playback stream: %s\n", snd_strerror(err));
		exit(0);
	}

	snd_pcm_hw_params_get_period_size(c_params, &c_psize, NULL);
	if (c_psize > (unsigned int)*bufsize)
		*bufsize = c_psize;
	snd_pcm_hw_params_get_period_time(c_params, &c_time, NULL);

	snd_pcm_hw_params_get_buffer_size(c_params, &c_size);
	if (c_psize * 2 < c_size) {
                snd_pcm_hw_params_get_periods_min(c_params, &val, NULL);
		if (val > 2 ) {
			printf("capture device does not support 2 periods per buffer\n");
			exit(0);
		}
		exit(1);
	}

		if ((err = setparams_set(chandle, c_params, c_swparams, "capture")) < 0) {
		printf("Unable to set sw parameters for playback stream: %s\n", snd_strerror(err));
		exit(0);
	}


	snd_pcm_dump(chandle, output);
       
}

static void snd_stop(snd_pcm_t *p)
{
	snd_pcm_hw_free(p);
	snd_pcm_close(p);
}

static void stop_source(struct test_ctx *ctx)
{
	if (ctx->source) {
		snd_stop(ctx->source);
	}
	ctx->source = NULL;    
}

static void stop_sink(struct test_ctx *ctx)
{
	if (ctx->sink) {
		snd_stop(ctx->sink);
	}
	ctx->sink = NULL;
}
#endif

static void report_sound(struct test_ctx *ctx)
{
	pa_usec_t latency_p = -999999, latency_r = -999999;
	int error;

#ifdef FIXME
	if (ctx->sink)
		if ((latency_p = pa_simple_get_latency(ctx->sink, &error)) == (pa_usec_t) -1) {
			fprintf(stderr, __FILE__": pa_simple_get_latency() failed: %s\n", pa_strerror(error));
			exit(1);
		}

	if (ctx->source)
		if ((latency_r = pa_simple_get_latency(ctx->source, &error)) == (pa_usec_t) -1) {
			fprintf(stderr, __FILE__": pa_simple_get_latency() failed: %s\n", pa_strerror(error));
			exit(1);
		}

	fprintf(stderr, "playback %10.0f usec, record %10.0f usec   \n", (float)latency_p, (float)latency_r);
#endif
}

static bool test_handle_dbus_ofono(struct test_ctx *ctx, DBusMessage *msg)
{
  const char* property = NULL;
  DBusError dbus_error = DBUS_ERROR_INIT;
  bool parse_ok = false;

  if (dbus_message_is_signal(msg, "org.ofono.AudioSettings", "PropertyChanged")) {
    dbus_message_get_args(msg, &dbus_error,
			  DBUS_TYPE_STRING, &property,
			  DBUS_TYPE_INVALID);
    DEBUG(fprintf(stderr, PREFIX "received ofono AudioSettings change, params name='%s'\n",
		 property));
    if (strcmp(property, "Active") == 0) {
      DBusMessageIter i;
      int type;

      dbus_message_iter_init(msg, &i);
      dbus_message_iter_next(&i);
      type = dbus_message_iter_get_arg_type(&i);

      if (type == DBUS_TYPE_VARIANT) {
	DBusMessageIter j;
	dbus_message_iter_recurse(&i, &j);
	type = dbus_message_iter_get_arg_type(&j);
	if (type == 'b') {
	  dbus_bool_t state, old_state = ctx->call_server_status;
	  dbus_message_iter_get_basic(&j, &state);

	  if (state != old_state) {
	    INFO(fprintf(stderr, PREFIX "org.ofono.AudioSettings.Active to %d.\n", state));
#if 0	    
	    if (state == 1) {
	      start_sound(ctx);
	    }
	    if (state == 0) {
	      stop_sound(ctx);
	    }
#endif	      
	    cmtspeech_state_change_call_status(ctx->cmtspeech, state);
	    ctx->call_server_status = state;
	  }

	  parse_ok = true;
	}
      }

      if (parse_ok != true)
	fprintf(stderr, PREFIX "ERROR: error parsing org.ofono.AudioSettings property '%s'\n", property);
    }
    else {
      fprintf(stderr, PREFIX "ERROR: unsupported org.ofono.AudioSettings property '%s'\n", property);
    }
  }

  return parse_ok;
}

static int test_handle_dbus_message(struct test_ctx *ctx, DBusMessage *msg)
{
  int res = 0;
  const char* dbusif = dbus_message_get_interface(msg);

  DEBUG(fprintf(stderr, PREFIX "got message to if:%s, member:%s.\n",
	      dbusif, dbus_message_get_member(msg)));

  if (strstr(dbusif, "org.ofono.")) {
    test_handle_dbus_ofono(ctx, msg);
  }
  else
    INFO(fprintf(stderr, PREFIX "unknown/ignored signal: if=%s, member=%s.\n",
		dbusif, dbus_message_get_member(msg)));

  return res;
}

static void handle_signal_sigint(int signr)
{
  /* fprintf(stderr, PREFIX "SIGNAL\n"); */
  if (global_exit_request) {
    exit(-1);
  }

  global_exit_request = 1;
}

static int priv_setup_signals(void)
{
  struct sigaction sa;
  int res;

  sa.sa_handler = handle_signal_sigint;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  res = sigaction(SIGINT, &sa, NULL);
  if (res == -1)
    return -1;

  return res;
}

static struct option const opt_tbl[] =
  {
    {"verbose",         0, NULL, 'v'},
    {"help",            0, NULL, 'h'},
    {"audio",           0, NULL, 'a'},
    {NULL,              0, NULL, 0}
  };

static void priv_usage(char *name)
{
  fprintf(stderr, "usage: %s [options]\n", name);
  fprintf(stderr, "\noptions:\n\t[-v|--verbose] [-h|--help]\n");
  exit(1);
}

static void priv_parse_options(struct test_ctx *ctx, int argc, char *argv[])
{
  int opt_index;
  int res;

  assert(ctx);

  while (res = getopt_long(argc, argv, "hvat", opt_tbl, &opt_index), res != -1) {
    switch (res)
      {

      case 'v':
	++ctx->verbose;
	fprintf(stderr, PREFIX "Increasing verbosity to %d.\n", ctx->verbose);
	break;

      case 'a':
	fprintf(stderr, "Enabling audio path\n");
#if 0
	{
	  int flags = fcntl(ctx->source_fd, F_GETFL, 0);
	  fcntl(ctx->source_fd, F_SETFL, flags | O_NONBLOCK);
	}
#endif
	break;

      case 't':
	      printf("opening streams\n"); fflush(stdout);	      
	      start_sink(ctx);
	      printf("sink ok\n"); fflush(stdout);	      	      
	      start_source(ctx);
	      printf("start: %d", snd_pcm_start(ctx->source));
	      printf("streams open\n"); fflush(stdout);
	      while (1) {
		      int len = 128;
		      char buf[len];
		      long res;
		      int i;

		      for (i=0; i<len; i++)
			      buf[i] = i*5;
#if 0
		      res = read_bytes(ctx, buf, len);
		      printf("read: %d\n", res);
#endif
		      res = write_bytes(ctx, buf, len);
		      printf("write: %d\n", res);		      
	      }

      case 'h':
      default:
	priv_usage(argv[0]);
	break;
      }
  }
}

static void test_handle_cmtspeech_data(struct test_ctx *ctx)
{
	cmtspeech_buffer_t *dlbuf, *ulbuf;
	char scratch[10240];
	int res, error, num;
	int state = cmtspeech_protocol_state(ctx->cmtspeech);
	int active_ul = (state == CMTSPEECH_STATE_ACTIVE_DLUL);
	int active_dl = (state == CMTSPEECH_STATE_ACTIVE_DLUL) || (state == CMTSPEECH_STATE_ACTIVE_DL);
	int loops;

	while (ctx->source && active_ul) {
		pa_usec_t latency_r;
#ifdef FIXME
		if ((latency_r = pa_simple_get_latency(ctx->source, &error)) == (pa_usec_t) -1) {
			fprintf(stderr, __FILE__": pa_simple_get_latency() failed: %s\n", pa_strerror(error));
			exit(1);
		}

		if (latency_r < 100000) {
		  fprintf(stderr, "...skip latency (%d)\n", latency_r);
			break;
		}

		if (latency_r > 1000000) {
		  fprintf(stderr, "...flush latency (%d)\n", latency_r);
		  errror = readbuf(ctx->source, scratch, fixme_latency, &num);
		  if (error < 0){
		    fprintf(stderr, __FILE__": error during flushing: %s\n", pa_strerror(error));
		    exit(1);
		  }

		  if ((latency_r = pa_simple_get_latency(ctx->source, &error)) == (pa_usec_t) -1) {
		    fprintf(stderr, __FILE__": pa_simple_get_latency() failed: %s\n", pa_strerror(error));
		    exit(1);
		  }
		}
#endif
		res = cmtspeech_ul_buffer_acquire(ctx->cmtspeech, &ulbuf);
		if (res != 0)
			break;

		memset(ulbuf->payload, 0, ulbuf->pcount);
		error = readbuf(ctx->source, ulbuf->payload, ctx->latency, &num, &ulbuf->pcount);
		if (error) {
			fprintf(stderr, "error reading from source (%d), error %s\n", ulbuf->pcount,
				pa_strerror(error));
			break;
		}

		ctx->data_through += ulbuf->pcount;
      
		res = cmtspeech_ul_buffer_release(ctx->cmtspeech, ulbuf);
		if (res != 0) {
			fprintf(stderr, "Could notrelease ulbuf, says (%d)\n", res);
			break;
		}
		break;
	}

	if (!active_dl)
	  return;
     
	res = cmtspeech_dl_buffer_acquire(ctx->cmtspeech, &dlbuf);
	if (res != 0) {
		return;
	}

	DEBUG(fprintf(stderr, PREFIX "Received a DL packet (%u bytes).\n", dlbuf->count));
	/*  */
  
	if (!ctx->sink) {
		fprintf(stderr, PREFIX "have packet but no sink?.\n");
		exit(1);
	}
	int cnt = dlbuf->pcount;
	num = writebuf(ctx->sink, dlbuf->payload, ctx->latency, &cnt);
	if (num < 0) {
		fprintf(stderr, "Error writing to sink, %d, error %s\n", dlbuf->pcount, pa_strerror(error));
	}
	report_sound(ctx);	
	res = cmtspeech_dl_buffer_release(ctx->cmtspeech, dlbuf);
}

static int test_handle_cmtspeech_control(struct test_ctx *ctx)
{
  cmtspeech_event_t cmtevent;
  int state_tr = CMTSPEECH_TR_INVALID;

  cmtspeech_read_event(ctx->cmtspeech, &cmtevent);
  DEBUG(fprintf(stderr, PREFIX "read cmtspeech event %d.\n", cmtevent.msg_type));

  state_tr = cmtspeech_event_to_state_transition(ctx->cmtspeech, &cmtevent);
  DEBUG(fprintf(stderr, PREFIX "state transition %d.\n", state_tr));

  switch(state_tr)
    {
    case CMTSPEECH_TR_INVALID:
      fprintf(stderr, PREFIX "ERROR: invalid state transition\n");
      break;

    case CMTSPEECH_TR_1_CONNECTED:
    case CMTSPEECH_TR_2_DISCONNECTED:
	    break;
    case CMTSPEECH_TR_3_DL_START:
      /* Start audio playback here ? */
	    start_sink(ctx);
	    break;
    case CMTSPEECH_TR_4_DLUL_STOP:
	    stop_source(ctx);
	    stop_sink(ctx);
	    break;
      /* Stop audio ? */
    case CMTSPEECH_TR_5_PARAM_UPDATE:
      /* no-op */
      break;

    case CMTSPEECH_TR_6_TIMING_UPDATE:
    case CMTSPEECH_TR_7_TIMING_UPDATE:
      INFO(printf(PREFIX "WARNING: modem UL timing update ignored\n"));

    case CMTSPEECH_TR_10_RESET:
	    break;
    case CMTSPEECH_TR_11_UL_STOP:
	    stop_source(ctx);
	    break;
    case CMTSPEECH_TR_12_UL_START:
	    start_source(ctx);
	    break;
      /* Start audio record? */
      /* no-op */

    default:
      assert(0);
    }

  return 0;
}

static int test_mainloop(struct test_ctx *ctx)
{
  const int cmt = 0;
  const int dbus = 1;
  struct pollfd fds[2];
  int res = 0;

  fds[cmt].fd = cmtspeech_descriptor(ctx->cmtspeech);
  fds[cmt].events = POLLIN;
  assert(fds[cmt].fd >= 0);

  while(!global_exit_request) {
    int count = 1, pollres;

    if (ctx->dbus_fd >= 0) {
      fds[dbus].fd = ctx->dbus_fd;
      assert(fds[dbus].fd >= 0);
      fds[dbus].events = POLLIN;
      count = 2;
    }

    pollres = poll(fds, count, -1);

    DEBUG(fprintf(stderr, "poll returned %d (count:%d, cmt:%02X, dbus:%02X)\n",
		 pollres, count, fds[cmt].revents, fds[dbus].revents));

    /* Should we poll for audio, too? */
    if (pollres > 0) {

      if (fds[cmt].revents) {

	int flags = 0, res =
	  cmtspeech_check_pending(ctx->cmtspeech, &flags);

	if (res > 0) {

	  if (flags & CMTSPEECH_EVENT_DL_DATA)
	    test_handle_cmtspeech_data(ctx);

	  if (flags & CMTSPEECH_EVENT_CONTROL)
	    test_handle_cmtspeech_control(ctx);

	}
      }

      if (count > 1 && fds[dbus].revents) {
	DBusMessage *msg;

	if (dbus_connection_get_dispatch_status(ctx->dbus_conn) == DBUS_DISPATCH_DATA_REMAINS)
	  dbus_connection_dispatch(ctx->dbus_conn);

	dbus_watch_handle(ctx->dbus_watch, DBUS_WATCH_READABLE);

	msg = dbus_connection_pop_message(ctx->dbus_conn);

	while (msg) {
	  test_handle_dbus_message(ctx, msg);
	  dbus_message_unref(msg);
	  msg = dbus_connection_pop_message(ctx->dbus_conn);
	}

      }
    }
    else if (pollres < 0) {
      res = -1;
      break;
    }
  }

  return res;
}

int main_disabled(int argc, char *argv[])
{
	snd_pcm_t *phandle, *chandle;
	char *buffer;
	int err, latency, morehelp;
	int ok;
	snd_timestamp_t p_tstamp, c_tstamp;
	ssize_t r;
	size_t frames_in, frames_out, in_max;
	int effect = 0;
	morehelp = 0;

	loop_limit = loop_sec * rate;
	latency = latency_min - 4;
	buffer = malloc((latency_max * snd_pcm_format_width(format) / 8) * 2);

	printf("Playback device is %s\n", pdevice);
	printf("Capture device is %s\n", cdevice);
	printf("Parameters are %iHz, %s, %i channels, %s mode\n", rate, snd_pcm_format_name(format), channels, block ? "blocking" : "non-blocking");
	printf("Poll mode: %s\n", use_poll ? "yes" : "no");
	printf("Loop limit is %li frames, minimum latency = %i, maximum latency = %i\n", loop_limit, latency_min * 2, latency_max * 2);

	while (1) {
		frames_in = frames_out = 0;
		showlatency(latency);
		if ((err = snd_pcm_link(chandle, phandle)) < 0) {
			printf("Streams link error: %s\n", snd_strerror(err));
			exit(0);
		}
		if (snd_pcm_format_set_silence(format, buffer, latency*channels) < 0) {
			fprintf(stderr, "silence error\n");
			break;
		}
		if (writebuf(phandle, buffer, latency, &frames_out) < 0) {
			fprintf(stderr, "write error\n");
			break;
		}
		if (writebuf(phandle, buffer, latency, &frames_out) < 0) {
			fprintf(stderr, "write error\n");
			break;
		}

		if ((err = snd_pcm_start(chandle)) < 0) {
			printf("Go error: %s\n", snd_strerror(err));
			exit(0);
		}
		gettimestamp(phandle, &p_tstamp);
		gettimestamp(chandle, &c_tstamp);
#if 0
		printf("Playback:\n");
		showstat(phandle, frames_out);
		printf("Capture:\n");
		showstat(chandle, frames_in);
#endif

		ok = 1;
		in_max = 0;
		while (ok && frames_in < loop_limit) {
			if (use_poll) {
				/* use poll to wait for next event */
				snd_pcm_wait(chandle, 1000);
			}
			if ((r = readbuf(chandle, buffer, latency, &frames_in, &in_max)) < 0)
				ok = 0;
			else {
			 	if (writebuf(phandle, buffer, r, &frames_out) < 0)
					ok = 0;
			}
		}
		if (ok)
			printf("Success\n");
		else
			printf("Failure\n");
		printf("Playback:\n");
		showstat(phandle, frames_out);
		printf("Capture:\n");
		showstat(chandle, frames_in);
		showinmax(in_max);
		if (p_tstamp.tv_sec == p_tstamp.tv_sec &&
		    p_tstamp.tv_usec == c_tstamp.tv_usec)
			printf("Hardware sync\n");
		snd_pcm_drop(chandle);
		snd_pcm_nonblock(phandle, 0);
		snd_pcm_drain(phandle);
		snd_pcm_nonblock(phandle, !block ? 1 : 0);
		if (ok) {
#if 1
			printf("Playback time = %li.%i, Record time = %li.%i, diff = %li\n",
			       p_tstamp.tv_sec,
			       (int)p_tstamp.tv_usec,
			       c_tstamp.tv_sec,
			       (int)c_tstamp.tv_usec,
			       timediff(p_tstamp, c_tstamp));
#endif
			break;
		}
		snd_pcm_unlink(chandle);
		snd_pcm_hw_free(phandle);
		snd_pcm_hw_free(chandle);
	}
	snd_pcm_close(phandle);
	snd_pcm_close(chandle);
	return 0;
}

int main(int argc, char *argv[])
{
  DBusBusType dbus_type = DBUS_BUS_SYSTEM;
  struct test_ctx ctx0;
  struct test_ctx *ctx = &ctx0;
  int res = 0;

  fprintf(stderr, "NFS sucks, version 0.0.1\n");
  priv_setup_signals();

	int err = snd_output_stdio_attach(&output, stdout, 0);
	if (err < 0) {
		printf("Output failed: %s\n", snd_strerror(err));
		return 0;
	}

	setscheduler();

  ctx->dbus_conn = NULL;
  ctx->dbus_fd = -1;
  ctx->dbus_watch = NULL;
  ctx->verbose = 0;
	ctx->source = 0;
	ctx->sink = 0;
	ctx->data_through = 0;

  priv_parse_options(ctx, argc, argv);

  cmtspeech_init();
  test_dbus_init(ctx, dbus_type);

  if (ctx->verbose > 0) {
    cmtspeech_trace_toggle(CMTSPEECH_TRACE_STATE_CHANGE, true);
    cmtspeech_trace_toggle(CMTSPEECH_TRACE_IO, true);
    if (ctx->verbose > 1) {
      cmtspeech_trace_toggle(CMTSPEECH_TRACE_DEBUG, true);
    }
  }

  ctx->cmtspeech = cmtspeech_open();
  if (!ctx->cmtspeech) {
    fprintf(stderr, "ERROR: unable to open libcmtspeechdata instance\n");
    return -1;
  }

  INFO(fprintf(stderr, PREFIX "Setup succesful, entering mainloop.\n"));

  res = test_mainloop(ctx);

  cmtspeech_close(ctx->cmtspeech);
  test_dbus_release(ctx);

  INFO(fprintf(stderr, PREFIX "Completed, exiting (%d).\n", res));

  return res;
}
