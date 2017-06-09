/* -*- c-file-style: "linux" -*- */

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

typedef int16_t s16;

#ifdef ALSA
#include <alsa/asoundlib.h>
typedef snd_pcm_t * audio_t;
#endif
#ifdef PULSE
#include <pulse/simple.h>
#include <pulse/error.h>
typedef pa_simple * audio_t;
#endif	
#ifdef DSP
typedef int audio_t;
#endif
#ifndef PULSE
typedef long long pa_usec_t;
#endif

struct test_ctx {
#ifdef CMT_REAL
	DBusConnection* dbus_conn;
	int dbus_fd;
	DBusWatch *dbus_watch;
	bool call_server_status;
	int verbose;
	cmtspeech_t *cmtspeech;
#endif
	audio_t source;
	audio_t sink;
#ifdef CMT_REAL
	int latency;
	int data_through;

	int source_cc, sink_cc;
#endif
};

void to_mono(s16 *b1, s16 *b2, int size)
{
	int i;
	for (i = 0; i < size/2; i++) {
		b2[i] = b1[2*i] + b1[2*i+1];
	}
}

void to_stereo(s16 *b1, s16 *b2, int size)
{
	int i;
	for (i = 0; i < size/2; i++) {
		b2[2*i] = b1[i];
		b2[2*i+1] = b1[i];
	}
}

void adjust_volume(int factor, s16 *b, int size)
{
	int i;
	long v;
	for (i = 0; i < size/2; i++) {
		v = b[i] * factor;
		if (v > SHRT_MAX)
			v = SHRT_MAX;
		if (v < SHRT_MIN)
			v = SHRT_MIN;
		b[i] = v;
	}
}

#ifdef ALSA
//#define MAN_STEREO
#include "alsa.c"
#endif
#ifdef PULSE
#include "pulse.c"
#endif
#ifdef DSP
#include "dsp.c"
#endif

#define SSIZE (16*1024)
char sbuf[SSIZE*2];

#ifdef MAN_STEREO
ssize_t audio_read(audio_t fd, void *buf, size_t count)
{
	ssize_t res;
	if (count > SSIZE) {
		printf("Too big request\n");
		exit(1);
	}
	res = audio_read_raw(fd, sbuf, count*2);
	to_mono(sbuf, buf, res);
	adjust_volume(1, buf, count);
	return res;
}

ssize_t audio_write(audio_t fd, void *buf, size_t count)
{
	ssize_t res;
	if (count > SSIZE) {
		printf("Too big request\n");
		exit(1);
	}
	adjust_volume(1, buf, count);
	to_stereo(buf, sbuf, count);
	res = audio_write_raw(fd, sbuf, count*2);
	return res;
}
#else
ssize_t audio_read(audio_t fd, void *buf, size_t count)
{
	ssize_t res = audio_read_raw(fd, buf, count);
	adjust_volume(1, buf, count);
	return res;
}

ssize_t audio_write(audio_t fd, void *buf, size_t count)
{
	ssize_t res;
	adjust_volume(1, buf, count);
	res = audio_write_raw(fd, buf, count);
	return res;
}
#endif

ssize_t audio_generate(s16 *buf, size_t count)
{
	int i;
	for (i = 0; i < count/2; i++) {
		buf[i] = sin((i * 3.1415) / 16.0) * 32700;
	}
}
