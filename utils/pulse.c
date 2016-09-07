
long readbuf(pa_simple *handle, char *buf, long len, size_t *frames, size_t *max)
{
  /* int pa_simple_read(pa_simple *s, void *data, size_t bytes, int *error); */
  return pa_simple_read(handle, buf, len, frames);
}

long writebuf(pa_simple *handle, char *buf, long len, size_t *frames)
{
  /* int pa_simple_write(pa_simple *s, const void *data, size_t bytes, int *error); */
  return pa_simple_write(handle, buf, len, frames);
}

void snd_init(void) {}
