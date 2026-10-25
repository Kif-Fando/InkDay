#include "geocoding.h"
#include <inkview.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <json-c/json.h>

static pid_t search_pid = -1;
static FILE *search_file;
static GeoSearchCallback search_callback;
static GeoPlace search_places[GEOCODING_MAX_RESULTS];
static char search_url[1024];
static int search_by_ip;
static int start_request(GeoSearchCallback callback);

static void search_poll(void);

static void close_search_file(void)
{
  if (search_file != NULL)
  {
    if (fclose(search_file) != 0)
    {
      perror("Geocoding: fclose");
    }
    search_file = NULL;
  }
}

/* Percent-encode UTF-8 bytes; the URL is passed to curl without a shell. */
static int encode_query(const char *text, char *output, size_t capacity)
{
  static const char hex[] = "0123456789ABCDEF";
  size_t used = 0;

  for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
  {
    const unsigned char c = *p;
    const int unreserved =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
    const size_t needed = unreserved ? 1 : 3;

    if (used + needed >= capacity)
    {
      return 0;
    }
    if (unreserved)
    {
      output[used++] = (char)c;
    }
    else
    {
      output[used++] = '%';
      output[used++] = hex[c >> 4];
      output[used++] = hex[c & 15];
    }
  }
  output[used] = '\0';
  return 1;
}

static int copy_string(struct json_object *object, const char *key,
                       char *output, size_t capacity, int optional)
{
  struct json_object *value = NULL;
  if (!json_object_object_get_ex(object, key, &value))
  {
    output[0] = '\0';
    return optional;
  }
  if (!json_object_is_type(value, json_type_string))
  {
    return 0;
  }

  const char *text = json_object_get_string(value);
  const size_t length = (size_t)json_object_get_string_len(value);
  if (length >= capacity || strlen(text) != length || (!optional && length == 0))
  {
    return 0;
  }
  memcpy(output, text, length + 1);
  return 1;
}

static int read_coordinate(struct json_object *object, const char *key,
                           double limit, double *output)
{
  struct json_object *value = NULL;
  if (!json_object_object_get_ex(object, key, &value) || (!json_object_is_type(value, json_type_double) && !json_object_is_type(value, json_type_int)))
  {
    return 0;
  }
  const double number = json_object_get_double(value);
  if (!isfinite(number) || number < -limit || number > limit)
  {
    return 0;
  }
  *output = number;
  return 1;
}

static int parse_results(const char *text, int *count)
{
  enum json_tokener_error error;
  struct json_object *root = json_tokener_parse_verbose(text, &error);
  struct json_object *results = NULL;
  struct json_object *api_error = NULL;
  int valid = 0;
  *count = 0;

  if (root == NULL || error != json_tokener_success || !json_object_is_type(root, json_type_object))
  {
    goto done;
  }
  if (json_object_object_get_ex(root, "error", &api_error) && json_object_get_boolean(api_error))
  {
    goto done;
  }

  if (search_by_ip)
  {
    struct json_object *success = NULL;
    struct json_object *timezone = NULL;
    GeoPlace place = {0};

    if (!json_object_object_get_ex(root, "success", &success)
    || !json_object_is_type(success, json_type_boolean)
    || !json_object_get_boolean(success)
    || !copy_string(root, "city", place.city, sizeof(place.city), 0)
    || !copy_string(root, "region", place.region, sizeof(place.region), 1)
    || !copy_string(root, "country", place.country, sizeof(place.country), 0)
    || !read_coordinate(root, "latitude", 90.0, &place.latitude)
    || !read_coordinate(root, "longitude", 180.0, &place.longitude)
    || !json_object_object_get_ex(root, "timezone", &timezone)
    || !json_object_is_type(timezone, json_type_object)
    || !copy_string(timezone, "id", place.timezone, sizeof(place.timezone), 0))
    {
      fprintf(stderr, "Geocoding: IP lookup failed or returned invalid data\n");
      goto done;
    }

    search_places[0] = place;
    *count = 1;
    valid = 1;
    goto done;
  }

  /* Open-Meteo omits results when no places match the query. */
  if (!json_object_object_get_ex(root, "results", &results))
  {
    struct json_object *generation_time = NULL;
    valid = json_object_object_get_ex(root, "generationtime_ms",
                                      &generation_time) &&
            (json_object_is_type(generation_time, json_type_double) || json_object_is_type(generation_time, json_type_int));
    goto done;
  }
  if (!json_object_is_type(results, json_type_array))
  {
    goto done;
  }

  const size_t length = json_object_array_length(results);
  if (length > GEOCODING_MAX_RESULTS)
  {
    goto done;
  }
  for (size_t i = 0; i < length; ++i)
  {
    struct json_object *item = json_object_array_get_idx(results, i);
    GeoPlace place = {0};
    if (!json_object_is_type(item, json_type_object) || !copy_string(item, "name", place.city, sizeof(place.city), 0) || !copy_string(item, "admin1", place.region, sizeof(place.region), 1) || !copy_string(item, "country", place.country, sizeof(place.country), 1) || !copy_string(item, "timezone", place.timezone, sizeof(place.timezone), 0) || !read_coordinate(item, "latitude", 90.0, &place.latitude) || !read_coordinate(item, "longitude", 180.0, &place.longitude))
    {
      goto done;
    }
    search_places[i] = place;
  }
  *count = (int)length;
  valid = 1;

done:
  if (root != NULL)
  {
    json_object_put(root);
  }
  return valid;
}

static void search_poll(void)
{
  ClearTimer(search_poll);
  if (search_pid <= 0)
  {
    return;
  }

  int status;
  const pid_t result = waitpid(search_pid, &status, WNOHANG);
  if (result == 0 || (result < 0 && errno == EINTR))
  {
    SetHardTimer("geocoding-poll", search_poll, 500);
    return;
  }

  const char *error = NULL;
  int count = 0;
  if (result < 0)
  {
    perror("Geocoding: waitpid");
    error = "Cannot collect search result";
  }
  else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
  {
    if (WIFEXITED(status))
    {
      fprintf(stderr, "Geocoding: curl exited with code %d\n",
              WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
      fprintf(stderr, "Geocoding: curl terminated by signal %d\n",
              WTERMSIG(status));
    }
    error = "City search failed";
  }
  else
  {
    char text[32769];
    if (fseek(search_file, 0, SEEK_SET) != 0)
    {
      perror("Geocoding: fseek");
      error = "Cannot read city search response";
    }
    else
    {
      const size_t length = fread(text, 1, sizeof(text) - 1, search_file);
      if (ferror(search_file) || length == 0 || length == sizeof(text) - 1 || memchr(text, '\0', length) != NULL)
      {
        error = "Invalid city search response";
      }
      else
      {
        text[length] = '\0';
        if (!parse_results(text, &count))
        {
          error = "Invalid city search data";
        }
      }
    }
  }

  search_pid = -1;
  close_search_file();
  GeoSearchCallback callback = search_callback;
  search_callback = NULL;
  if (error != NULL)
  {
    fprintf(stderr, "Geocoding: %s\n", error);
  }
  /* Release request resources before notifying the UI. */
  if (callback != NULL)
  {
    callback(search_places, error == NULL ? count : 0, error);
  }
}

int geocoding_start(const char *city, GeoSearchCallback callback)
{
  if (search_pid > 0 || city == NULL || callback == NULL || city[0] == '\0' || strlen(city) >= 128)
  {
    fprintf(stderr, "Geocoding: busy or invalid search arguments\n");
    return 0;
  }

  char encoded[384];
  if (!encode_query(city, encoded, sizeof(encoded)))
  {
    fprintf(stderr, "Geocoding: city query is too long\n");
    return 0;
  }
  const int length = snprintf(
      search_url, sizeof(search_url),
      "https://geocoding-api.open-meteo.com/v1/search"
      "?name=%s&count=5&language=en&format=json",
      encoded);
  if (length < 0 || (size_t)length >= sizeof(search_url))
  {
    fprintf(stderr, "Geocoding: search URL is too long\n");
    return 0;
  }

  search_by_ip = 0;
  return start_request(callback);
}

int geocoding_detect_ip(GeoSearchCallback callback)
{
  if (search_pid > 0 || callback == NULL)
  {
    fprintf(stderr, "Geocoding: busy or invalid IP lookup callback\n");
    return 0;
  }

  snprintf(search_url, sizeof(search_url),
           "https://ipwho.is/"
           "?fields=success,message,city,region,country,"
           "latitude,longitude,timezone.id");

  search_by_ip = 1;
  return start_request(callback);
}

static int start_request(GeoSearchCallback callback)
{

  search_file = tmpfile();
  if (search_file == NULL)
  {
    perror("Geocoding: tmpfile");
    return 0;
  }
  const int output_fd = fileno(search_file);
  if (output_fd < 0)
  {
    perror("Geocoding: fileno");
    close_search_file();
    return 0;
  }
  const pid_t pid = fork();
  if (pid < 0)
  {
    perror("Geocoding: fork");
    close_search_file();
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
    execl("/usr/bin/curl", "curl",
          "--fail", "--silent", "--show-error",
          "--connect-timeout", "10", "--max-time", "20",
          "--max-filesize", "32767", "--output", "-",
          search_url, (char *)NULL);
    _exit(127);
  }
  search_pid = pid;
  search_callback = callback;
  SetHardTimer("geocoding-poll", search_poll, 500);
  return 1;
}

void geocoding_cancel(void)
{
  ClearTimer(search_poll);
  search_callback = NULL;
  if (search_pid > 0)
  {
    /* Terminate only this module's child, then collect it. */
    if (kill(search_pid, SIGKILL) != 0 && errno != ESRCH)
    {
      perror("Geocoding: kill");
    }
    pid_t result;
    do
    {
      result = waitpid(search_pid, NULL, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
    {
      perror("Geocoding: waitpid on cancel");
    }
    search_pid = -1;
  }
  close_search_file();
}
