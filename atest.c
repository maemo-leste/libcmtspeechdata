/* -*- c-file-style: "linux" -*- */
/* Audio test */

#include "utils/audio.c"

struct test_ctx ctx0;
struct test_ctx *ctx = &ctx0;

void
duplex_test(void)
{
	while (1) {
		int len = 1024;
		char buf[len];
		long res;
		int i;

		for (i=0; i<len; i++)
			buf[i] = i*5;

		res = audio_read(ctx->source, buf, len);
		printf("read: %d, ", res);
		res = audio_write(ctx->sink, buf, res);
		printf("write: %d, ", res);
#ifdef ALSA
		printf("avail: %d", snd_pcm_avail_update(ctx->sink));
#endif
		printf("\n");
	}
}

void
volume_test(void)
{
	while (1) {
		int len = 1024;
		char buf[len];
		long res;
		int i;

		audio_generate(buf, len);
		res = audio_write(ctx->sink, buf, len);
		printf("write: %d, ", res);
#ifdef ALSA
		while (snd_pcm_avail_update(ctx->sink) < 512)
			;
		printf("avail: %d", snd_pcm_avail_update(ctx->sink));
#endif
		printf("\n");
	}
}

void
main(void)
{
#define SIZE 3300
	char buf[SIZE] = { 0, };
  audio_init(ctx);
	      printf("opening streams\n"); fflush(stdout);
	      start_sink(ctx);
	      printf("initial write: %d\n", audio_write(ctx->sink, buf, SIZE));
	      printf("sink ok\n"); fflush(stdout);	      	      
	      start_source(ctx);
#ifdef ALSA
	      printf("start: %d", snd_pcm_start(ctx->source));
	      snd_pcm_start(ctx->sink);
#endif
	      printf("streams open\n"); fflush(stdout);

	      //volume_test(); /* cca 62.5 dBA */
	      duplex_test();
}
