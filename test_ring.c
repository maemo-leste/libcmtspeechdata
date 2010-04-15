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

/** @file test_ring.c
 *
 * Unit test for sal_ring.h.
 */

#include <check.h>
#include <stdlib.h>
#include <stdint.h>

#include "sal_ring.h"

START_TEST(test_ring_read)
{
  ring_buffer_t ringbuf;

  ring_buffer_init(&ringbuf, NULL, 16);
  fail_unless(ring_buffer_avail_for_read(&ringbuf) == 0);
  fail_unless(ring_buffer_cavail_for_read(&ringbuf) == 0);

  ringbuf.read_idx = 15;
  ringbuf.write_idx = 0;
  fail_unless(ring_buffer_avail_for_read(&ringbuf) == 1);
  fail_unless(ring_buffer_cavail_for_read(&ringbuf) == 1);

  ringbuf.read_idx = 15;
  ringbuf.write_idx = 2;
  fail_unless(ring_buffer_avail_for_read(&ringbuf) == 3);
  fail_unless(ring_buffer_cavail_for_read(&ringbuf) == 1);

  ringbuf.read_idx = 1;
  ringbuf.write_idx = 2;
  fail_unless(ring_buffer_avail_for_read(&ringbuf) == 1);
  fail_unless(ring_buffer_cavail_for_read(&ringbuf) == 1);

  ringbuf.read_idx = 0;
  ringbuf.write_idx = 15;
  fail_unless(ring_buffer_avail_for_read(&ringbuf) == 15);
  fail_unless(ring_buffer_cavail_for_read(&ringbuf) == 15);

}
END_TEST

START_TEST(test_ring_write)
{
  ring_buffer_t ringbuf;

  ring_buffer_init(&ringbuf, NULL, 16);
  fail_unless(ring_buffer_avail_for_write(&ringbuf) == 15);
  fail_unless(ring_buffer_cavail_for_write(&ringbuf) == 15);

  ringbuf.read_idx = 0;
  ringbuf.write_idx = 0;
  fail_unless(ring_buffer_avail_for_write(&ringbuf) == 15);
  fail_unless(ring_buffer_cavail_for_write(&ringbuf) == 15);

  ringbuf.read_idx = 8;
  ringbuf.write_idx = 8;
  fail_unless(ring_buffer_avail_for_write(&ringbuf) == 15);
  fail_unless(ring_buffer_cavail_for_write(&ringbuf) == 8);

  ringbuf.read_idx = 15;
  ringbuf.write_idx = 0;
  fail_unless(ring_buffer_avail_for_write(&ringbuf) == 14);
  fail_unless(ring_buffer_cavail_for_write(&ringbuf) == 14);

  ringbuf.read_idx = 15;
  ringbuf.write_idx = 2;
  fail_unless(ring_buffer_avail_for_write(&ringbuf) == 12);
  fail_unless(ring_buffer_cavail_for_write(&ringbuf) == 12);

  ringbuf.read_idx = 0;
  ringbuf.write_idx = 1;
  fail_unless(ring_buffer_avail_for_write(&ringbuf) == 14);
  fail_unless(ring_buffer_cavail_for_write(&ringbuf) == 14);

  ringbuf.read_idx = 1;
  ringbuf.write_idx = 2;
  fail_unless(ring_buffer_avail_for_write(&ringbuf) == 14);
  fail_unless(ring_buffer_cavail_for_write(&ringbuf) == 14);

  ringbuf.read_idx = 0;
  ringbuf.write_idx = 15;
  fail_unless(ring_buffer_avail_for_write(&ringbuf) == 0);
  fail_unless(ring_buffer_cavail_for_write(&ringbuf) == 0);

}
END_TEST

Suite *ring_suite(void)
{
  Suite *suite = suite_create("ring_buffer");
  TCase *ring = tcase_create("ring_buffer");

  tcase_add_test(ring, test_ring_read);
  tcase_add_test(ring, test_ring_write);
  suite_add_tcase(suite, ring);

  return suite;
}

int main(int argc, char *argv[])
{
  int nr_failed;
  Suite *suite = ring_suite();
  SRunner *runner = srunner_create(suite);
  srunner_set_xml(runner, "/tmp/result.xml");
  srunner_run_all(runner, CK_NORMAL);
  nr_failed = srunner_ntests_failed(runner);
  srunner_free(runner);
  return (nr_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
