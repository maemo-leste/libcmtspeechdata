/*
 * This file is part of libcmtspeechdata.
 *
 * Copyright (C) 2008,2009,2010,2011 Nokia Corporation.
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

/** @file cmtspeech_msgs.c
 *
 * Helper functions to encode and parse CMT Speech Data
 * protocol messages as specified in 'CMT Speech Data
 * SSI Protocol' document (DCU02180).
 */

#include "cmtspeech_msgs.h"

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

/**
 * Returns the type of control message in 'cmd'.
 * On success, returned type is one of CMTSPEECH_RESET_CONN,
 * CSDATA_ACK, CMTSPEECH_CONFIG_RESP, ... On error, -1 is
 * returned.
 */
int cmtspeech_msg_get_type(const cmtspeech_cmd_t cmd)
{
  return (int)cmd.d.buf[BYTE0] >> 4;
}

/**
 * On success, returns domain number. On error (invalid message),
 * returns -1.
 */
int cmtspeech_msg_get_domain(const cmtspeech_cmd_t cmd)
{
  return (int)cmd.d.buf[BYTE0] & 0xf;
}

const char* cmtspeech_msg_type_to_string(const cmtspeech_cmd_t cmd)
{
#if !defined(NDEBUG)
  int domain =
    cmtspeech_msg_get_domain(cmd);

  if (domain == CMTSPEECH_DOMAIN_CONTROL) {
    switch(cmtspeech_msg_get_type(cmd))
    {
    case CMTSPEECH_RESET_CONN_REQ: return "RESET_CONN_REQ";
    case CMTSPEECH_RESET_CONN_RESP: return "RESET_CONN_RESP";
    case CMTSPEECH_SSI_CONFIG_REQ: return "SSI_CONFIG_REQ";
    case CMTSPEECH_SPEECH_CONFIG_REQ: return "SPEECH_CONFIG_REQ";
    case CMTSPEECH_TIMING_CONFIG_NTF: return "TIMING_CONFIG_NTF";
    case CMTSPEECH_UPLINK_CONFIG_NTF: return "UPLINK_CONFIG_NTF";
    case CMTSPEECH_NEW_TIMING_CONFIG_REQ: return "NEW_TIMING_CONFIG_REQ";
    case CMTSPEECH_SSI_CONFIG_RESP: return "SSI_CONFIG_RESP";
    case CMTSPEECH_SPEECH_CONFIG_RESP: return "SPEECH_CONFIG_RESP";
    case CMTSPEECH_TEST_RAMP_PING: return "TEST_RAMP_PING";
    default: return "<unknown-control-message-type>";
    }
  }
  else if (domain == CMTSPEECH_DOMAIN_INTERNAL) {
    switch(cmtspeech_msg_get_type(cmd))
      {
      case CMTSPEECH_EVENT_RESET: return "CMTSPEECH_EVENT_RESET";
      default: return "<unknown-internal-message-type>";
      }
  }

  return "<unknown-message-type>";

#else
  return "<disabled-in-build>";
#endif
}

/**
 * Encodes an UL_SPEECH_DATA_FRAME message to buffer pointed
 * by 'buf'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_ul_data_header(uint8_t *buf, int len, uint16_t frame_counter, uint8_t data_length, uint8_t sample_rate, uint8_t data_type)
{
  if (len < 4)
    return -1;

  buf[BYTE0] = frame_counter >> 8;
  buf[BYTE1] = frame_counter;
  buf[BYTE2] = 0; /* reserved; */
  buf[BYTE3] = (data_length & 0x3) << 4 | (sample_rate & 0x3) << 2 | (data_type & 0x3);

  return 4;
}

/**
 * Decodes CMT speech message in 'buf'. Results are stored to locations
 * given as arguments.
 *
 * @return 0 on success, non-zero otherwise
 */
int cmtspeech_msg_decode_ul_data_header(uint8_t *buf, int len, uint16_t *frame_counter, uint8_t *data_length, uint8_t *sample_rate, uint8_t *data_type)
{
  if (len < 4)
    return -1;

  *frame_counter = buf[BYTE0] << 8 | buf[BYTE1];
  *data_length = (buf[BYTE3] >> 4) & 0x3;
  *sample_rate = (buf[BYTE3] >> 2) & 0x3;
  *data_type = buf[BYTE3] & 0x3;

  return 0;
}

/**
 * Encodes an DL_SPEECH_DATA_FRAME message to buffer pointed
 * by 'buf'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_dl_data_header(uint8_t *buf, int len, uint16_t frame_counter, uint8_t spc_flags, uint8_t data_length, uint8_t sample_rate, uint8_t data_type)
{
  return cmtspeech_msg_encode_dl_data_header_v5(buf,
						len,
						frame_counter,
						spc_flags,
						data_length,
						sample_rate,
						CMTSPEECH_SAMPLE_RATE_NONE,
						data_type);
}

/**
 * Encodes an DL_SPEECH_DATA_FRAME message to buffer pointed
 * by 'buf'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_dl_data_header_v5(uint8_t *buf, int len, uint16_t frame_counter, uint8_t spc_flags, uint8_t data_length, uint8_t sample_rate, uint8_t codec_sample_rate, uint8_t data_type)
{
  if (len < 4)
    return -1;

  buf[BYTE0] = frame_counter >> 8;
  buf[BYTE1] = frame_counter;
  buf[BYTE2] =
    (codec_sample_rate & 0x3) << 7 |
    ((spc_flags >> 2) & 0x1f);
  buf[BYTE3] =
    (spc_flags & 0x3) << 6 |
    (data_length & 0x3) << 4 |
    (sample_rate & 0x3) << 2 |
    (data_type & 0x3);

  return 4;
}

/**
 * Decodes CMT speech message in 'buf'. Results are stored to locations
 * given as arguments.
 *
 * @return 0 on success, non-zero otherwise
 */
int cmtspeech_msg_decode_dl_data_header(uint8_t *buf, int len, uint16_t *frame_counter, uint8_t *spc_flags, uint8_t *data_length, uint8_t *sample_rate, uint8_t *data_type)
{
  uint8_t tmp;
  return cmtspeech_msg_decode_dl_data_header_v5(buf,
						len,
						frame_counter,
						spc_flags,
						data_length,
						sample_rate,
						&tmp,
						data_type);
}

/**
 * Decodes CMT speech message in 'buf'. Results are stored to locations
 * given as arguments.
 *
 * Variant of cmtspeech_msg_decode_dl_data_header() for ABI
 * compatibility.
 *
 * @return 0 on success, non-zero otherwise
 */
int cmtspeech_msg_decode_dl_data_header_v5(uint8_t *buf, int len, uint16_t *frame_counter, uint8_t *spc_flags, uint8_t *data_length, uint8_t *sample_rate, uint8_t *codec_sample_rate, uint8_t *data_type)
{

  if (len < 4)
    return -1;

  *frame_counter = buf[BYTE0] << 8 | buf[BYTE1];
  *spc_flags =
    ((buf[BYTE2] & 0x1f) << 2) |
    ((buf[BYTE3] >> 6) & 0x3);
  *data_length = (buf[BYTE3] >> 4) & 0x3;
  *sample_rate = (buf[BYTE3] >> 2) & 0x3;
  *codec_sample_rate = (buf[BYTE2] >> 5) & 0x3;
  *data_type = buf[BYTE3] & 0x3;

  return 0;
}

/**
 * Encodes a SPEECH_CONFIG_REQ message to buffer pointed
 * by 'cmd'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_speech_config_req(cmtspeech_cmd_t *cmd, uint8_t speech_data_stream, uint8_t call_user_connecting_ind, uint8_t codec_info, uint8_t cellular_info, uint8_t sample_rate, uint8_t data_format)
{
  cmd->d.buf[BYTE0] = (CMTSPEECH_SPEECH_CONFIG_REQ << 4) | CMTSPEECH_DOMAIN_CONTROL;
  cmd->d.buf[BYTE1] = 0; /* part of reserved range */
  cmd->d.buf[BYTE2] =
    (speech_data_stream & 0x1) << 3 |
    (call_user_connecting_ind & 0x1) << 2 |
    (codec_info & 0xf) >> 2;
  cmd->d.buf[BYTE3] =
    ((codec_info & 0xf) << 6) |
    ((cellular_info & 0x3) << 4) |
    ((sample_rate & 0x3) << 2) |
    (data_format & 0x3);

  return 4;
}

/**
 * Decodes CMT speech message in 'cmd'. Results are stored to locations
 * given as arguments. If an argument is NULL, it is ignored.
 *
 * @return 0 on success, non-zero otherwise
 */
int cmtspeech_msg_decode_speech_config_req(const cmtspeech_cmd_t cmd, uint8_t *speech_data_stream, uint8_t *call_user_connecting_ind, uint8_t *codec_info, uint8_t *cellular_info, uint8_t *sample_rate, uint8_t *data_format)
{
  if (speech_data_stream)
    *speech_data_stream =
      (cmd.d.buf[BYTE2] >> 3) & 0x1;

  if (call_user_connecting_ind)
    *call_user_connecting_ind =
      (cmd.d.buf[BYTE2] >> 2) & 0x1;

  if (codec_info)
    *codec_info =
      (cmd.d.buf[BYTE2] << 2 | cmd.d.buf[BYTE3] >> 6) & 0xf;

  if (cellular_info)
    *cellular_info =
      (cmd.d.buf[BYTE3] >> 4) & 0x3;

  if (sample_rate)
    *sample_rate = (cmd.d.buf[BYTE3] >> 2) & 0x3;

  if (data_format)
    *data_format = cmd.d.buf[BYTE3] & 0x3;

  return 0;
}

/**
 * Encodes a NEW_TIMING_CONFIG_REQ message to buffer pointed
 * by 'cmd'. Returns size of encoded data (in octets).
 */
static int priv_cmtspeech_msg_encode_simple_message(cmtspeech_cmd_t *cmd, unsigned simplecmd)
{
  cmd->d.buf[BYTE0] = (simplecmd << 4) | CMTSPEECH_DOMAIN_CONTROL;
  cmd->d.buf[BYTE1] = 0;
  cmd->d.buf[BYTE2] = 0;
  cmd->d.buf[BYTE3] = 0;

  return 4;
}

/**
 * Encodes a NEW_TIMING_CONFIG_REQ message to buffer pointed
 * by 'cmd'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_new_timing_config_req(cmtspeech_cmd_t *cmd)
{
  return priv_cmtspeech_msg_encode_simple_message(cmd, CMTSPEECH_NEW_TIMING_CONFIG_REQ);
}

/**
 * Encodes a UPLINK_CONFIG_NTF message to buffer pointed
 * by 'cmd'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_uplink_config_ntf(cmtspeech_cmd_t *cmd)
{
  return priv_cmtspeech_msg_encode_simple_message(cmd, CMTSPEECH_UPLINK_CONFIG_NTF);
}

/**
 * Encodes a TIMING_CONFIG_NTF message to buffer pointed
 * by 'cmd'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_timing_config_ntf(cmtspeech_cmd_t *cmd, uint16_t msec, uint16_t usec)
{
  cmd->d.buf[BYTE0] = (CMTSPEECH_TIMING_CONFIG_NTF << 4) | CMTSPEECH_DOMAIN_CONTROL;
  cmd->d.buf[BYTE1] = (msec & 0x1ff) >> 6;
  cmd->d.buf[BYTE2] =
    ((msec & 0x1ff) << 2) |
    (usec & 0x3ff) >> 8;
  cmd->d.buf[BYTE3] = usec & 0xff;

  return 4;
}

/**
 * Decodes CMT speech message in 'cmd'. Results are stored to locations
 * given as arguments. If an argument is NULL, it is ignored.
 *
 * @return 0 on success, non-zero otherwise
 */
int cmtspeech_msg_decode_timing_config_ntf(const cmtspeech_cmd_t cmd, uint16_t *msec, uint16_t *usec)
{
  if (msec)
    *msec = (cmd.d.buf[BYTE1] & 0x7) << 6 | cmd.d.buf[BYTE2] >> 2;

  if (usec)
    *usec = (cmd.d.buf[BYTE2] & 0x3) << 8 | cmd.d.buf[BYTE3];

  return 0;
}


/**
 * Encodes a SSI_CONFIG_REQ message to buffer pointed
 * by 'cmd'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_ssi_config_req(cmtspeech_cmd_t *cmd, uint8_t layout, uint8_t version, uint8_t state)
{
  cmd->d.buf[BYTE0] = (CMTSPEECH_SSI_CONFIG_REQ << 4) | CMTSPEECH_DOMAIN_CONTROL;
  cmd->d.buf[BYTE1] = 0x0;
  cmd->d.buf[BYTE2] = layout & 0x7;
  cmd->d.buf[BYTE3] = ((version & 0xf) << 1) | (state & 0x1);

  return 4;
}

/**
 * Decodes CMT speech message in 'cmd'. Results are stored to locations
 * given as arguments. If an argument is NULL, it is ignored.
 *
 * @return 0 on success, non-zero otherwise
 */
int cmtspeech_msg_decode_ssi_config_req(const cmtspeech_cmd_t cmd, uint8_t *layout, uint8_t *version, uint8_t *state)
{
  if (version)
    *version = (cmd.d.buf[BYTE3] >> 1) & 0xf;

  if (state)
    *state = cmd.d.buf[BYTE3] & 0x1;

  if (layout)
    *layout = cmd.d.buf[BYTE2] & 0x7;

  return 0;
}

/**
 * Encodes a SSI_CONFIG_RESP message to buffer pointed
 * by 'cmd'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_ssi_config_resp(cmtspeech_cmd_t *cmd, uint8_t layout, uint8_t result)
{
  cmd->d.buf[BYTE0] = (CMTSPEECH_SSI_CONFIG_RESP << 4) | CMTSPEECH_DOMAIN_CONTROL;
  cmd->d.buf[BYTE1] = 0x0;
  cmd->d.buf[BYTE2] = layout & 0x7;
  cmd->d.buf[BYTE3] = result & 0x3;

  return 4;
}

/**
 * Decodes CMT speech message in 'cmd'. Results are stored to locations
 * given as arguments. If an argument is NULL, it is ignored.
 *
 * @return 0 on success, non-zero otherwise
 */
int cmtspeech_msg_decode_ssi_config_resp(const cmtspeech_cmd_t cmd, uint8_t *layout, uint8_t *result)
{
  if (result)
    *result = cmd.d.buf[BYTE3] & 0x3;

  if (layout)
    *layout = cmd.d.buf[BYTE2] & 0x7;

  return 0;
}

/**
 * Decodes CMT speech message in 'cmd'. Results are stored to locations
 * given as arguments. If an argument is NULL, it is ignored.
 *
 * @return 0 on success, non-zero otherwise
 */
int cmtspeech_msg_decode_speech_config_resp(const cmtspeech_cmd_t cmd, uint8_t *result)
{
  if (result)
    *result = cmd.d.buf[BYTE3] & 0x1;

  return 0;
}

/**
 * Encodes an RESET_CONN_RESP message to buffer pointed
 * by 'cmd'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_reset_conn_resp(cmtspeech_cmd_t *cmd)
{
  cmd->d.buf[BYTE0] = (CMTSPEECH_RESET_CONN_RESP << 4) | CMTSPEECH_DOMAIN_CONTROL;
  cmd->d.buf[BYTE1] = 0x0;
  cmd->d.buf[BYTE2] = 0x0;
  cmd->d.buf[BYTE3] = 0x0;

  return 4;
}

/**
 * Encodes an RESET_CONN_RESP message to buffer pointed
 * by 'cmd'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_reset_conn_req(cmtspeech_cmd_t *cmd)
{
  cmd->d.buf[BYTE0] = (CMTSPEECH_RESET_CONN_REQ << 4) | CMTSPEECH_DOMAIN_CONTROL;
  cmd->d.buf[BYTE1] = 0x0;
  cmd->d.buf[BYTE2] = 0x0;
  cmd->d.buf[BYTE3] = 0x0;

  return 4;
}

/**
 * Encodes a SPEECH_CONFIG_RESP message to buffer pointed
 * by 'cmd'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_speech_config_resp(cmtspeech_cmd_t *cmd, uint8_t result)
{
  cmd->d.buf[BYTE0] = (CMTSPEECH_SPEECH_CONFIG_RESP << 4) | CMTSPEECH_DOMAIN_CONTROL;
  cmd->d.buf[BYTE1] = 0x0;
  cmd->d.buf[BYTE2] = 0x0;
  cmd->d.buf[BYTE3] = result;

  return 4;
}

/**
 * Decodes CMT speech message in 'cmd'. Results are stored to locations
 * given as arguments. If an argument is NULL, it is ignored.
 *
 * @return 0 on success, non-zero otherwise
 */
int cmtspeech_msg_decode_test_ramp_ping(const cmtspeech_cmd_t cmd, uint8_t *domain, uint8_t *replydomain, uint8_t *rampstart, uint8_t *ramplen)
{
  if (domain)
    *domain = cmd.d.buf[BYTE0] & 0xf;

  if (replydomain)
    *replydomain = cmd.d.buf[BYTE1] & 0xf;

  if (rampstart)
    *rampstart = cmd.d.buf[BYTE2];

  if (ramplen)
    *ramplen = cmd.d.buf[BYTE3];

  return 0;
}

/**
 * Encodes a TEST_RAMP_PING message to buffer pointed
 * by 'cmd'. Returns size of encoded data (in octets).
 */
int cmtspeech_msg_encode_test_ramp_ping(cmtspeech_cmd_t *cmd, uint8_t domain, uint8_t replydomain, uint8_t rampstart, uint8_t ramplen)
{
  cmd->d.buf[BYTE0] = (CMTSPEECH_TEST_RAMP_PING << 4) | domain;
  cmd->d.buf[BYTE1] = replydomain & 0xf;
  cmd->d.buf[BYTE2] = rampstart;
  cmd->d.buf[BYTE3] = ramplen;

  return 4;

}
