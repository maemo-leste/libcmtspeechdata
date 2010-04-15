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

/** @file test_cmtspeech_msgs.c
 *
 * Unit test for cmtspeech_msgs.h.
 */

#include <check.h>
#include <stdlib.h>
#include <stdint.h>

#include "cmtspeech_msgs.h"
#include "cmtspeech.h"

#if CMTSPEECH_BIG_ENDIAN_CMDS
#define BYTE0 0  /* MSB byte */
#define BYTE1 1  
#define BYTE2 2
#define BYTE3 3  /* LSB byte */
#elif CMTSPEECH_LITTLE_ENDIAN_CMDS
#define BYTE0 3  /* MSB byte */
#define BYTE1 2  
#define BYTE2 1
#define BYTE3 0  /* LSB byte */
#else
#error "Endianess must be set."
#endif

START_TEST(test_speech_codec_req)
{
  cmtspeech_cmd_t cmd;
  uint8_t speech_data_stream = 1;
  uint8_t call_user_connecting_ind = 1;
  uint8_t codec_info = CMTSPEECH_CODEC_INFO_AMR_NB;
  uint8_t cellular_info = CMTSPEECH_CELLULAR_INFO_GSM;
  uint8_t sample_rate = CMTSPEECH_SAMPLE_RATE_16KHZ;
  uint8_t data_format = CMTSPEECH_DATA_FORMAT_S16LINPCM;
  
  /* test encoding-decoding */
  cmtspeech_msg_encode_speech_config_req(&cmd, speech_data_stream, call_user_connecting_ind, codec_info, cellular_info, sample_rate, data_format);

  fail_unless(cmtspeech_msg_get_type(cmd) == CMTSPEECH_SPEECH_CONFIG_REQ);
  fail_unless(cmtspeech_msg_get_domain(cmd) == CMTSPEECH_DOMAIN_CONTROL);

  sample_rate = CMTSPEECH_SAMPLE_RATE_NONE;
  cmtspeech_msg_decode_speech_config_req(cmd, NULL, NULL, NULL, NULL, &sample_rate, NULL);
  fail_unless(sample_rate == CMTSPEECH_SAMPLE_RATE_16KHZ);

  /* test with a prefilled test vector */
  /* params: DataLength=10ms, SpeechDS=1, CallUCI=0, AMR-WB, GSM, 8kHz, 16bitlinpcm */
  cmd.d.buf[BYTE0] = 0x31;
  cmd.d.buf[BYTE1] = 0x00;
  cmd.d.buf[BYTE2] = 0x2a;
  cmd.d.buf[BYTE3] = 0xd5;

  speech_data_stream = call_user_connecting_ind = codec_info = sample_rate = data_format = 255;
  cmtspeech_msg_decode_speech_config_req(cmd, &speech_data_stream, &call_user_connecting_ind, &codec_info, &cellular_info, &sample_rate, &data_format);
  fail_unless(speech_data_stream == 1);
  fail_unless(call_user_connecting_ind == 0);
  fail_unless(codec_info == CMTSPEECH_CODEC_INFO_AMR_WB);
  fail_unless(cellular_info == CMTSPEECH_CELLULAR_INFO_GSM);
  fail_unless(sample_rate == CMTSPEECH_SAMPLE_RATE_8KHZ);
}
END_TEST

START_TEST(test_timing_config_ntf)
{
  cmtspeech_cmd_t cmd;
  uint16_t msec = 500; 
  uint16_t usec = 999;

  /* test encoding-decoding */  
  cmtspeech_msg_encode_timing_config_ntf(&cmd, msec, usec);
  fail_unless(cmtspeech_msg_get_type(cmd) == CMTSPEECH_TIMING_CONFIG_NTF);
  fail_unless(cmtspeech_msg_get_domain(cmd) == CMTSPEECH_DOMAIN_CONTROL);
  msec = 0;
  usec = 0;
  cmtspeech_msg_decode_timing_config_ntf(cmd, &msec, &usec);
  fail_unless(msec == 500);
  fail_unless(usec == 999);

  msec = 0;
  usec = 0;
  cmtspeech_msg_encode_timing_config_ntf(&cmd, msec, usec);
  msec = 500;
  usec = 999;
  cmtspeech_msg_decode_timing_config_ntf(cmd, &msec, &usec);
  fail_unless(msec == 0);
  fail_unless(usec == 0);

  /* test with a prefilled test vector */
  cmd.d.buf[BYTE0] = 0x41;
  cmd.d.buf[BYTE1] = 0x06;
  cmd.d.buf[BYTE2] = 0xbf;
  cmd.d.buf[BYTE3] = 0xdb;

  msec = usec = 65535;
  cmtspeech_msg_decode_timing_config_ntf(cmd, &msec, &usec);
  fail_unless(msec == 431);
  fail_unless(usec == 987);
}
END_TEST

START_TEST(test_ssi_config_req)
{
  cmtspeech_cmd_t cmd;
  uint8_t layout = CMTSPEECH_SAMPLE_LAYOUT_SWAPPED_LE;
  uint8_t version = 2;
  uint8_t state = 1;

  /* test encoding-decoding */  
  cmtspeech_msg_encode_ssi_config_req(&cmd, layout, version, state);
  fail_unless(cmtspeech_msg_get_type(cmd) == CMTSPEECH_SSI_CONFIG_REQ);
  fail_unless(cmtspeech_msg_get_domain(cmd) == CMTSPEECH_DOMAIN_CONTROL);

  state = 0; 
  version = 0;
  layout = 0;
  fail_unless(cmtspeech_msg_decode_ssi_config_req(cmd, &layout, &version, &state) == 0);
  fail_unless(state == 1);
  fail_unless(version == 2);
  fail_unless(layout == CMTSPEECH_SAMPLE_LAYOUT_SWAPPED_LE);

  /* test with a prefilled test vector */
  cmd.d.buf[BYTE0] = 0x21;
  cmd.d.buf[BYTE1] = 0x00;
  cmd.d.buf[BYTE2] = 0x02;
  cmd.d.buf[BYTE3] = 0x00;
  cmtspeech_msg_decode_ssi_config_req(cmd, &layout, &version, &state);
  fail_unless(cmtspeech_msg_get_type(cmd) == CMTSPEECH_SSI_CONFIG_REQ);
  fail_unless(layout == CMTSPEECH_SAMPLE_LAYOUT_INORDER_LE);
}
END_TEST

START_TEST(test_ssi_config_resp)
{
  cmtspeech_cmd_t cmd;
  uint8_t layout = CMTSPEECH_SAMPLE_LAYOUT_SWAPPED_LE;
  uint8_t state = 2;

  /* test encoding-decoding */  
  cmtspeech_msg_encode_ssi_config_resp(&cmd, layout, state);
  fail_unless(cmtspeech_msg_get_type(cmd) == CMTSPEECH_SSI_CONFIG_RESP);
  fail_unless(cmtspeech_msg_get_domain(cmd) == CMTSPEECH_DOMAIN_CONTROL);

  state = 0; 
  layout = 0;
  fail_unless(cmtspeech_msg_decode_ssi_config_resp(cmd, &layout, &state) == 0);
  fail_unless(state == 2);
  fail_unless(layout == CMTSPEECH_SAMPLE_LAYOUT_SWAPPED_LE);
}
END_TEST

START_TEST(test_reset_conn_resp)
{
  cmtspeech_cmd_t cmd;

  /* test encoding-decoding */  
  cmtspeech_msg_encode_reset_conn_resp(&cmd);
  fail_unless(cmtspeech_msg_get_type(cmd) == CMTSPEECH_RESET_CONN_RESP);
  fail_unless(cmtspeech_msg_get_domain(cmd) == CMTSPEECH_DOMAIN_CONTROL);

  /* test with a prefilled test vector */
  cmd.d.buf[BYTE0] = 0x11;
  cmd.d.buf[BYTE1] = 0x00;
  cmd.d.buf[BYTE2] = 0x00;
  cmd.d.buf[BYTE3] = 0x00;
  fail_unless(cmtspeech_msg_get_type(cmd) == CMTSPEECH_RESET_CONN_RESP);
}
END_TEST

START_TEST(test_reset_conn_req)
{
  cmtspeech_cmd_t cmd;

  /* test encoding-decoding */  
  cmtspeech_msg_encode_reset_conn_req(&cmd);
  fail_unless(cmtspeech_msg_get_type(cmd) == CMTSPEECH_RESET_CONN_REQ);
  fail_unless(cmtspeech_msg_get_domain(cmd) == CMTSPEECH_DOMAIN_CONTROL);

  /* test with a prefilled test vector */
  cmd.d.buf[BYTE0] = 0x01;
  cmd.d.buf[BYTE1] = 0x00;
  cmd.d.buf[BYTE2] = 0x00;
  cmd.d.buf[BYTE3] = 0x00;
  fail_unless(cmtspeech_msg_get_type(cmd) == CMTSPEECH_RESET_CONN_REQ);
}
END_TEST

START_TEST(test_ul_data_frame)
{
  uint8_t buf[255] = { 0 };
  uint16_t frame_counter = 65535;
  uint8_t data_length = 255;
  uint8_t sample_rate = 255;
  uint8_t data_type = 255;

  /* test encoding-decoding */  
  cmtspeech_msg_encode_ul_data_header(buf, 255, 12345, CMTSPEECH_DATA_LENGTH_10MS, CMTSPEECH_SAMPLE_RATE_8KHZ, CMTSPEECH_DATA_TYPE_INVALID);
  cmtspeech_msg_decode_ul_data_header(buf, 255, &frame_counter, &data_length, &sample_rate, &data_type);
  fail_unless(frame_counter == 12345);
  fail_unless(data_length == CMTSPEECH_DATA_LENGTH_10MS);
  fail_unless(sample_rate == CMTSPEECH_SAMPLE_RATE_8KHZ);
  fail_unless(data_type == CMTSPEECH_DATA_TYPE_INVALID);

  /* test with a prefilled test vector */
  uint8_t testbuf[4];
  /* params: frame counter = 0xabcd, DataLength=20ms, SR=16kHz, valid data */
  testbuf[BYTE0] = 0xab;
  testbuf[BYTE1] = 0xcd;
  testbuf[BYTE2] = 0x00;
  testbuf[BYTE3] = 0x2a;

  frame_counter = 65535; 
  data_length = sample_rate = data_type = 255;
  cmtspeech_msg_decode_ul_data_header(testbuf, 4, &frame_counter, &data_length, &sample_rate, &data_type);
  fail_unless(frame_counter == 0xabcd);
  fail_unless(data_length == CMTSPEECH_DATA_LENGTH_20MS);
  fail_unless(sample_rate == CMTSPEECH_SAMPLE_RATE_16KHZ);
  fail_unless(data_type == CMTSPEECH_DATA_TYPE_VALID);
}
END_TEST

START_TEST(test_dl_data_frame)
{
  uint8_t buf[255] = { 0 };
  uint16_t frame_counter = 65535;
  uint8_t sbc_flags = 0;
  uint8_t data_length = 255;
  uint8_t sample_rate = 255;
  uint8_t data_type = 255;

  /* test encoding-decoding */  
  cmtspeech_msg_encode_dl_data_header(buf, 255, 12345, CMTSPEECH_SPC_FLAGS_MUTE, CMTSPEECH_DATA_LENGTH_10MS, CMTSPEECH_SAMPLE_RATE_8KHZ, CMTSPEECH_DATA_TYPE_INVALID);
  cmtspeech_msg_decode_dl_data_header(buf, 255, &frame_counter, &sbc_flags, &data_length, &sample_rate, &data_type);
  fail_unless(frame_counter == 12345);
  fail_unless(sbc_flags == CMTSPEECH_SPC_FLAGS_MUTE);
  fail_unless(data_length == CMTSPEECH_DATA_LENGTH_10MS);
  fail_unless(sample_rate == CMTSPEECH_SAMPLE_RATE_8KHZ);
  fail_unless(data_type == CMTSPEECH_DATA_TYPE_INVALID);

  /* test with a prefilled test vector */
  /* params: frame counter = 0xabcd, SPC-attenuate, DataLength=20ms,
       SR=16kHz, valid data */
  uint8_t testbuf[4];
  testbuf[BYTE0] = 0xab;
  testbuf[BYTE1] = 0xcd;
  testbuf[BYTE2] = 0x10;
  testbuf[BYTE3] = 0xa9;

  frame_counter = 65535; 
  sbc_flags = data_length = sample_rate = data_type = 255;
  cmtspeech_msg_decode_dl_data_header(testbuf, 4, &frame_counter, &sbc_flags, &data_length, &sample_rate, &data_type);
  fail_unless(frame_counter == 0xabcd);
  fail_unless(sbc_flags & CMTSPEECH_SPC_FLAGS_BFI);
  fail_unless(sbc_flags & CMTSPEECH_SPC_FLAGS_DTX_USED);
  fail_unless((sbc_flags & ~(CMTSPEECH_SPC_FLAGS_DTX_USED | CMTSPEECH_SPC_FLAGS_BFI)) == 0);
  fail_unless(data_length == CMTSPEECH_DATA_LENGTH_20MS);
  fail_unless(sample_rate == CMTSPEECH_SAMPLE_RATE_16KHZ);
  fail_unless(data_type == CMTSPEECH_DATA_TYPE_INVALID);
}
END_TEST

Suite *ssi_msgs_suite(void)
{
  Suite *suite = suite_create("ssi_msgs");
  
  TCase *control = tcase_create("control");
  TCase *data = tcase_create("data");

  tcase_add_test(control, test_speech_codec_req);
  tcase_add_test(control, test_timing_config_ntf);
  tcase_add_test(control, test_ssi_config_req);
  tcase_add_test(control, test_ssi_config_resp);
  tcase_add_test(control, test_reset_conn_req);
  tcase_add_test(control, test_reset_conn_resp);

  tcase_add_test(data, test_ul_data_frame);
  tcase_add_test(data, test_dl_data_frame);

  suite_add_tcase(suite, control);
  suite_add_tcase(suite, data);

  return suite;
}

int main(int argc, char *argv[])
{
  int nr_failed;
  Suite *suite = ssi_msgs_suite();
  SRunner *runner = srunner_create(suite);
  srunner_set_xml(runner, "/tmp/result.xml");
  srunner_run_all(runner, CK_NORMAL);
  nr_failed = srunner_ntests_failed(runner);
  srunner_free(runner);
  return (nr_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
