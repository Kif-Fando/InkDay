#include "settings_store.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <json-c/json.h>

static const char settings_path[] = "/mnt/ext1/system/config/inkday.json";

static int valid_settings(const WeatherSettings *settings)
{
  return memchr(settings->city, '\0', sizeof(settings->city)) != NULL
         && memchr(settings->region, '\0', sizeof(settings->region)) != NULL
         && memchr(settings->country, '\0', sizeof(settings->country)) != NULL
         && memchr(settings->timezone, '\0', sizeof(settings->timezone)) != NULL
         && settings->city[0] != '\0' && settings->timezone[0] != '\0'
         && settings->location_selected == 1
         && isfinite(settings->latitude) && fabs(settings->latitude) <= 90.0
         && isfinite(settings->longitude) && fabs(settings->longitude) <= 180.0
         && (settings->refresh_minutes == 15 || settings->refresh_minutes == 30
             || settings->refresh_minutes == 60 || settings->refresh_minutes == 120);
}

static int read_string(struct json_object *root, const char *key,
                       char *destination, size_t capacity)
{
  struct json_object *value = NULL;
  if (!json_object_object_get_ex(root, key, &value)
      || !json_object_is_type(value, json_type_string))
  {
    return 0;
  }
  const char *text = json_object_get_string(value);
  const size_t length = (size_t)json_object_get_string_len(value);
  if (length >= capacity || strlen(text) != length)
  {
    return 0;
  }
  memcpy(destination, text, length + 1);
  return 1;
}

static int read_number(struct json_object *root, const char *key, double *number)
{
  struct json_object *value = NULL;
  if (!json_object_object_get_ex(root, key, &value)
      || (!json_object_is_type(value, json_type_double)
          && !json_object_is_type(value, json_type_int)))
  {
    return 0;
  }
  *number = json_object_get_double(value);
  return isfinite(*number);
}

int settings_store_load(WeatherSettings *settings)
{
  FILE *file = fopen(settings_path, "rb");
  if (file == NULL)
  {
    if (errno == ENOENT)
    {
      return 0;
    }
    perror("Settings: open");
    return -1;
  }

  char text[4097];
  const size_t length = fread(text, 1, sizeof(text) - 1, file);
  int readable = !ferror(file) && length > 0 && length < sizeof(text) - 1
                 && memchr(text, '\0', length) == NULL;
  if (fclose(file) != 0)
  {
    perror("Settings: close");
    readable = 0;
  }
  if (!readable)
  {
    fprintf(stderr, "Settings: cannot read settings file or file is oversized\n");
    return -1;
  }
  text[length] = '\0';
  enum json_tokener_error error;
  struct json_object *root = json_tokener_parse_verbose(text, &error);
  struct json_object *version = NULL;
  struct json_object *interval = NULL;
  WeatherSettings loaded = {0};
  int valid = root != NULL && error == json_tokener_success
              && json_object_is_type(root, json_type_object)
              && json_object_object_get_ex(root, "version", &version)
              && json_object_is_type(version, json_type_int)
              && json_object_get_int64(version) == 1
              && read_string(root, "city", loaded.city, sizeof(loaded.city))
              && read_string(root, "region", loaded.region, sizeof(loaded.region))
              && read_string(root, "country", loaded.country, sizeof(loaded.country))
              && read_string(root, "timezone", loaded.timezone, sizeof(loaded.timezone))
              && read_number(root, "latitude", &loaded.latitude)
              && read_number(root, "longitude", &loaded.longitude)
              && json_object_object_get_ex(root, "refresh_minutes", &interval)
              && json_object_is_type(interval, json_type_int);
  if (valid)
  {
    const int64_t minutes = json_object_get_int64(interval);
    valid = minutes == 15 || minutes == 30 || minutes == 60 || minutes == 120;
    if (valid)
    {
      loaded.refresh_minutes = (int)minutes;
      loaded.location_selected = 1;
      valid = valid_settings(&loaded);
    }
  }
  if (root != NULL)
  {
    json_object_put(root);
  }
  if (!valid)
  {
    fprintf(stderr, "Settings: invalid or unsupported settings file\n");
    return -1;
  }
  *settings = loaded;
  return 1;
}

int settings_store_save(const WeatherSettings *settings)
{
  if (!valid_settings(settings))
  {
    fprintf(stderr, "Settings: refusing to save invalid configuration\n");
    return 0;
  }

  /* String fields are escaped by json-c; no hand-written JSON quoting. */
  struct json_object *root = json_object_new_object();
  if (root == NULL)
  {
    fprintf(stderr, "Settings: cannot allocate JSON object\n");
    return 0;
  }
  struct json_object *values[] = {
      json_object_new_int(1),
      json_object_new_string(settings->city),
      json_object_new_string(settings->region),
      json_object_new_string(settings->country),
      json_object_new_string(settings->timezone),
      json_object_new_double(settings->latitude),
      json_object_new_double(settings->longitude),
      json_object_new_int(settings->refresh_minutes)};
  static const char *const keys[] = {
      "version", "city", "region", "country", "timezone",
      "latitude", "longitude", "refresh_minutes"};
  int allocated = 1;
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
  {
    if (values[i] == NULL)
    {
      allocated = 0;
    }
  }
  if (!allocated)
  {
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
      if (values[i] != NULL)
      {
        json_object_put(values[i]);
      }
    }
    json_object_put(root);
    fprintf(stderr, "Settings: cannot allocate JSON values\n");
    return 0;
  }
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
  {
    json_object_object_add(root, keys[i], values[i]);
  }
  const char *text = json_object_to_json_string(root);
  if (text == NULL)
  {
    fprintf(stderr, "Settings: cannot serialize configuration\n");
    json_object_put(root);
    return 0;
  }

  /* Same-directory rename keeps the old file intact until writing succeeds. */
  char temporary[] = "/mnt/ext1/system/config/inkday.json.XXXXXX";
  const int fd = mkstemp(temporary);
  if (fd < 0)
  {
    perror("Settings: create temporary file");
    json_object_put(root);
    return 0;
  }
  FILE *file = fdopen(fd, "wb");
  int success = 0;
  if (file == NULL)
  {
    perror("Settings: fdopen");
    if (close(fd) != 0)
    {
      perror("Settings: close temporary file");
    }
  }
  else
  {
    const size_t length = strlen(text);
    if (fwrite(text, 1, length, file) != length
        || fflush(file) != 0 || fsync(fd) != 0)
    {
      perror("Settings: write temporary file");
    }
    else
    {
      success = 1;
    }
    if (fclose(file) != 0)
    {
      perror("Settings: close temporary file");
      success = 0;
    }
  }
  json_object_put(root);
  if (success && rename(temporary, settings_path) != 0)
  {
    perror("Settings: replace configuration");
    success = 0;
  }
  if (!success && unlink(temporary) != 0)
  {
    perror("Settings: remove temporary file");
  }
  return success;
}
