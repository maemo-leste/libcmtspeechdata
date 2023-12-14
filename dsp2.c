/* -*- linux-c -*- */

struct test_ctx {
	int source;
	int sink;
};

#include "utils/dsp.c"
#include "utils/audio.h"

/*
 * Mandatory variables.
 */
int audio_fd;  
char silence[SSIZE];

int main(int argc, char *argv[]) 
{
  int speed = 4000;
  audio_fd = audio_open(speed);

  {
#define SIZE 128
    s16 buf[SIZE], buf2[SIZE];
    int count = 0;

    while (1) {
      int len = read(audio_fd, buf, SIZE);
      if (len < 0) {
	printf("Error reading DSP: %m\n");
	continue;
      }
      if (len != SIZE)
	printf("Short read: %d\n", len);
      if (!count) {
	printf("priming\n");
	write(audio_fd, silence, SSIZE);
      }
      count++;

      to_mono(buf, buf2, SIZE);
      to_stereo(buf2, buf, SIZE/2);

      {
	int delay;
	float t;
	if (ioctl(audio_fd, SNDCTL_DSP_GETODELAY, &delay) == -1)
	  delay = 0;

	delay /= 4;/* Get number of 16 bit stereo samples */

	t = (float) delay / (float) speed; /* Get delay in seconds */

	t *= 1000.0;/* Convert delay to milliseconds */

	if (len != write(audio_fd, buf, len)) {
	  printf("Short write\n");
	}

#if 1
	printf ("\rDelay=%5.3g msec", t);
	fflush (stdout);
#endif
      }
    }
  }
}
