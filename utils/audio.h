#define SSIZE 4096

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

ssize_t audio_write(audio_t fd, void *buf, size_t count);
