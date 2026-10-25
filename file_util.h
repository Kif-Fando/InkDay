#ifndef INKDAY_FILE_UTIL_H
#define INKDAY_FILE_UTIL_H

#include <stddef.h>

/* Replace path atomically (temporary file in the same directory, fsync, rename).
   The file is created with mode 0600. */
int file_write_atomic(const char *path, const char *text);
/* Read a whole text file into a malloc'd NUL-terminated buffer of at most
   max_bytes. Returns NULL on error; *missing is set when the file is absent. */
char *file_read_text(const char *path, size_t max_bytes, int *missing);

#endif
