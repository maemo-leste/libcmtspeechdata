/* -*- linux-c -*- */

/* 
 * Standard includes
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

typedef int16_t s16;

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
  for (i = 0; i < size; i++) {
    b2[2*i] = b1[i];
    b2[2*i+1] = b1[i];
  }
}

void adjust_volume(int factor, s16 *b, int size)
{
  int i;
  for (i = 0; i < size/2; i++) {
    b[i] = b[i] * factor;
  }
}

ssize_t audio_read(int fd, void *buf, size_t count)
{
  ssize_t res = read(fd, buf, count);
  adjust_volume(10, buf, count);
  return res;
}

ssize_t audio_write(int fd, void *buf, size_t count)
{
  ssize_t res;
  adjust_volume(10, buf, count);
  res = write(fd, buf, count);
  return res;
}

#define DEVICE_NAME "/dev/dsp"

int audio_open(int speed)
{
  int audio_fd;

  printf("Opening %s, speed %d\n", DEVICE_NAME, speed);
  if ((audio_fd = open(DEVICE_NAME, O_RDWR, 0)) == -1) { /* Opening device failed */  
      perror(DEVICE_NAME);  
      exit(1);  
  } 

  {
    int devcaps;

    if (ioctl (audio_fd, SNDCTL_DSP_GETCAPS, &devcaps) == -1) {
      perror ("SNDCTL_DSP_GETCAPS");
      exit (-1);
    }
#if 0
    if (!(devcaps & PCM_CAP_DUPLEX))
      {
	fprintf (stderr,
		 "%s doesn't support one device based full duplex scheme\n",
		 dspname);
	fprintf (stderr, "Please use the two device scheme.\n");
	exit (-1);
      }
#endif
  }


  if (ioctl(audio_fd, SNDCTL_DSP_SETDUPLEX, 0) == -1) {
    perror("Can't do FD\n");
  }

  {
    //    unsigned int frag = 0x7fff000a;/* Unlimited number of 1k fragments */
    unsigned int frag = 0x7fff000c;

    if (ioctl (audio_fd, SNDCTL_DSP_SETFRAGMENT, &frag) == -1) {
      perror ("SNDCTL_DSP_SETFRAGMENT");
      exit (-1);
    }
  }

  /*
    http://www.4front-tech.com/pguide/audio.html#speed

    There are three parameters which affect quality (and memory/bandwidth
    requirements) of sampled audio data. These parameters are the
    following:


    Sample format (sometimes called as number of bits)
    Number of channels (mono/stereo)
    Sampling rate (speed)
  */

  /* AFMT_S16_LE */

  {
    int format;
    format = AFMT_S16_LE;  
    if (ioctl(audio_fd, SNDCTL_DSP_SETFMT, &format)==-1) { /* Fatal error */  
	perror("SNDCTL_DSP_SETFMT");  
	exit(2);  
      }  

    if (format != AFMT_S16_LE) {
	printf("S16LE not available\n");
	/* 
	   The device doesn't support the requested audio format. The program  
	   should use another format (for example the one returned in "format")  
	   or alternatively it must display an error message and to abort.  
	*/
      } 
  }

  {
    int stereo = 1;     /* 0=mono, 1=stereo */
    if (ioctl(audio_fd, SNDCTL_DSP_STEREO, &stereo)==-1) { /* Fatal error */
	perror("SNDCTL_DSP_STEREO");
	exit(3);
      }

    if (stereo != 1) {
      printf("The device doesn't support selected mode.\n");
    }
  }

  {
    int speed2 = speed;
    if (ioctl(audio_fd, SNDCTL_DSP_SPEED, &speed)==-1) { /* Fatal error */  
      perror("SNDCTL_DSP_SPEED");  
      exit(4);  
    }  

    if (speed != speed2) {
      printf("The device doesn't support the requested speed.\n");
    }
    printf("The sample rate is %d\n", speed);
  }

  return audio_fd;
}

static void start_source(struct test_ctx *ctx)
{
#define SSIZE 4096
  static char silence[SSIZE];
  audio_write(ctx->sink, silence, SSIZE);
}

static void start_sink(struct test_ctx *ctx)
{
}

static void stop_source(struct test_ctx *ctx)
{
}

static void stop_sink(struct test_ctx *ctx)
{
}

void audio_init(struct test_ctx *ctx)
{
  ctx->source = ctx->sink = audio_open(4000);
}

static char *audio_strerror(void)
{
  return strerror(errno);
}
