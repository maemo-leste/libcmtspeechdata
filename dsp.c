/* -*- linux-c -*- */

/* 
 * Standard includes
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Mandatory variables.
 */
#define SSIZE 10240
int audio_fd;  
char silence[SSIZE];

#define DEVICE_NAME "/dev/dsp"

int main(int argc, char *argv[]) 
{

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

  int speed = 8000;
    if (ioctl(audio_fd, SNDCTL_DSP_SPEED, &speed)==-1) { /* Fatal error */  
	perror("SNDCTL_DSP_SPEED");  
	exit(4);  
      }  

    if (speed != 8000) {
      printf("The device doesn't support the requested speed.\n");
    }
    printf("The sample rate is %d\n", speed);
    

  {
#define SIZE 1024
    char buf[SIZE];
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
	printf ("\rDelay=%5.3g msec\n", t);
	fflush (stdout);
#endif
      }
    }
  }
}
