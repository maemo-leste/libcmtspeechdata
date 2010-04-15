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

/** @file cmtspeech_msgs.h
 *
 * Helper functions to encode and parse CMT Speech
 * Data protocol messages.
 */

#ifndef INCLUDED_CMTSPEECH_MSGS_H
#define INCLUDED_CMTSPEECH_MSGS_H

#include <stdint.h>
#include <stddef.h>

/* Build-time configuration */
/* -------------------------*/

/* #define CMTSPEECH_BIG_ENDIAN_CMDS 1  */
#define CMTSPEECH_LITTLE_ENDIAN_CMDS 1

/* CMT Speech Data protocol control message types  */
/* ------------------------------------------------*/

#define CMTSPEECH_RESET_CONN_REQ          0x00
#define CMTSPEECH_RESET_CONN_RESP         0x01
#define CMTSPEECH_SSI_CONFIG_REQ          0x02
#define CMTSPEECH_SPEECH_CONFIG_REQ       0x03
#define CMTSPEECH_TIMING_CONFIG_NTF       0x04
#define CMTSPEECH_NEW_TIMING_CONFIG_REQ   0x05
#define CMTSPEECH_SSI_CONFIG_RESP         0x06
#define CMTSPEECH_SPEECH_CONFIG_RESP      0x07
#define CMTSPEECH_UPLINK_CONFIG_NTF       0x08
#define CMTSPEECH_TEST_RAMP_PING          0x0c

/* Library event types (must not overlap with control messages)  */
/* ------------------------------------------------*/

#define CMTSPEECH_EVENT_STATE_CHANGE      0xff01
#define CMTSPEECH_EVENT_ERROR             0xff02
#define CMTSPEECH_EVENT_RESET             0xff03

/* Header and message sizes */
/* -------------------------*/

#define CMTSPEECH_CTRL_LEN                4 /* command command length, in octets */
#define CMTSPEECH_DATA_HEADER_LEN         4 /* data frame header length, octets */ 

/* Header fields for control and data frames */
/* ------------------------------------------*/

#define CMTSPEECH_DOMAIN_INTERNAL        0x00
#define CMTSPEECH_DOMAIN_CONTROL         0x01
#define CMTSPEECH_DOMAIN_DATA            0x02

#define CMTSPEECH_SAMPLE_LAYOUT_NO_PREF     0x00
#define CMTSPEECH_SAMPLE_LAYOUT_SWAPPED_LE  0x01
#define CMTSPEECH_SAMPLE_LAYOUT_INORDER_LE  0x02

#define CMTSPEECH_CODEC_INFO_NONE         0
#define CMTSPEECH_CODEC_INFO_GSM_FR       1
#define CMTSPEECH_CODEC_INFO_GSM_EFR      2
#define CMTSPEECH_CODEC_INFO_AMR_NB       3
#define CMTSPEECH_CODEC_INFO_GSM_HR       6
#define CMTSPEECH_CODEC_INFO_AMR_WB       11

#define CMTSPEECH_CELLULAR_INFO_NONE      0x00
#define CMTSPEECH_CELLULAR_INFO_GSM       0x01
#define CMTSPEECH_CELLULAR_INFO_WCDMA     0x02

#define CMTSPEECH_SAMPLE_RATE_NONE        0x00
#define CMTSPEECH_SAMPLE_RATE_8KHZ        0x01
#define CMTSPEECH_SAMPLE_RATE_16KHZ       0x02

#define CMTSPEECH_DATA_FORMAT_NONE        0x00
#define CMTSPEECH_DATA_FORMAT_S16LINPCM   0x01

#define CMTSPEECH_DATA_LENGTH_NONE        0x00
#define CMTSPEECH_DATA_LENGTH_10MS        0x01
#define CMTSPEECH_DATA_LENGTH_20MS        0x02

#define CMTSPEECH_SSI_CONFIG_RES_SUCCESS           0x00
#define CMTSPEECH_SSI_CONFIG_RES_GENERAL_ERROR     0x01
#define CMTSPEECH_SSI_CONFIG_RES_UNSUPPORTED_PROTO 0x02

/* Type definitions */
/* -----------------*/

struct cmtspeech_cmd_s {
  union {
    uint8_t buf[CMTSPEECH_CTRL_LEN];
    uint32_t cmd;    /* in big-endian byte-order */
  } d;
};
typedef struct cmtspeech_cmd_s cmtspeech_cmd_t;

/* Function prototypes / common */
/* -----------------------------*/

int cmtspeech_msg_get_type(const cmtspeech_cmd_t cmd);
int cmtspeech_msg_get_domain(const cmtspeech_cmd_t cmd);

/* Function prototypes / tracing */
/* ------------------------------*/

const char* cmtspeech_msg_type_to_string(const cmtspeech_cmd_t cmd);

/* Function prototypes / data messages */
/* ------------------------------------*/

int cmtspeech_msg_encode_ul_data_header(uint8_t *buf, int len, uint16_t frame_counter, uint8_t data_length, uint8_t sample_rate, uint8_t data_type);
int cmtspeech_msg_decode_ul_data_header(uint8_t *buf, int len, uint16_t *frame_counter, uint8_t *data_length, uint8_t *sample_rate, uint8_t *data_type);
int cmtspeech_msg_encode_dl_data_header(uint8_t *buf, int len, uint16_t frame_counter, uint8_t spc_flags, uint8_t data_length, uint8_t sample_rate, uint8_t data_type);
int cmtspeech_msg_decode_dl_data_header(uint8_t *buf, int len, uint16_t *frame_counter, uint8_t *spc_flags, uint8_t *data_length, uint8_t *sample_rate, uint8_t *data_type);

/* Function prototypes / control messages */
/* ---------------------------------------*/

int cmtspeech_msg_encode_reset_conn_resp(cmtspeech_cmd_t *cmd);
int cmtspeech_msg_encode_reset_conn_req(cmtspeech_cmd_t *cmd);

int cmtspeech_msg_encode_ssi_config_req(cmtspeech_cmd_t *cmd, uint8_t layout, uint8_t version, uint8_t state);
int cmtspeech_msg_decode_ssi_config_req(const cmtspeech_cmd_t cmd, uint8_t *layout, uint8_t *version, uint8_t *state);

int cmtspeech_msg_encode_ssi_config_resp(cmtspeech_cmd_t *cmd, uint8_t layout, uint8_t result);
int cmtspeech_msg_decode_ssi_config_resp(const cmtspeech_cmd_t cmd, uint8_t *layout, uint8_t *result);

int cmtspeech_msg_encode_speech_config_req(cmtspeech_cmd_t *cmd, uint8_t speech_data_stream, uint8_t call_user_connecting_ind, uint8_t codec_info, uint8_t cellular_info, uint8_t sample_rate, uint8_t data_format);
int cmtspeech_msg_decode_speech_config_req(const cmtspeech_cmd_t cmd, uint8_t *speech_data_stream, uint8_t *call_user_connecting_ind, uint8_t *codec_info, uint8_t *cellular_info, uint8_t *sample_rate, uint8_t *data_format);

int cmtspeech_msg_encode_timing_config_ntf(cmtspeech_cmd_t *cmd, uint16_t msec, uint16_t usec);
int cmtspeech_msg_decode_timing_config_ntf(const cmtspeech_cmd_t cmd, uint16_t *msec, uint16_t *usec);

int cmtspeech_msg_encode_new_timing_config_req(cmtspeech_cmd_t *cmd);

int cmtspeech_msg_decode_speech_config_resp(const cmtspeech_cmd_t cmd, uint8_t *result);
int cmtspeech_msg_encode_speech_config_resp(cmtspeech_cmd_t *cmd, uint8_t result);

int cmtspeech_msg_encode_uplink_config_ntf(cmtspeech_cmd_t *cmd);

int cmtspeech_msg_decode_test_ramp_ping(const cmtspeech_cmd_t cmd, uint8_t *domain, uint8_t *replydomain, uint8_t *rampstart, uint8_t *ramplen);
int cmtspeech_msg_encode_test_ramp_ping(cmtspeech_cmd_t *cmd, uint8_t domain, uint8_t replydomain, uint8_t rampstart, uint8_t ramplen);

#endif /* INCLUDED_CMTSPEECH_MSGS_H */
