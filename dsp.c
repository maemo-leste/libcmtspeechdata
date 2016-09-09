/* -*- linux-c -*- */

/* 
 * Standard includes
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/soundcard.h>

/*
 * Mandatory variables.
 */
#define BUF_SIZE    4096
int audio_fd;  
unsigned char audio_buffer[BUF_SIZE];

#define DEVICE_NAME "/dev/dsp"

int main(int argc, char *argv[]) 
{

  if ((audio_fd = open(DEVICE_NAME, O_RDWR, 0)) == -1) { /* Opening device failed */  
      perror(DEVICE_NAME);  
      exit(1);  
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
      printf("The device doesn't support stereo mode.\n");
    }
  }

  {
    int speed = 8000;
    if (ioctl(audio_fd, SNDCTL_DSP_SPEED, &speed)==-1) { /* Fatal error */  
	perror("SNDCTL_DSP_SPEED");  
	exit(4);  
      }  

    if (speed != 8000) {
      printf("The device doesn't support the requested speed.\n");
    } 
  }
}
