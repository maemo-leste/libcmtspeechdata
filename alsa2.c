/* -*- linux-c -*- */

#define TEST
#include "utils/alsa.c"

struct test_ctx ctx;

#define LEN 1024

int main(int argc, char *argv[])
{
	char buf[LEN];
	int j;
	
	snd_init();
	start_sink(&ctx);
	sleep(2);
	start_source(&ctx);

	int i;

	snd_pcm_start(ctx.sink);	
		
//	printf("Preparing latency %d, writing %d\n", latency, LEN);
	for (i=0; i<LEN; i++) {
		buf[i] = i*30;
	}
	j = write_bytes(&ctx, buf, LEN);
	j = write_bytes(&ctx, buf, LEN);
	snd_pcm_start(ctx.source);
	
	for (i = 0; i < 10000; i++) {
		char buf[LEN];
		int j;
		j = read_bytes(&ctx, buf, LEN);
		if (j != LEN)
			printf("read: only %d bytes\n");
		j = write_bytes(&ctx, buf, LEN);
		if (j != LEN)
			printf("write: only %d bytes\n");
		
		usleep(100000);
	}
	stop_source(&ctx);
	stop_sink(&ctx);
	return 0;
}
