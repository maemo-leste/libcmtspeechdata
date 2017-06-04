/* -*- c-file-style: "linux" -*- */
/* Audio test */

#include "utils/audio.c"

struct test_ctx ctx0;
struct test_ctx *ctx = &ctx0;

void
main(void)
{
  audio_init(ctx);
	      printf("opening streams\n"); fflush(stdout);	      
	      start_sink(ctx);
	      printf("sink ok\n"); fflush(stdout);	      	      
	      start_source(ctx);
#ifdef ALSA
	      printf("start: %d", snd_pcm_start(ctx->source));
#endif
	      printf("streams open\n"); fflush(stdout);
	      while (1) {
		      int len = 128;
		      char buf[len];
		      long res;
		      int i;

		      for (i=0; i<len; i++)
			      buf[i] = i*5;

		      res = audio_read(ctx->source, buf, len);
		      printf("read: %d\n", res);
		      res = audio_write(ctx->sink, buf, len);
		      printf("write: %d\n", res);		      
	      }
}
