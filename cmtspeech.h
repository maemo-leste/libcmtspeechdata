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

/** @file cmtspeech.h
 *
 * CMT Speech Data protocol library.
 */

#ifndef INCLUDED_CMTSPEECH_H
#define INCLUDED_CMTSPEECH_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include <time.h>

#include <cmtspeech_msgs.h>
#include <cmtspeech_config.h>

/**
 * General list of TODO items:
 *
 * - relay SPC_FLAGS with downlink frames
 * - rename file to cmtspeechdata.h?
 */

/* Constants */
/* ------------------------------------------------*/

/**
 * Protocol states
 */
enum cmtspeech_state {
  CMTSPEECH_STATE_INVALID = 0,
  CMTSPEECH_STATE_DISCONNECTED,
  CMTSPEECH_STATE_CONNECTED,
  CMTSPEECH_STATE_ACTIVE_DL,
  CMTSPEECH_STATE_ACTIVE_DLUL,
  CMTSPEECH_STATE_TEST_RAMP_PING_ACTIVE
};

/**
 * Enumeration of state transitions
 */
enum cmtspeech_state_tr {
  CMTSPEECH_TR_INVALID = -1,
  CMTSPEECH_TR_0_NO_CHANGE = 0,
  CMTSPEECH_TR_1_CONNECTED = 1,
  CMTSPEECH_TR_2_DISCONNECTED = 2,
  CMTSPEECH_TR_3_DL_START = 3,
  CMTSPEECH_TR_4_DLUL_STOP = 4,
  CMTSPEECH_TR_5_PARAM_UPDATE = 5,
  CMTSPEECH_TR_6_TIMING_UPDATE = 6,
  CMTSPEECH_TR_7_TIMING_UPDATE = 7,
  CMTSPEECH_TR_10_RESET = 10,
  CMTSPEECH_TR_11_UL_STOP = 11,
  CMTSPEECH_TR_12_UL_START = 12
};

/**
 * A continuous stream of little-endian 16bit PCM
 * samples.
 */
#define CMTSPEECH_BUFFER_TYPE_PCM_S16_LE 0

/* bitmask set by cmtspeech_check_pending() */
#define CMTSPEECH_EVENT_CONTROL          (1 << 0) /**< control message available */
#define CMTSPEECH_EVENT_DL_DATA          (1 << 1) /**< DL frame available */
#define CMTSPEECH_EVENT_XRUN             (1 << 2) /**< DL overrun occured */

/* enum for 'frame_flags' field of cmtspeech_buffer_t */
#define CMTSPEECH_DATA_TYPE_ZERO         0x00
#define CMTSPEECH_DATA_TYPE_INVALID      0x01
#define CMTSPEECH_DATA_TYPE_VALID        0x02

/* bitmask for 'spc_flags' field of cmtspeech_buffer_t */
#define CMTSPEECH_SPC_FLAGS_SPEECH       (1 << 0)
#define CMTSPEECH_SPC_FLAGS_BFI          (1 << 1)
#define CMTSPEECH_SPC_FLAGS_ATTENUATE    (1 << 2)
#define CMTSPEECH_SPC_FLAGS_DEC_RESET    (1 << 3)
#define CMTSPEECH_SPC_FLAGS_MUTE         (1 << 4)
#define CMTSPEECH_SPC_FLAGS_PREV         (1 << 5)
#define CMTSPEECH_SPC_FLAGS_DTX_USED     (1 << 6)

/* Data structures
 * ----------------*/

/**
 * Frame data buffer descriptor.
 */
struct cmtspeech_buffer_s {
  int type;               /**< buffer type (CMTSPEECH_BUFFER_TYPE_*) */
  int count;              /**< octets of valid data (including header) */
  int pcount;             /**< octets of valid payload data */
  int size;               /**< octets of allocated space */
  int frame_flags;        /**< frame flags; enum CMTSPEECH_DATATYPE_* */
  unsigned int spc_flags; /**< speech codec flags for the frame;
			     for UL: always set to zero,
			     for DL: bitmask of CMTSPEECH_SPC_FLAGS_* */
  uint8_t *data;          /**< pointer to a buffer of 'size' octets */
  uint8_t *payload;       /**< pointer to frame payload */
  int index;              /**< library internal */
  int reserved[3];
};

/**
 * Typedef for cmtspeech_buffer_s
 */
typedef struct cmtspeech_buffer_s cmtspeech_buffer_t;

/**
 * Protocol state change event
 */
struct cmtspeech_event_s {

  int state;             /**< new state (CMTSPEECH_STATE_*) */
  int prev_state;        /**< previous state */

  int msg_type;          /**< type of the message that caused
			      the state change */

  int reserved;          /**< reserved */

  /**
   * A copy of the protocol message that caused
   * the state change (matching 'msg_type' field).
   */
  union {

    /* msg_type == CMTSPEECH_SSI_CONFIG_RESP: */
    struct {
      uint8_t layout;
      uint8_t version;       /**< deprecated in 1.6.1 */
      uint8_t result;
    } ssi_config_resp;

    /* msg_type == CMTSPEECH_SPEECH_CONFIG_REQ: */
    struct {
      uint8_t speech_data_stream;
      uint8_t call_user_connect_ind;
      uint8_t codec_info;    /**< deprecated in 1.5.3 */
      uint8_t cellular_info; /**< deprecated in 1.5.3 */
      uint8_t sample_rate;
      uint8_t data_format;
      bool layout_changed;   /**< if true, acquired dl/ul buffers
			          are invalid and need to be released */
    } speech_config_req;

    /* msg_type == CMTSPEECH_TIMING_CONFIG_NTF: */
    struct {
      uint16_t msec;
      uint16_t usec;
      struct timespec tstamp; /**< CLOCK_MONOTONIC timestamp when
			           the message was received by
			           HW driver */
    } timing_config_ntf;

    /* msg_type == CMTSPEECH_EVENT_RESET: */
    struct {
      int cmt_sent_req;  /**< whether CMT was the initiator */
    } reset_done;

    /* reserved / padding */
    struct {
      int reserved[4];
    } reserved;

  } msg;
};

/**
 * Typedef for cmtspeech_event_s
 */
typedef struct cmtspeech_event_s cmtspeech_event_t;

typedef void cmtspeech_t;

/* Interfaces: Core I/O
 * -------------------- */

/**
 * Initializes the libcmtspeechdata for use. Should be
 * called once when application is started.
 */
void cmtspeech_init(void);

/**
 * Opens the CMT Speech Data connection to the modem.
 *
 * @return Instance pointer is returned. On error,
 *         NULL is returned.
 */
cmtspeech_t* cmtspeech_open(void);

/**
 * Closes CMT Speech Data connection.
 *
 * @return Zero on success, a negative error code otherwise.
 */
int cmtspeech_close(cmtspeech_t *context);

/**
 * Returns a file descriptor that can be used to wait (with
 * poll()/select()) for events.
 *
 * When there is readable data, client should call
 * cmtspeech_check_events() function.
 *
 * @return a file descriptor
 */
int cmtspeech_descriptor(cmtspeech_t *context);

/**
 * Checks what events are available.
 *
 * Should be called when cmtspeech_descriptor() becomes readable.
 *
 * @param context self-pointer
 * @param flags bitmask of pending events (e.g.
 *        CMTSPEECH_EVENT_CONTROL, CMTSPEECH_DL_DATA_AVAIL, ...)
 *
 * @return 1 if events available, 0 if not, or a negative error code
 */
int cmtspeech_check_pending(cmtspeech_t *context, int *flags);

/**
 * Reads a control event and stores it to 'event'.
 *
 * This function should be called when cmtspeech_check_pending()
 * returns flag CMTSPEECH_EVENT_CONTROL.
 *
 * @return 0 on success
 */
int cmtspeech_read_event(cmtspeech_t *context, cmtspeech_event_t *event);

/* Interfaces: Event parsing
 * ------------------------- */

/**
 * Returns the matching state transition for for 'event'.
 *
 * In case either 'context' or 'event' is invalid,
 * CMTSPEECH_TR_INVALID is returned.
 *
 * This function is a helper function and its use is not
 * mandatory.
 *
 * @return enum cmtspeech_state_tr
 */
int cmtspeech_event_to_state_transition(const cmtspeech_t *context, const cmtspeech_event_t *event);

/* Interfaces: Runtime configuration
 * --------------------------------- */

/**
 * Sets preference for wideband mode
 *
 * If set to enabled, application is assumed to prefer sending and
 * receiving wideband (i.e. 16kHz sampling rate) speech frames. If
 * disabled, library assumes only 8kHz sampling rate is supported
 * and/or preferred.
 *
 * This setting will be passed to the modem upon session setup.
 * The final selection of sampling rate will however be done by
 * the modem. It is thus possible that modem will send
 * a CMTSPEECH_SPEECH_CONFIG_REQ event with sampling rate of 16kHz
 * even though application has disabled wideband support with
 * this method.
 *
 * The sampling rate used to interface with the modem does not
 * necessarily match the codec sampling rate used on the radio
 * interface. In other words the modem may resample the voice
 * frames to match the sampling rate used on modem<->libcmtspeechdata
 * link.
 *
 * By default, wideband mode is disabled after initial cmtspeech_open().
 *
 * This function can only be called when there is no active
 * session.
 *
 * @pre cmtspeech_is_ssi_connection_enabled(context) != true
 *
 * @return 0 on success
 */
int cmtspeech_set_wb_preference(cmtspeech_t *context, bool enabled);

/* Interfaces: State management
 * ---------------------------- */

/**
 * Returns the current protocol state.
 *
 * @return one of CMTSPEECH_STATE_*
 */
int cmtspeech_protocol_state(cmtspeech_t* context);

/**
 * Is SSI connection enabled?
 *
 * The connection is enabled when a succesful SSI_CONFIG_REQ/_RESP
 * transaction has completed.
 *
 * @return boolean answer
 */
bool cmtspeech_is_ssi_connection_enabled(cmtspeech_t *context);

/**
 * Is speech data stream active?
 *
 * The speech frame data stream is active when call parameters
 * have been delivered and either downlink, or both downlink and
 * uplink, frames are being sent.
 *
 * @return boolean answer
 */
bool cmtspeech_is_active(cmtspeech_t *context);

/**
 * Signals a change in call signaling server status.
 *
 * Note: this reflects the status of call signaling and is not
 * same as cmtspeech protocol state.
 *
 * @param state true call signaling instance has been started
 * @param state false call signaling instance has terminated
 */
int cmtspeech_state_change_call_status(cmtspeech_t *context, bool state);

/**
 * Signals a change in call connected status.
 *
 * Note: this reflects the status of call signaling and is not
 * same as cmtspeech protocol state.
 *
 * @param state true call has been connected
 * @param state false call has been disconnected
 */
int cmtspeech_state_change_call_connect(cmtspeech_t *context, bool state);

/**
 * Signals an error. Protocol state should be reseted.
 *
 * @param state true call has been connected
 * @param state false call has been disconnected
 *
 */
int cmtspeech_state_change_error(cmtspeech_t *context);

/* Interfaces: Buffer management
 * -------------------------------------- */

/**
 * Acquires the next uplink buffer slot. The buffer should be
 * filled with uplink data to be sent.
 *
 * Buffer can be acquired when data stream is active (i.e.
 * cmtspeech_is_active() is true). Note that it is possible
 * to acquire buffers already in CMTSPEECH_STATE_ACTIVE_DL
 * state.
 *
 * Following buffer properties are set by the library: 'type',
 * 'count', 'pcount', 'size', 'data' and 'payload'. Client should
 * copy payload contents to buffer pointed by 'payload', and
 * if needed, update the flags in 'frame_flags'. See documentation
 * of cmtspeech_buffer_t for more information about the struct
 * fields.
 *
 * @pre cmtspeech_is_active() == true
 *
 * @return 0 on success, other a negative error code:
 *    -EINVAL:  Invalid paramers or state, buffer not acquired.
 *    -ENOBUFS: No free uplink buffers, buffer not acquired.
 */
int cmtspeech_ul_buffer_acquire(cmtspeech_t *context, cmtspeech_buffer_t **buf);

/**
 * Releases a previously acquired uplink buffer slot back to the
 * library. The buffer will be sent immediately to the network, so
 * the caller must release the buffer in sync with network timing.
 * The timing parameters are delivered as events ('msg_type'
 * CMTSPEECH_TIMING_CONFIG_NTF).
 *
 * Client should update the following buffer properties: contents of
 * buffer pointed to by 'payload', and 'frame_flags'.
 *
 * @return 0 on success, otherwise a negative error code:
 *    -EBUSY:  Connection currently busy, frame not sent. Application
 *             should acquire a new buffer and retry sending later.
 *    -EINVAL: Invalid paramers or state, frame not sent.
 *    -EIO:    I/O error, frame not sent.
 *    -EPIPE:  Protocol state has changed since the buffer was
 *             acquired, frame not sent.
 */
int cmtspeech_ul_buffer_release(cmtspeech_t *context, cmtspeech_buffer_t *buf);

/**
 * Acquires the next downlink buffer slot. The buffer contains
 * the next available received frame of data.
 *
 * All the buffer properties are set according to the received
 * downlink frame.
 *
 * Note: Application must not write to the buffer memory,
 * i.e. 'buf->data' and 'buf->payload'.
 *
 * @return 0 on success, otherwise a negative error code:
 *    -EINVAL: Invalid paramers or state, buffer not acquired.
 *    -ENODATA: No downlink slot available, buffer not acquired.
 */
int cmtspeech_dl_buffer_acquire(cmtspeech_t *context, cmtspeech_buffer_t **buf);

/**
 * Releases a previously acquired downlink buffer slot back to the library.
 *
 * @return 0 on success, otherwise a negative error code
 *    -ENOENT: The buffer given as argument does not match any
 *             existing acquired buffer.
 *    -EINVAL: Invalid parameters or state.
 *    -EPIPE:  An underrun has occured because the buffer was released
 *             too late.
 */
int cmtspeech_dl_buffer_release(cmtspeech_t *context, cmtspeech_buffer_t *buf);

/**
 * Returns the codec sample rate for the buffer.
 *
 * The value reflects the original codec sample rate as it was
 * received from, or will be sent to, the radio interface. The value
 * may be different from value of cmtspeech_buffer_sample_rate(),
 * e.g. in the case where modem does decoding and resamples
 * the resulting PCM buffers.
 *
 * @return
 *   CMTSPEECH_SAMPLE_RATE_NONE: if information not available,
 *   CMTSPEECH_SAMPLE_RATE_8KHZ: 8kHz (narrow band),
 *   CMTSPEECH_SAMPLE_RATE_16KHZ: 16kHz (wide band)
 *
 * @see cmtspeech_dl_sample_rate().
 */
int cmtspeech_buffer_codec_sample_rate(cmtspeech_buffer_t *context);

/**
 * Returns the sample rate for the buffer.
 *
 * @return
 *   CMTSPEECH_SAMPLE_RATE_NONE: if information not available,
 *   CMTSPEECH_SAMPLE_RATE_8KHZ: 8kHz (narrow band),
 *   CMTSPEECH_SAMPLE_RATE_16KHZ: 16kHz (wide band)
 *
 * @see cmtspeech_dl_codec_sample_rate().
 */
int cmtspeech_buffer_sample_rate(cmtspeech_buffer_t *context);

/**
 * Returns the buffer descriptor pointing to raw downlink
 * frame at 'data'.
 *
 * @see cmtspeech_dl_buffer_release().
 */
cmtspeech_buffer_t *cmtspeech_dl_buffer_find_with_data(cmtspeech_t *context, uint8_t *data);

/**
 * Returns the buffer descriptor pointing to raw downlink
 * frame at 'data'.
 *
 * @see cmtspeech_dl_buffer_release().
 */
cmtspeech_buffer_t *cmtspeech_dl_buffer_find_with_data(cmtspeech_t *context, uint8_t *data);

/* Interfaces: Implementation version information
 * ----------------------------------------------------  */

/**
 * Returns the library implementation version as
 * a NULL-terminated string.
 */
const char* cmtspeech_version_str(void);

/**
 * Returns the implemented CMT Speech Data protocol version.
 *
 * Note that implementation may also provide backwards
 * compatibility for older versions.
 */
int cmtspeech_protocol_version(void);

/* Interfaces: Access to backend specific functionality
 * ----------------------------------------------------  */

/**
 * Returns a string identifying the library backend implementation.
 *
 * Backend name can be used to verify which set of custom messages
 * can be sent (@see cmtspeech_backend_name()).
 */
const char* cmtspeech_backend_name(cmtspeech_t* context);

/**
 * Passes a custom message to the library backend implementation.
 *
 * @param @type a typedef identifying which the message type (backend
 *        specific, @see cmtspeech_backend_name())
 * @param args variable number of arguments
 *
 * @return 0 on success, otherwise a negative error code
 */
int cmtspeech_backend_message(cmtspeech_t *self, int type, int args, ...);

/* Interfaces: Low level message handling
 * -------------------------------------- */

/**
 * Sends the NEW_TIMING_CONFIG_REQ message to CMT.
 *
 * @return 0 on success
 */
int cmtspeech_send_timing_request(cmtspeech_t *context);

/**
 * Sends the SSI_CONFIG_REQ message to CMT.
 *
 * @param state enabled or disabled
 *
 * @return 0 on success
 */
int cmtspeech_send_ssi_config_request(cmtspeech_t *context, bool active);

/* Interfaces: Test interface
 * -------------------------- */

/**
 * Initiates a test sequence where the peer is requested to send
 * back a test data frame.
 *
 * The response frame payload should contain a data ramp, starting
 * from value 'rampstart', and incremented by one in every octet.
 * The payload length should be 'ramplen' 32bit words.
 *
 * Note: this is a test interface that may not be supported by
 * all library backends.
 *
 * @return 0 on sucess, -1 on error
 */
int cmtspeech_test_data_ramp_req(cmtspeech_t *context, uint8_t rampstart, uint8_t ramplen);

/* Interfaces: Trace messages
 * -------------------------- */

typedef void (*cmtspeech_trace_handler_t)(int priority, const char *message, va_list args);

#define CMTSPEECH_TRACE_ERROR           0
#define CMTSPEECH_TRACE_INFO            3
#define CMTSPEECH_TRACE_STATE_CHANGE    4
#define CMTSPEECH_TRACE_IO              5
#define CMTSPEECH_TRACE_DEBUG           8
#define CMTSPEECH_TRACE_INTERNAL       16

/**
 * Enables or disables trace message of a given priority level.
 *
 * Note, some traces may be disabled at build time, and thus
 * some traces may be unavailable even if enabled with this
 * function.
 *
 * @param priority one of CMTSPEECH_TRACE_*
 * @param enabled if true, traces are allowed through
 */
void cmtspeech_trace_toggle(int priority, bool enabled);

/**
 * Sets the function to call when the library emits a trace
 * message.
 *
 * @param func handler function, or NULL to reset back to default
 *             handler
 */
int cmtspeech_set_trace_handler(cmtspeech_trace_handler_t func);

#endif /* INCLUDED_CMTSPEECH_H */
