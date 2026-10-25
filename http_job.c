#include "http_job.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void http_job_init(HttpJob *job)
{
  job->pid = -1;
  job->output = NULL;
  job->config_path[0] = '\0';
}

int url_encode(char *destination, size_t capacity, const char *text)
{
  static const char hex[] = "0123456789ABCDEF";
  size_t used = 0;

  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; ++p)
  {
    const int plain = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')
                      || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_'
                      || *p == '.' || *p == '~';
    const size_t needed = plain ? 1 : 3;
    if (used + needed >= capacity)
    {
      return 0;
    }
    if (plain)
    {
      destination[used++] = (char)*p;
    }
    else
    {
      destination[used++] = '%';
      destination[used++] = hex[*p >> 4];
      destination[used++] = hex[*p & 15];
    }
  }
  if (capacity == 0)
  {
    return 0;
  }
  destination[used] = '\0';
  return 1;
}

/* curl config values are double-quoted; escape the two special characters. */
static int write_config_value(FILE *file, const char *name, const char *prefix,
                              const char *value)
{
  if (fprintf(file, "%s = \"%s", name, prefix) < 0)
  {
    return 0;
  }
  for (const char *p = value; *p != '\0'; ++p)
  {
    if (*p == '\n' || *p == '\r')
    {
      return 0;
    }
    if ((*p == '"' || *p == '\\') && fputc('\\', file) == EOF)
    {
      return 0;
    }
    if (fputc(*p, file) == EOF)
    {
      return 0;
    }
  }
  return fputs("\"\n", file) != EOF;
}

static void remove_config(HttpJob *job)
{
  if (job->config_path[0] != '\0')
  {
    if (unlink(job->config_path) != 0 && errno != ENOENT)
    {
      perror("HTTP: remove curl config");
    }
    job->config_path[0] = '\0';
  }
}

static void close_output(HttpJob *job)
{
  if (job->output != NULL)
  {
    if (fclose(job->output) != 0)
    {
      perror("HTTP: fclose");
    }
    job->output = NULL;
  }
}

int http_job_start(HttpJob *job, const char *url, const char *bearer,
                   const char *form_body, long max_bytes)
{
  if (job->pid > 0)
  {
    fprintf(stderr, "HTTP: a request is already running\n");
    return 0;
  }

  /* mkstemp creates the file with mode 0600. */
  snprintf(job->config_path, sizeof(job->config_path), "/tmp/inkday-curl.XXXXXX");
  const int config_fd = mkstemp(job->config_path);
  if (config_fd < 0)
  {
    perror("HTTP: create curl config");
    job->config_path[0] = '\0';
    return 0;
  }
  FILE *config = fdopen(config_fd, "w");
  if (config == NULL)
  {
    perror("HTTP: fdopen curl config");
    close(config_fd);
    remove_config(job);
    return 0;
  }
  int written = write_config_value(config, "url", "", url);
  if (written && bearer != NULL)
  {
    written = write_config_value(config, "header", "Authorization: Bearer ", bearer);
  }
  if (written && form_body != NULL)
  {
    written = write_config_value(config, "data", "", form_body);
  }
  if (fclose(config) != 0)
  {
    written = 0;
  }
  if (!written)
  {
    fprintf(stderr, "HTTP: cannot write curl config\n");
    remove_config(job);
    return 0;
  }

  job->output = tmpfile();
  if (job->output == NULL)
  {
    perror("HTTP: tmpfile");
    remove_config(job);
    return 0;
  }
  const int output_fd = fileno(job->output);

  char limit[24];
  snprintf(limit, sizeof(limit), "%ld", max_bytes);

  const pid_t pid = fork();
  if (pid < 0)
  {
    perror("HTTP: fork");
    close_output(job);
    remove_config(job);
    return 0;
  }
  if (pid == 0)
  {
    if (dup2(output_fd, STDOUT_FILENO) < 0)
    {
      _exit(126);
    }
    if (output_fd != STDOUT_FILENO)
    {
      close(output_fd);
    }
    /* No --fail: Google reports errors as JSON bodies, which callers parse. */
    execl("/usr/bin/curl", "curl",
          "--silent", "--show-error",
          "--connect-timeout", "10",
          "--max-time", "30",
          "--max-filesize", limit,
          "--output", "-",
          "--config", job->config_path, (char *)NULL);
    _exit(127);
  }
  job->pid = pid;
  return 1;
}

int http_job_busy(const HttpJob *job)
{
  return job->pid > 0;
}

static char *read_output(FILE *file)
{
  if (fseek(file, 0, SEEK_END) != 0)
  {
    perror("HTTP: fseek");
    return NULL;
  }
  const long length = ftell(file);
  if (length <= 0 || fseek(file, 0, SEEK_SET) != 0)
  {
    fprintf(stderr, "HTTP: empty response\n");
    return NULL;
  }
  char *text = malloc((size_t)length + 1);
  if (text == NULL)
  {
    fprintf(stderr, "HTTP: out of memory\n");
    return NULL;
  }
  if (fread(text, 1, (size_t)length, file) != (size_t)length
      || memchr(text, '\0', (size_t)length) != NULL)
  {
    fprintf(stderr, "HTTP: cannot read response\n");
    free(text);
    return NULL;
  }
  text[length] = '\0';
  return text;
}

int http_job_poll(HttpJob *job, char **body)
{
  *body = NULL;
  if (job->pid <= 0)
  {
    return HTTP_FAILED;
  }
  int status;
  const pid_t result = waitpid(job->pid, &status, WNOHANG);
  if (result == 0 || (result < 0 && errno == EINTR))
  {
    return HTTP_RUNNING;
  }
  job->pid = -1;
  remove_config(job);

  int outcome = HTTP_FAILED;
  if (result < 0)
  {
    perror("HTTP: waitpid");
  }
  else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
  {
    *body = read_output(job->output);
    outcome = *body != NULL ? HTTP_DONE : HTTP_FAILED;
  }
  else if (WIFEXITED(status))
  {
    fprintf(stderr, "HTTP: curl exited with code %d\n", WEXITSTATUS(status));
  }
  else if (WIFSIGNALED(status))
  {
    fprintf(stderr, "HTTP: curl terminated by signal %d\n", WTERMSIG(status));
  }
  close_output(job);
  return outcome;
}

void http_job_cancel(HttpJob *job)
{
  if (job->pid > 0)
  {
    if (kill(job->pid, SIGKILL) != 0 && errno != ESRCH)
    {
      perror("HTTP: kill");
    }
    pid_t result;
    do
    {
      result = waitpid(job->pid, NULL, 0);
    } while (result < 0 && errno == EINTR);
    job->pid = -1;
  }
  close_output(job);
  remove_config(job);
}
