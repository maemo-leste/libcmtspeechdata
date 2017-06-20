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
	for (i = 0; i < size/4; i++) {
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

float adjust_volume(float factor, s16 *b, int size)
{
	int i;
	long v;
	long long avg = 0;
	static int overflows = 0;
	int min=99999, max=-99999; 
	for (i = 0; i < size/2; i++) {
		v = b[i] * factor;
		avg += v;
		if (v > max) max=v;
		if (v < min) min=v;
		if (v > SHRT_MAX) {
			v = SHRT_MAX;
			overflows++;
		}
		if (v < SHRT_MIN) {
			v = SHRT_MIN;
			overflows++;
		}
		b[i] = v;
	}
	avg = avg / ( size/2 );
	printf("%.4f gain, %d overflows, %6d..%d..%d\n", factor, overflows, min, (int) avg, max);
	if (-min > max)
		max = -min;
	if (max > (SHRT_MAX * 0.7))
		factor *= 0.7;
	if (max < (SHRT_MAX * 0.3))
		factor *= 1.02;
	if (factor < 1)
		factor = 1;
	if (factor > 6)
		factor = 6;
	return factor;
}

#ifdef ALSA
#define MAN_STEREO
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
	static float gain = 3;
	ssize_t res;
	if (count > SSIZE) {
		printf("Too big request\n");
		exit(1);
	}
	res = audio_read_raw(fd, sbuf, count*2);
	to_mono(sbuf, buf, res);
	gain = adjust_volume(gain, buf, count);
	return res/2;
}

ssize_t audio_write(audio_t fd, void *buf, size_t count)
{
	static float gain = 3;
	ssize_t res;
	if (count > SSIZE) {
		printf("Too big request\n");
		exit(1);
	}
	gain = adjust_volume(gain, buf, count);
	to_stereo(buf, sbuf, count);
	res = audio_write_raw(fd, sbuf, count*2);
	{
		char buf[1024];

		sprintf(buf, "call\ndriver: %s\nspeaker: %.2f\n",
			DRIVER_NAME,
			gain);
		wd_write(buf);
	}
	return res/2;
}
#else
#error Update me: auto gain support, watchdog ....
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

static char wd_buf[1024] = "\0";

char *wd_name(void)
{
	if (*wd_buf)
		return wd_buf;

	sprintf(wd_buf, "%s/herd/wd/call", getenv("HOME"));
	return wd_buf;
}

void wd_write(char *s)
{
	int f = open(wd_name(), O_WRONLY | O_TRUNC | O_CREAT, 0600);

	write(f, s, strlen(s));
	close(f);
}

void wd_done(void)
{
	wd_write("no\n");
}
