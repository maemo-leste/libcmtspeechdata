/* -*- linux-c -*- */

#define TEST
#include "utils/alsa.c"

struct test_ctx source, sink;

int main(int argc, char *argv[])
{
	snd_init();
	start_sink(&sink);
	sleep(5);
	start_source(&source);

	int i;
	for (i = 0; i < 10000; i++) {
#define LEN 1024
		char buf[LEN];
		read_bytes(&source, buf, LEN);
		write_bytes(&sink, buf, LEN);
	}
	stop_source(&source);
	stop_sink(&sink);
	return 0;
}
