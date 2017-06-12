/* -*- c-file-style: "linux" -*- */
/* Audio test */

#include "utils/audio.c"

struct test_ctx ctx0;
struct test_ctx *ctx = &ctx0;

void
volume_test(void)
{
	int f = open("/data/tmp/sink.old.raw", O_RDONLY);
	int fo = open("/data/tmp/rawplay.raw", O_WRONLY | O_CREAT, 0600);
	while (1) {
		int len = 4096;
		char buf[len];
		long res;
		int i;

		res = read(f, buf, len);
		if (res <= 0) {
			printf("error reading: %d %m\n", res);
			return;
		}
		res = audio_write(ctx->sink, buf, res);

		res = audio_read(ctx->source, buf, len);
		write(fo, buf, res);
//		printf("write: %d, ", res);
#ifdef ALSA
		while (snd_pcm_avail_update(ctx->sink) < 512)
			;
//		printf("avail: %d", snd_pcm_avail_update(ctx->sink));
#endif
//		printf("\n");
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

	      volume_test();
}
