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

/** @file test_cmtspeech.c
 *
 * Unit test for cmtspeech.h.
 */

#include <assert.h>
#include <getopt.h>

#include <stdio.h>
#include <stdlib.h>

#include <poll.h>
#include <signal.h>

#include "cmtspeech.h"

enum TestState {
  TEST_STATE_INIT = 0,
  TEST_STATE_STARTED,
  TEST_STATE_WAITING_FOR_CONNECTED,
  TEST_STATE_WAITING_FOR_DISCONNECTED,
  TEST_STATE_EXIT,
  TEST_STATE_ERROR,
  TEST_STATE_LAST
};

const char *test_state_strings[TEST_STATE_LAST] =
  {
    "INIT",
    "STARTED",
    "WAITING_FOR_CONNECTED",
    "WAITING_FOR_DISCONNECTED",
    "EXIT",
    "ERROR",
  };

struct test_ctx {
  cmtspeech_t *cmtspeech;
  int state;
  int conns;
  int conns_target;
  int errors;
  int warnings;
  int verbose;
};

#define TEST_INACTIVITY_TIMEOUT_SEC 300.0f
#define TEST_ROUNDS 50 /* takes around 40sec */

static sig_atomic_t global_exit_request = 0;

#define PREFIX "TESTER: "

/**
 * Returns 'a - b' in seconds.
 */
static float priv_timespec_diff(struct timespec *a, struct timespec *b)
{
  return (float)(a->tv_sec + ((float)a->tv_nsec)/1000000000U) -
    (float)(b->tv_sec + ((float)b->tv_nsec)/1000000000U);
}

#define TEST_CHECK_INT(ctx, x, y) \
  test_check_impl(ctx, true, x, __LINE__, #x, y)

#define TEST_WARN_INT(ctx, x, y) \
  test_check_impl(ctx, false, x, __LINE__, #x, y)

static void test_check_impl(struct test_ctx *ctx, bool error, int x, int line, const char* check, int y)
{
  if (!x) {
    if (error)
      ++ctx->errors;
    else
      ++ctx->warnings;

    fprintf(stderr, PREFIX "%s %s:%d: case '%s' with value %d (%d errors, %d warnings)\n",
	    (error ? "FAILURE" : "WARNING"),
	    __FILE__, line,
	    check, y, ctx->errors, ctx->warnings);
  }
}

static void test_state_tr(struct test_ctx *ctx, int to)
{
  printf(PREFIX "Changing tester state to %s (from %s).\n",
	 test_state_strings[to],
	 test_state_strings[ctx->state]);

  ctx->state = to;
}

static int link_updown_cmtspeech_events(struct test_ctx *ctx)
{
  cmtspeech_event_t event;
  static int sleepind = 0;
  struct timespec tv[10] = {
    { 0,  2000000LU },
    { 0,  1000000LI },
    { 0,   500000LU },
    { 0,   200000LU },
    { 0,  2000000LU },
    { 0,     3000LU },
    { 0,100000000LI },
    { 0,        0LU },
    { 0,        0LU },
    { 4,        0LU },
  };
  int tr;

  cmtspeech_read_event(ctx->cmtspeech, &event);
  tr = cmtspeech_event_to_state_transition(ctx->cmtspeech, &event);

  if (ctx->verbose)
    printf(PREFIX "cmtspeech event received, tr %d\n", tr);


  TEST_WARN_INT(ctx,
		(tr == CMTSPEECH_TR_1_CONNECTED ||
		 tr == CMTSPEECH_TR_2_DISCONNECTED), ctx->state);

  if (ctx->state == TEST_STATE_WAITING_FOR_CONNECTED &&
      tr == CMTSPEECH_TR_1_CONNECTED) {
    ++ctx->conns;
    test_state_tr(ctx, TEST_STATE_WAITING_FOR_DISCONNECTED);
    cmtspeech_state_change_call_status(ctx->cmtspeech, 0);
  }
  else if (ctx->state == TEST_STATE_WAITING_FOR_DISCONNECTED &&
	   tr == CMTSPEECH_TR_2_DISCONNECTED) {
    if (ctx->conns < ctx->conns_target) {
      test_state_tr(ctx, TEST_STATE_WAITING_FOR_CONNECTED);
      cmtspeech_state_change_call_status(ctx->cmtspeech, 1);
    }
    else {
      /* note: reached end-of-test */
      test_state_tr(ctx, TEST_STATE_EXIT);
    }
  }
  else {
    fprintf(stderr, 
	    PREFIX "unknown transition %d in state %d\n", tr, ctx->state);
    test_state_tr(ctx, TEST_STATE_ERROR);
  }

  if (ctx->verbose)
    printf(PREFIX "sleeping for %lds:%ldns (sleep %d/%d)\n",
	   tv[sleepind].tv_sec, tv[sleepind].tv_nsec, 
	   sleepind, (sizeof(tv) / sizeof(struct timespec)));
  nanosleep(&tv[sleepind], NULL);

  ++sleepind;
  sleepind %= (sizeof(tv) / sizeof(struct timespec));

  return 0;
}

static int link_updown_loop(struct test_ctx *ctx)
{
  struct timespec mainloop_started_at;

  clock_gettime(CLOCK_MONOTONIC, &mainloop_started_at);

  test_state_tr(ctx, TEST_STATE_STARTED);

  while(ctx->state < TEST_STATE_EXIT) {
    struct pollfd fds[1];
    struct timespec now;
    int pollres;
    float testrun_len;


    if (ctx->state == TEST_STATE_STARTED) {
      cmtspeech_state_change_call_status(ctx->cmtspeech, 1);
      test_state_tr(ctx, TEST_STATE_WAITING_FOR_CONNECTED);
    }

    fds[0].fd = cmtspeech_descriptor(ctx->cmtspeech);
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    /* very verbose */
    /* fprintf(stderr, "p"); */

    pollres = poll(fds, 1, 5000);

    if (pollres > 0) {
      if (fds[0].revents & POLLIN) {
	int flags = 0;
	int res =
	  cmtspeech_check_pending(ctx->cmtspeech, &flags);

	if (res > 0) {
	  if (flags & CMTSPEECH_EVENT_CONTROL) {
	    link_updown_cmtspeech_events(ctx);
	  }
	}

	if (res < 0) {
	  fprintf(stderr, PREFIX "cmtspeech mainloop error\n");
	  test_state_tr(ctx, TEST_STATE_ERROR);
	  break;
	}
      }
    }

    clock_gettime(CLOCK_MONOTONIC, &now);
    testrun_len = priv_timespec_diff(&now, &mainloop_started_at);
    if (testrun_len > TEST_INACTIVITY_TIMEOUT_SEC &&
	ctx->state < TEST_STATE_EXIT) {

      /* test call should have been answered by now */
      TEST_CHECK_INT(ctx, ctx->state >= TEST_STATE_STARTED, ctx->state);
      fprintf(stderr, PREFIX "cmtspeech mainloop stuck after %f seconds\n", testrun_len);

      test_state_tr(ctx, TEST_STATE_ERROR);

      break;
    }


    if (global_exit_request)
      test_state_tr(ctx, TEST_STATE_ERROR);
  }

  if (ctx->state == TEST_STATE_ERROR)
    return -1;

  return 0;
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
    {"verbose",          0, NULL, 'v'},
    {"help",            0, NULL, 'h'},
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

  while (res = getopt_long(argc, argv, "hv", opt_tbl, &opt_index), res != -1) {
    switch (res)
      {

      case 'v':
	ctx->verbose = 1;
	break;

      case 'h':
      default:
	priv_usage(argv[0]);
	break;
      }
  }
}

int main(int argc, char *argv[])
{
  struct test_ctx ctx;
  cmtspeech_t *cmtspeech;
  int res = 0;

  priv_setup_signals();

  ctx.state = TEST_STATE_INIT;
  ctx.conns = 0;
  ctx.conns_target = TEST_ROUNDS;
  ctx.errors = 0;
  ctx.warnings = 0;
  ctx.verbose = 0;

  priv_parse_options(&ctx, argc, argv);

  cmtspeech_init();
  cmtspeech = cmtspeech_open();
  ctx.cmtspeech = cmtspeech;

  if (ctx.verbose) {
    cmtspeech_trace_toggle(CMTSPEECH_TRACE_STATE_CHANGE, true);
    cmtspeech_trace_toggle(CMTSPEECH_TRACE_IO, true);
  }

  if (!cmtspeech)
    return -1;

  if (link_updown_loop(&ctx))
    res = -2;

  cmtspeech_close(cmtspeech);

  return res;
}
