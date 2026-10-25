#include "file_util.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int file_write_atomic(const char *path, const char *text)
{
  char temporary[256];
  if (snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path) >= (int)sizeof(temporary))
  {
    fprintf(stderr, "File: path is too long: %s\n", path);
    return 0;
  }
  const int fd = mkstemp(temporary);
  if (fd < 0)
  {
    fprintf(stderr, "File: cannot create temporary file for %s: %s\n", path, strerror(errno));
    return 0;
  }
  FILE *file = fdopen(fd, "wb");
  if (file == NULL)
  {
    perror("File: fdopen");
    close(fd);
    unlink(temporary);
    return 0;
  }
  const size_t length = strlen(text);
  int success = fwrite(text, 1, length, file) == length && fflush(file) == 0 && fsync(fd) == 0;
  if (fclose(file) != 0)
  {
    success = 0;
  }
  if (success && rename(temporary, path) != 0)
  {
    success = 0;
  }
  if (!success)
  {
    fprintf(stderr, "File: cannot write %s: %s\n", path, strerror(errno));
    unlink(temporary);
  }
  return success;
}

char *file_read_text(const char *path, size_t max_bytes, int *missing)
{
  *missing = 0;
  FILE *file = fopen(path, "rb");
  if (file == NULL)
  {
    if (errno == ENOENT)
    {
      *missing = 1;
    }
    else
    {
      fprintf(stderr, "File: cannot open %s: %s\n", path, strerror(errno));
    }
    return NULL;
  }
  char *text = malloc(max_bytes + 1);
  if (text == NULL)
  {
    fclose(file);
    return NULL;
  }
  const size_t length = fread(text, 1, max_bytes + 1, file);
  const int failed = ferror(file);
  fclose(file);
  if (failed || length > max_bytes || memchr(text, '\0', length) != NULL)
  {
    fprintf(stderr, "File: %s is unreadable or oversized\n", path);
    free(text);
    return NULL;
  }
  text[length] = '\0';
  return text;
}
