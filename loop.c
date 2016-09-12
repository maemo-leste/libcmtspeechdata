/* -*- linux-c -*- */

#define TEST
#include "utils/audio.c"

struct test_ctx ctx;

#define LEN 1024

int main(int argc, char *argv[])
{
	audio_init(&ctx);
	start_sink(&ctx);
	sleep(2);
	start_source(&ctx);

	int i;

#if 1
	{
	char buf[LEN];
	int j;
	

//	printf("Preparing latency %d, writing %d\n", latency, LEN);
	for (i=0; i<LEN; i++) {
		buf[i] = i*30;
	}
	j = audio_write(ctx.sink, buf, LEN);
	if (j != LEN)
		printf("write: only %d bytes\n", j);
	j = audio_write(ctx.sink, buf, LEN);
	if (j != LEN)
		printf("write: only %d bytes\n", j);
	}
#endif
	
	snd_pcm_start(ctx.source);
	
	for (i = 0; i < 10000; i++) {
		char buf[LEN];
		int j;
		j = audio_read(ctx.source, buf, LEN);
		if (j != LEN)
		  	printf("read: only %d bytes\n", j);
		if (i == 0) {
		  audio_write(ctx.sink, buf, LEN);
		}
		j = audio_write(ctx.sink, buf, LEN);
		if (j != LEN)
		  	printf("write: only %d bytes\n", j);
		
		usleep(100000);
	}
	stop_source(&ctx);
	stop_sink(&ctx);
	return 0;
}
