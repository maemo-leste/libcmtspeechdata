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

ssize_t audio_read(int fd, void *buf, size_t count)
{
}

ssize_t audio_write(int fd, void *buf, size_t count)
{
}

void audio_init(struct test_ctx *ctx)
{
  
}
