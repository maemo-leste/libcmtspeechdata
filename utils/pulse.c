
static int pa_errno;

long audio_read(pa_simple *handle, char *buf, long len)
{
  size_t frames = len;
  int error;
  /* int pa_simple_read(pa_simple *s, void *data, size_t bytes, int *error); */
  error = pa_simple_read(handle, buf, len, &frames);
  if (error) {
    pa_errno = error;
    return -1;
  }
  return frames;
}

long audio_write(pa_simple *handle, char *buf, long len)
{
  int frames = len;
  int error;
  /* int pa_simple_write(pa_simple *s, const void *data, size_t bytes, int *error); */
  error = pa_simple_write(handle, buf, len, &frames);
  if (error) {
    pa_errno = error;
    return -1;
  }
  return len;
}

#if 0
static void flush_input(struct test_ctx *ctx)
{
  char scratch[10240];
  int total = 0;

  fprintf(stderr, "Flushing input...\n");
  if (!ctx->sink_fd)
    return;
  while(1) {
    int num;
    num = read(ctx->source_fd, scratch, 10240);
    if (num == -1)
      break;
    total += num;
  }
  fprintf(stderr, "Flushing input (%d)\n", total);
}
#endif

/* FIXME: Hmm. That makes no sense. Is it rate = 8000, channels = 1? */
/* FIXME: requesting 8000 crashes pulseaudio. */
static const pa_sample_spec ss = {
	.format = PA_SAMPLE_S16LE,
	.rate = 4000,
	.channels = 2
};
static const pa_buffer_attr pa_attr = {
	.fragsize = (uint32_t) 1024,
	.maxlength = (uint32_t) -1,
	.minreq = (uint32_t) 1024,
	.prebuf = (uint32_t) -1,
	.tlength = (uint32_t) 1024,
	/* fragsize / tlength can be 4096> pulseaudio CPU drops from 33% CPU to 10%, but latency can be heard */
};

static void start_sink(struct test_ctx *ctx)
{
	int error;
	/* The sample type to use */
	if (!(ctx->sink = pa_simple_new(NULL, "libcmtspeech_ofono", PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, &pa_attr, &error))) {
		fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
		exit(1);
	}
}

static void start_source(struct test_ctx *ctx)
{
	int error;
	/* Create the recording stream */
	if (!(ctx->source = pa_simple_new(NULL, "libcmtspeech_ofono", PA_STREAM_RECORD, NULL, "record", &ss, NULL, &pa_attr, &error))) {
		fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
		exit(1);
	}
}

static void stop_source(struct test_ctx *ctx)
{
	if (ctx->source)
		pa_simple_free(ctx->source);
	ctx->source = NULL;
}

static void stop_sink(struct test_ctx *ctx)
{
	if (ctx->sink)
		pa_simple_free(ctx->sink);
	ctx->sink = NULL;
}

void audio_init(struct test_ctx *ctx) {}

static const char *audio_strerror(void)
{
  return pa_strerror(pa_errno);
}
