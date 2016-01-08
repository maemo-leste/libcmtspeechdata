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


struct test_ctx {
  DBusConnection* dbus_conn;
  int dbus_fd;
  DBusWatch *dbus_watch;
  bool call_server_status;
  int verbose;
  cmtspeech_t *cmtspeech;
  pa_simple *source;
  pa_simple *sink;
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
static const pa_sample_spec ss = {
	.format = PA_SAMPLE_S16LE,
	.rate = 4000,
	.channels = 2
};

static void start_sink(struct test_ctx *ctx)
{
	int error;
	/* The sample type to use */
	if (!(ctx->sink = pa_simple_new(NULL, "libcmtspeech_ofono", PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error))) {
		fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
		exit(1);
	}
}

static void start_source(struct test_ctx *ctx)
{
	int error;
	/* Create the recording stream */
	if (!(ctx->source = pa_simple_new(NULL, "libcmtspeech_ofono", PA_STREAM_RECORD, NULL, "record", &ss, NULL, NULL, &error))) {
		fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
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

static void report_sound(struct test_ctx *ctx)
{
	pa_usec_t latency_p, latency_r;
	int error;
	if ((latency_p = pa_simple_get_latency(ctx->sink, &error)) == (pa_usec_t) -1) {
		fprintf(stderr, __FILE__": pa_simple_get_latency() failed: %s\n", pa_strerror(error));
		exit(1);
	}
	if ((latency_r = pa_simple_get_latency(ctx->source, &error)) == (pa_usec_t) -1) {
		fprintf(stderr, __FILE__": pa_simple_get_latency() failed: %s\n", pa_strerror(error));
		exit(1);
	}

	if (latency_r > 1330000) {
		fprintf(stderr, "...flush\n");

		if (pa_simple_flush(ctx->source, &error) < 0) {
			fprintf(stderr, __FILE__": pa_simple_flush() failed: %s\n", pa_strerror(error));
			return;
		}

		fprintf(stderr, "\rplayback %10.0f usec, record %10.0f usec    ", (float)latency_p, (float)latency_r);
	}
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

  while (res = getopt_long(argc, argv, "hva", opt_tbl, &opt_index), res != -1) {
    switch (res)
      {

      case 'v':
	++ctx->verbose;
	fprintf(stderr, PREFIX "Increasing verbosity to %d.\n", ctx->verbose);
	break;

      case 'a':
	fprintf(stderr, "Enabling audio path\n");
	ctx->source = 0;
	ctx->sink = 0;
	ctx->data_through = 0;
#if 0
	{
	  int flags = fcntl(ctx->source_fd, F_GETFL, 0);
	  fcntl(ctx->source_fd, F_SETFL, flags | O_NONBLOCK);
	}
#endif
	break;

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

	while (1) {
		pa_usec_t latency_r;
		
		if ((latency_r = pa_simple_get_latency(ctx->source, &error)) == (pa_usec_t) -1) {
			fprintf(stderr, __FILE__": pa_simple_get_latency() failed: %s\n", pa_strerror(error));
			exit(1);
		}

		if (latency_r < 10000)
			break;

		res = cmtspeech_ul_buffer_acquire(ctx->cmtspeech, &ulbuf);
		if (res != 0)
			break;

		num = pa_simple_read(ctx->source, ulbuf->payload, ulbuf->pcount, &error);
		if (num != ulbuf->pcount) {
			fprintf(stderr, "Not enough data on input (%d/%d), error %s\n", num, ulbuf->pcount,
				pa_strerror(error));
		}
		ctx->data_through += ulbuf->pcount;
      
		res = cmtspeech_ul_buffer_release(ctx->cmtspeech, ulbuf);
		if (res != 0)
			fprintf(stderr, "Could notrelease ulbuf, says (%d)\n", res);
	}
     
	res = cmtspeech_dl_buffer_acquire(ctx->cmtspeech, &dlbuf);
	if (res != 0) {
		return;
	}

	DEBUG(fprintf(stderr, PREFIX "Received a DL packet (%u bytes).\n", dlbuf->count));
	/* (cmtspeech_protocol_state(ctx->cmtspeech) == CMTSPEECH_STATE_ACTIVE_DLUL) */
  
	if (!ctx->sink) {
		fprintf(stderr, PREFIX "have packet but no sink?.\n");
		return;
	}
	num = pa_simple_write(ctx->sink, dlbuf->payload, dlbuf->pcount, &error);
	if (dlbuf->pcount != num) {
		fprintf(stderr, "Error on output? %d/%d, error %s\n", num, dlbuf->pcount, pa_strerror(error));
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

int main(int argc, char *argv[])
{
  DBusBusType dbus_type = DBUS_BUS_SYSTEM;
  struct test_ctx ctx0;
  struct test_ctx *ctx = &ctx0;
  int res = 0;

  fprintf(stderr, "NFS sucks, version 0.0.1\n");
  priv_setup_signals();

  ctx->dbus_conn = NULL;
  ctx->dbus_fd = -1;
  ctx->dbus_watch = NULL;
  ctx->verbose = 0;

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
