#ifndef INKDAY_HTTP_JOB_H
#define INKDAY_HTTP_JOB_H

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

/* One HTTPS request executed by a curl child process. Secrets (bearer token,
   form body) are passed through a private curl config file, never via argv. */
typedef struct
{
  pid_t pid;
  FILE *output;
  char config_path[64];
} HttpJob;

enum
{
  HTTP_RUNNING = 0,
  HTTP_DONE = 1,
  HTTP_FAILED = -1
};

void http_job_init(HttpJob *job);
/* form_body is application/x-www-form-urlencoded (POST) or NULL (GET). */
int http_job_start(HttpJob *job, const char *url, const char *bearer,
                   const char *form_body, long max_bytes);
/* Non-blocking. On HTTP_DONE *body is a NUL-terminated malloc'd buffer. */
int http_job_poll(HttpJob *job, char **body);
int http_job_busy(const HttpJob *job);
/* Kill and reap the child; safe to call on an idle job. */
void http_job_cancel(HttpJob *job);

/* Append percent-encoded text (RFC 3986 unreserved kept) to a buffer. */
int url_encode(char *destination, size_t capacity, const char *text);

#endif
