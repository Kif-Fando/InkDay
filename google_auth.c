#include "google_auth.h"
#include "file_util.h"
#include "http_job.h"
#include "sha256.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <json-c/json.h>

#ifdef GOOGLE_CLIENT_HEADER
#include "google_client.h"
#endif

/* Injected by the Makefile from .env; the secret of an installed app is not
   confidential by Google's definition, but it is kept out of the repository. */
#ifndef GOOGLE_CLIENT_ID
#define GOOGLE_CLIENT_ID ""
#endif
#ifndef GOOGLE_CLIENT_SECRET
#define GOOGLE_CLIENT_SECRET ""
#endif
#ifndef GOOGLE_RELAY_URL
#define GOOGLE_RELAY_URL "https://inkday.fandorka.eu/oauth.html"
#endif

#define TOKEN_PATH "/mnt/ext1/system/config/inkday-google.json"
#define SIGN_IN_PATH "/mnt/ext1/system/config/inkday-google-signin.txt"
#define DOWNLOADS_DIR "/mnt/ext1/Downloads"
#define BROWSER_PATH "/ebrmain/bin/webbrowser.sh"

enum
{
  SIGN_IN_TIMEOUT_SECONDS = 15 * 60,
  VERIFIER_SIZE = 44,
  STATE_SIZE = 17,
  CODE_SIZE = 512,
  TOKEN_SIZE = 2048
};

static char refresh_token[TOKEN_SIZE];
static char access_token[TOKEN_SIZE];
static time_t access_expires;
static int sign_in_active;
static time_t sign_in_started;
static char verifier[VERIFIER_SIZE];
static char state[STATE_SIZE];
static char code[CODE_SIZE];

int google_auth_configured(void)
{
  return GOOGLE_CLIENT_ID[0] != '\0' && GOOGLE_CLIENT_SECRET[0] != '\0';
}

int google_auth_connected(void)
{
  return refresh_token[0] != '\0';
}

int google_auth_signing_in(void)
{
  return sign_in_active;
}

int google_auth_has_code(void)
{
  return code[0] != '\0';
}

static void base64url(const uint8_t *data, size_t length, char *out, size_t capacity)
{
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  size_t used = 0;
  uint32_t buffer = 0;
  int bits = 0;
  for (size_t i = 0; i < length; ++i)
  {
    buffer = buffer << 8 | data[i];
    bits += 8;
    while (bits >= 6 && used + 1 < capacity)
    {
      bits -= 6;
      out[used++] = alphabet[(buffer >> bits) & 63];
    }
  }
  if (bits > 0 && used + 1 < capacity)
  {
    out[used++] = alphabet[(buffer << (6 - bits)) & 63];
  }
  out[used] = '\0';
}

static int random_bytes(uint8_t *data, size_t length)
{
  const int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
  {
    perror("Google auth: open /dev/urandom");
    return 0;
  }
  size_t done = 0;
  while (done < length)
  {
    const ssize_t got = read(fd, data + done, length - done);
    if (got < 0 && errno == EINTR)
    {
      continue;
    }
    if (got <= 0)
    {
      perror("Google auth: read /dev/urandom");
      close(fd);
      return 0;
    }
    done += (size_t)got;
  }
  close(fd);
  return 1;
}

static int save_refresh_token(void)
{
  struct json_object *root = json_object_new_object();
  if (root == NULL)
  {
    return 0;
  }
  json_object_object_add(root, "version", json_object_new_int(1));
  json_object_object_add(root, "refresh_token", json_object_new_string(refresh_token));
  const char *text = json_object_to_json_string(root);
  const int saved = text != NULL && file_write_atomic(TOKEN_PATH, text);
  json_object_put(root);
  return saved;
}

static void clear_sign_in(void)
{
  sign_in_active = 0;
  memset(verifier, 0, sizeof(verifier));
  memset(state, 0, sizeof(state));
  memset(code, 0, sizeof(code));
  if (unlink(SIGN_IN_PATH) != 0 && errno != ENOENT)
  {
    perror("Google auth: remove sign-in file");
  }
}

static int valid_token_text(const char *text)
{
  for (const char *p = text; *p != '\0'; ++p)
  {
    if (*p <= ' ' || *p == '"' || *p == '\\' || *p > '~')
    {
      return 0;
    }
  }
  return text[0] != '\0';
}

void google_auth_load(void)
{
  int missing;
  char *text = file_read_text(TOKEN_PATH, 8192, &missing);
  if (text != NULL)
  {
    struct json_object *root = json_tokener_parse(text);
    struct json_object *value = NULL;
    if (root != NULL && json_object_object_get_ex(root, "refresh_token", &value)
        && json_object_is_type(value, json_type_string)
        && (size_t)json_object_get_string_len(value) < sizeof(refresh_token)
        && valid_token_text(json_object_get_string(value)))
    {
      snprintf(refresh_token, sizeof(refresh_token), "%s", json_object_get_string(value));
      fprintf(stderr, "Google auth: refresh token loaded\n");
    }
    else
    {
      fprintf(stderr, "Google auth: invalid token file ignored\n");
    }
    if (root != NULL)
    {
      json_object_put(root);
    }
    free(text);
  }

  /* An unfinished sign-in survives the app being unloaded while the browser runs. */
  text = file_read_text(SIGN_IN_PATH, 256, &missing);
  if (text != NULL)
  {
    long long started = 0;
    char loaded_verifier[VERIFIER_SIZE];
    char loaded_state[STATE_SIZE];
    const time_t now = time(NULL);
    if (sscanf(text, "%43s %16s %lld", loaded_verifier, loaded_state, &started) == 3
        && strlen(loaded_verifier) == VERIFIER_SIZE - 1
        && strlen(loaded_state) == STATE_SIZE - 1
        && now >= (time_t)started && now - (time_t)started < SIGN_IN_TIMEOUT_SECONDS)
    {
      memcpy(verifier, loaded_verifier, sizeof(verifier));
      memcpy(state, loaded_state, sizeof(state));
      sign_in_started = (time_t)started;
      sign_in_active = 1;
      fprintf(stderr, "Google auth: resuming unfinished sign-in\n");
    }
    else
    {
      clear_sign_in();
    }
    free(text);
  }
}

static int is_auth_download(const char *name)
{
  const size_t length = strlen(name);
  /* "inkday-auth.txt" itself, or a browser rename like "inkday-auth (1).txt". */
  return strncmp(name, "inkday-auth", 11) == 0 && length >= 15
         && strcmp(name + length - 4, ".txt") == 0;
}

static void remove_auth_downloads(void)
{
  DIR *directory = opendir(DOWNLOADS_DIR);
  if (directory == NULL)
  {
    return;
  }
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL)
  {
    if (is_auth_download(entry->d_name))
    {
      char path[512];
      snprintf(path, sizeof(path), DOWNLOADS_DIR "/%s", entry->d_name);
      if (unlink(path) != 0)
      {
        fprintf(stderr, "Google auth: cannot remove %s: %s\n", path, strerror(errno));
      }
    }
  }
  closedir(directory);
}

/* Detach the browser completely: the intermediate child exits at once, so the
   browser is reparented to init and never needs to be reaped by InkDay. */
static int launch_browser(const char *url)
{
  const pid_t pid = fork();
  if (pid < 0)
  {
    perror("Google auth: fork");
    return 0;
  }
  if (pid == 0)
  {
    if (fork() == 0)
    {
      setsid();
      const int null_fd = open("/dev/null", O_RDWR);
      if (null_fd >= 0)
      {
        dup2(null_fd, STDIN_FILENO);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
      }
      for (int fd = 3; fd < 1024; ++fd)
      {
        close(fd);
      }
      execl(BROWSER_PATH, "webbrowser.sh", url, (char *)NULL);
      _exit(127);
    }
    _exit(0);
  }
  pid_t result;
  do
  {
    result = waitpid(pid, NULL, 0);
  } while (result < 0 && errno == EINTR);
  return 1;
}

int google_auth_begin(void)
{
  if (!google_auth_configured())
  {
    fprintf(stderr, "Google auth: client credentials were not compiled in\n");
    return 0;
  }
  uint8_t random[32 + 12];
  if (!random_bytes(random, sizeof(random)))
  {
    return 0;
  }
  clear_sign_in();
  base64url(random, 32, verifier, sizeof(verifier));
  base64url(random + 32, 12, state, sizeof(state));

  uint8_t digest[SHA256_DIGEST_SIZE];
  char challenge[44];
  sha256(verifier, strlen(verifier), digest);
  base64url(digest, sizeof(digest), challenge, sizeof(challenge));

  const time_t now = time(NULL);
  char saved[128];
  snprintf(saved, sizeof(saved), "%s %s %lld\n", verifier, state, (long long)now);
  if (!file_write_atomic(SIGN_IN_PATH, saved))
  {
    clear_sign_in();
    return 0;
  }
  /* Stale downloads would otherwise be renamed "inkday-auth (1).txt" etc. */
  remove_auth_downloads();

  char url[256];
  snprintf(url, sizeof(url), "%s#c=%s&s=%s", GOOGLE_RELAY_URL, challenge, state);
  if (!launch_browser(url))
  {
    clear_sign_in();
    return 0;
  }
  sign_in_active = 1;
  sign_in_started = now;
  fprintf(stderr, "Google auth: browser opened for sign-in\n");
  return 1;
}

void google_auth_cancel_sign_in(void)
{
  if (sign_in_active || code[0] != '\0')
  {
    fprintf(stderr, "Google auth: sign-in cancelled\n");
  }
  clear_sign_in();
}

static int hex_value(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/* Find key in an x-www-form-urlencoded query and percent-decode its value. */
static int query_value(const char *query, const char *key, char *out, size_t capacity)
{
  const size_t key_length = strlen(key);
  const char *p = query;
  while (*p != '\0')
  {
    const char *end = strpbrk(p, "&\r\n");
    if (end == NULL)
    {
      end = p + strlen(p);
    }
    if ((size_t)(end - p) > key_length && strncmp(p, key, key_length) == 0 && p[key_length] == '=')
    {
      size_t used = 0;
      for (const char *v = p + key_length + 1; v < end; ++v)
      {
        int c = (unsigned char)*v;
        if (c == '+')
        {
          c = ' ';
        }
        else if (c == '%' && v + 2 < end && hex_value(v[1]) >= 0 && hex_value(v[2]) >= 0)
        {
          c = hex_value(v[1]) * 16 + hex_value(v[2]);
          v += 2;
        }
        if (used + 1 >= capacity || c == 0)
        {
          return 0;
        }
        out[used++] = (char)c;
      }
      out[used] = '\0';
      return 1;
    }
    p = *end != '\0' ? end + 1 : end;
  }
  return 0;
}

int google_auth_check_downloads(void)
{
  if (!sign_in_active)
  {
    return code[0] != '\0' ? 1 : 0;
  }
  const time_t now = time(NULL);
  if (now - sign_in_started >= SIGN_IN_TIMEOUT_SECONDS || now < sign_in_started)
  {
    fprintf(stderr, "Google auth: sign-in timed out\n");
    clear_sign_in();
    return -1;
  }
  DIR *directory = opendir(DOWNLOADS_DIR);
  if (directory == NULL)
  {
    return 0;
  }
  int result = 0;
  struct dirent *entry;
  while (result == 0 && (entry = readdir(directory)) != NULL)
  {
    if (!is_auth_download(entry->d_name))
    {
      continue;
    }
    char path[512];
    int missing;
    snprintf(path, sizeof(path), DOWNLOADS_DIR "/%s", entry->d_name);
    char *text = file_read_text(path, 2048, &missing);
    if (text == NULL)
    {
      continue;
    }
    char received_state[64];
    char error[64];
    if (!query_value(text, "state", received_state, sizeof(received_state))
        || strcmp(received_state, state) != 0)
    {
      fprintf(stderr, "Google auth: ignoring %s (other sign-in attempt)\n", entry->d_name);
    }
    else if (query_value(text, "error", error, sizeof(error)))
    {
      fprintf(stderr, "Google auth: access was not granted: %s\n", error);
      result = -1;
    }
    else if (query_value(text, "code", code, sizeof(code)) && code[0] != '\0')
    {
      fprintf(stderr, "Google auth: authorization code received\n");
      result = 1;
    }
    else
    {
      fprintf(stderr, "Google auth: malformed relay file\n");
      result = -1;
    }
    free(text);
  }
  closedir(directory);

  if (result != 0)
  {
    remove_auth_downloads();
    if (result < 0)
    {
      clear_sign_in();
    }
    else
    {
      /* The verifier is still needed for the exchange; only the waiting ends. */
      sign_in_active = 0;
    }
  }
  return result;
}

static int append_param(char *body, size_t capacity, const char *key, const char *value)
{
  const size_t used = strlen(body);
  char encoded[TOKEN_SIZE * 3];
  if (!url_encode(encoded, sizeof(encoded), value))
  {
    return 0;
  }
  const int written = snprintf(body + used, capacity - used, "%s%s=%s",
                               used ? "&" : "", key, encoded);
  return written > 0 && (size_t)written < capacity - used;
}

int google_auth_exchange_body(char *body, size_t capacity)
{
  if (code[0] == '\0' || verifier[0] == '\0' || capacity == 0)
  {
    return 0;
  }
  body[0] = '\0';
  return append_param(body, capacity, "client_id", GOOGLE_CLIENT_ID)
         && append_param(body, capacity, "client_secret", GOOGLE_CLIENT_SECRET)
         && append_param(body, capacity, "code", code)
         && append_param(body, capacity, "code_verifier", verifier)
         && append_param(body, capacity, "grant_type", "authorization_code")
         && append_param(body, capacity, "redirect_uri", GOOGLE_RELAY_URL);
}

int google_auth_refresh_body(char *body, size_t capacity)
{
  if (refresh_token[0] == '\0' || capacity == 0)
  {
    return 0;
  }
  body[0] = '\0';
  return append_param(body, capacity, "client_id", GOOGLE_CLIENT_ID)
         && append_param(body, capacity, "client_secret", GOOGLE_CLIENT_SECRET)
         && append_param(body, capacity, "refresh_token", refresh_token)
         && append_param(body, capacity, "grant_type", "refresh_token");
}

static const char *json_string(struct json_object *root, const char *key)
{
  struct json_object *value = NULL;
  if (json_object_object_get_ex(root, key, &value) && json_object_is_type(value, json_type_string))
  {
    return json_object_get_string(value);
  }
  return NULL;
}

int google_auth_handle_token_response(const char *text, int exchange)
{
  struct json_object *root = json_tokener_parse(text);
  if (root == NULL || !json_object_is_type(root, json_type_object))
  {
    fprintf(stderr, "Google auth: token response is not JSON\n");
    if (root != NULL)
    {
      json_object_put(root);
    }
    if (exchange)
    {
      clear_sign_in();
    }
    return 0;
  }
  int result = 0;
  const char *error = json_string(root, "error");
  const char *access = json_string(root, "access_token");
  const char *refresh = json_string(root, "refresh_token");
  struct json_object *expires = NULL;
  if (error != NULL)
  {
    const char *description = json_string(root, "error_description");
    fprintf(stderr, "Google auth: token endpoint error: %s (%s)\n", error,
            description != NULL ? description : "");
    /* invalid_grant: expired/revoked refresh token (Testing apps: 7 days) or bad code. */
    result = strcmp(error, "invalid_grant") == 0 || strcmp(error, "unauthorized_client") == 0
                     || strcmp(error, "invalid_client") == 0
                 ? -1
                 : 0;
  }
  else if (access == NULL || strlen(access) >= sizeof(access_token) || !valid_token_text(access)
           || (exchange && (refresh == NULL || strlen(refresh) >= sizeof(refresh_token)
                            || !valid_token_text(refresh))))
  {
    fprintf(stderr, "Google auth: incomplete token response\n");
  }
  else
  {
    snprintf(access_token, sizeof(access_token), "%s", access);
    int64_t lifetime = 3600;
    if (json_object_object_get_ex(root, "expires_in", &expires)
        && json_object_is_type(expires, json_type_int))
    {
      lifetime = json_object_get_int64(expires);
    }
    /* Renew a minute early so a request never starts with a token about to expire. */
    access_expires = time(NULL) + (time_t)(lifetime > 120 ? lifetime - 60 : 60);
    if (refresh != NULL && strlen(refresh) < sizeof(refresh_token) && valid_token_text(refresh))
    {
      snprintf(refresh_token, sizeof(refresh_token), "%s", refresh);
      if (!save_refresh_token())
      {
        fprintf(stderr, "Google auth: cannot save the refresh token\n");
      }
    }
    result = 1;
  }
  json_object_put(root);
  if (exchange)
  {
    /* An authorization code is single-use: success or failure ends the sign-in. */
    clear_sign_in();
  }
  return result;
}

const char *google_auth_access_token(void)
{
  if (access_token[0] == '\0' || time(NULL) >= access_expires)
  {
    return NULL;
  }
  return access_token;
}

void google_auth_invalidate_access(void)
{
  memset(access_token, 0, sizeof(access_token));
  access_expires = 0;
}

void google_auth_disconnect(void)
{
  clear_sign_in();
  google_auth_invalidate_access();
  memset(refresh_token, 0, sizeof(refresh_token));
  if (unlink(TOKEN_PATH) != 0 && errno != ENOENT)
  {
    perror("Google auth: remove token file");
  }
  fprintf(stderr, "Google auth: disconnected\n");
}
