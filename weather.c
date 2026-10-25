#include "weather.h"
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <json-c/json.h>

/* Fonts are borrowed from the application and remain valid until shutdown. */
static WeatherView view;
static ifont *day_font;
static ifont *date_font;
static int foreground;
static int foreground_color = BLACK;
static int background_color = WHITE;
/* Keep the last complete response when a later request fails. */
typedef struct
{
  int valid;
  double temperature;
  double apparent_temperature;
  double min_temperature;
  double max_temperature;
  int code;
  int is_day;
  time_t updated_at;
} WeatherData;

static WeatherData weather_data;
static int weather_failed;
static pid_t weather_pid = -1;
static FILE *weather_file;
static time_t weather_next_request;
static int weather_network_pending;
static time_t weather_network_deadline;
static int weather_network_ready;
static int weather_refresh_icon_x;

enum
{
  WEATHER_REFRESH_SECONDS = 30 * 60,
  WEATHER_RETRY_SECONDS = 60
};

static int weather_refresh_seconds = WEATHER_REFRESH_SECONDS;
static char weather_url[512];
static int weather_configured;
static double weather_latitude;
static double weather_longitude;

#define WEATHER_CACHE_PATH "/mnt/ext1/system/config/inkday-weather.json"

/* A 3x3 brush thickens strokes consistently in every direction. */
static void draw_icon_line(int x1, int y1, int x2, int y2, int color)
{
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dy = -1; dy <= 1; ++dy)
    {
      DrawLine(x1 + dx, y1 + dy, x2 + dx, y2 + dy, color);
    }
  }
}

static void draw_icon_circle(int x, int y, int radius, int color)
{
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dy = -1; dy <= 1; ++dy)
    {
      DrawCircle(x + dx, y + dy, radius, color);
    }
  }
}

/* Icon coordinates are relative to their centers; no icon font is required. */
static void draw_weather_sun(int x, int y)
{
  static const int rays[][4] = {
      {0, -38, 0, -52},
      {27, -27, 37, -37},
      {38, 0, 52, 0},
      {27, 27, 37, 37},
      {0, 38, 0, 52},
      {-27, 27, -37, 37},
      {-38, 0, -52, 0},
      {-27, -27, -37, -37}};

  draw_icon_circle(x, y, 26, foreground_color);

  for (int i = 0; i < 8; ++i)
  {
    draw_icon_line(x + rays[i][0], y + rays[i][1],
                   x + rays[i][2], y + rays[i][3],
                   foreground_color);
  }
}

static void draw_weather_moon(int x, int y)
{
  /* Draw a crescent as two arcs without painting over nearby shapes. */
  static const int points[][2] = {
      {15, -40}, {-10, -35}, {-29, -17}, {-34, 8}, {-24, 30}, {-3, 42}, {22, 39}, {39, 23}, {16, 25}, {-2, 13}, {-8, -7}, {-2, -27}, {15, -40}};

  for (int i = 1; i < 13; ++i)
  {
    draw_icon_line(x + points[i - 1][0], y + points[i - 1][1],
                   x + points[i][0], y + points[i][1],
                   foreground_color);
  }
}

static void draw_weather_cloud(int x, int y)
{
  static const int points[][2] = {
      {-48, 22}, {-57, 12}, {-57, -2}, {-47, -14}, {-32, -17}, {-25, -35}, {-9, -44}, {10, -43}, {26, -31}, {30, -16}, {45, -16}, {57, -5}, {60, 8}, {52, 22}, {-48, 22}};

  for (int i = 1; i < 15; ++i)
  {
    draw_icon_line(x + points[i - 1][0], y + points[i - 1][1],
                   x + points[i][0], y + points[i][1],
                   foreground_color);
  }
}

static void draw_weather_icon(int x, int y, int code, int is_day)
{
  /* Open-Meteo uses WMO codes; unknown conditions get a visible placeholder. */
  if (code == 0 || code == 1)
  {
    if (is_day)
    {
      draw_weather_sun(x, y);
    }
    else
    {
      draw_weather_moon(x, y);
    }
    return;
  }

  if (code == 45 || code == 48)
  {
    for (int i = 0; i < 4; ++i)
    {
      int inset = (i % 2) * 12;
      draw_icon_line(x - 55 + inset, y - 30 + i * 20,
                     x + 55 - inset, y - 30 + i * 20,
                     foreground_color);
    }
    return;
  }

  const int rain = (code >= 51 && code <= 57) || (code >= 61 && code <= 67) || (code >= 80 && code <= 82);
  const int snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
  const int thunder = code == 95 || code == 96 || code == 99;

  if (code != 2 && code != 3 && !rain && !snow && !thunder)
  {
    SetFont(date_font, foreground_color);
    DrawTextRect(x - 60, y - 45, 120, 90,
                 "?", ALIGN_CENTER | VALIGN_MIDDLE);
    return;
  }

  draw_weather_cloud(x, y);

  if (rain)
  {
    for (int i = -1; i <= 1; ++i)
    {
      draw_icon_line(x + i * 30 + 5, y + 34,
                     x + i * 30 - 5, y + 55,
                     foreground_color);
    }
  }
  else if (snow)
  {
    for (int i = -1; i <= 1; ++i)
    {
      const int sx = x + i * 32;
      const int sy = y + 45;
      draw_icon_line(sx - 8, sy, sx + 8, sy, foreground_color);
      draw_icon_line(sx, sy - 8, sx, sy + 8, foreground_color);
      draw_icon_line(sx - 6, sy - 6, sx + 6, sy + 6,
                     foreground_color);
      draw_icon_line(sx - 6, sy + 6, sx + 6, sy - 6,
                     foreground_color);
    }
  }
  else if (thunder)
  {
    draw_icon_line(x + 10, y + 30, x - 10, y + 48,
                   foreground_color);
    draw_icon_line(x - 10, y + 48, x + 10, y + 48,
                   foreground_color);
    draw_icon_line(x + 10, y + 48, x - 10, y + 67,
                   foreground_color);
  }
}

static void draw_feels_like_icon(int x, int y)
{
  draw_icon_circle(x, y - 24, 10, foreground_color);

  draw_icon_line(x, y - 13, x, y + 14, foreground_color);
  draw_icon_line(x - 17, y, x, y - 7, foreground_color);
  draw_icon_line(x, y - 7, x + 17, y, foreground_color);
  draw_icon_line(x, y + 14, x - 13, y + 34, foreground_color);
  draw_icon_line(x, y + 14, x + 13, y + 34, foreground_color);

  draw_icon_line(x + 23, y - 20, x + 51, y - 20, foreground_color);
  draw_icon_line(x + 29, y - 9, x + 59, y - 9, foreground_color);
  draw_icon_line(x + 23, y + 2, x + 47, y + 2, foreground_color);
}

static void draw_temperature_arrow(int x, int y, int upward)
{
  const int tip_y = y + (upward ? -14 : 14);
  const int tail_y = y + (upward ? 14 : -14);
  const int wing_y = tip_y + (upward ? 10 : -10);

  draw_icon_line(x, tail_y, x, tip_y, foreground_color);
  draw_icon_line(x - 8, wing_y, x, tip_y, foreground_color);
  draw_icon_line(x + 8, wing_y, x, tip_y, foreground_color);
}

static void draw_refresh_icon(int x, int y)
{
  static const int points[][2] = {
      {12, -9}, {5, -15}, {-5, -15}, {-13, -8}, {-16, 1}, {-12, 11}, {-3, 16}, {7, 14}, {14, 7}};

  for (int i = 1; i < 9; ++i)
  {
    draw_icon_line(x + points[i - 1][0], y + points[i - 1][1],
                   x + points[i][0], y + points[i][1],
                   foreground_color);
  }
  draw_icon_line(x + 12, y - 9, x + 1, y - 9, foreground_color);
  draw_icon_line(x + 12, y - 9, x + 12, y - 20, foreground_color);
}

static void draw_location_label(int x, int y, int width)
{
  if (width <= 0 || view.city == NULL || view.city[0] == '\0')
  {
    return;
  }

  char label[300];
  int length;

  if (view.region != NULL && view.region[0] != '\0')
  {
    length = snprintf(label, sizeof(label), "%s, %s",
                      view.city, view.region);

    if (length < 0 || (size_t)length >= sizeof(label))
    {
      fprintf(stderr, "Weather: location label is too long\n");
      return;
    }

    if (StringWidth(label) <= width)
    {
      DrawTextRect(x, y, width, 40,
                   label, ALIGN_LEFT | VALIGN_TOP);
      return;
    }
  }

  length = snprintf(label, sizeof(label), "%s", view.city);
  if (length < 0 || (size_t)length >= sizeof(label))
  {
    fprintf(stderr, "Weather: city label is too long\n");
    return;
  }

  if (StringWidth(label) > width)
  {
    if (StringWidth("...") > width)
    {
      return;
    }

    size_t end = strlen(label);

    /* Remove whole UTF-8 characters, then append an ASCII ellipsis. */
    while (end > 0)
    {
      do
      {
        --end;
      } while (end > 0 && ((unsigned char)label[end] & 0xC0) == 0x80);

      memcpy(label + end, "...", 4);

      if (StringWidth(label) <= width)
      {
        break;
      }
    }
  }

  DrawTextRect(x, y, width, 40,
               label, ALIGN_LEFT | VALIGN_TOP);
}

/* Draw into the framebuffer only; the caller chooses the refresh strategy. */
void weather_draw(void)
{
  const int x = view.x;
  const int y = view.y;
  const int width = view.width;
  const int feels_x = x + width / 2 + 40;
  const int feels_text_x = feels_x + 75;
  const int limits_x = x + width - 205;
  char text[32];
  char apparent_text[32];
  char min_text[32];
  char max_text[32];

  FillArea(x, y, width, WEATHER_HEIGHT, background_color);

  if (weather_data.valid)
  {
    draw_weather_icon(x + 80, y + 80,
                      weather_data.code, weather_data.is_day);

    snprintf(text, sizeof(text), "%.1f \xC2\xB0"
                                 "C",
             weather_data.temperature);
    snprintf(apparent_text, sizeof(apparent_text),
             "%.1f \xC2\xB0"
             "C",
             weather_data.apparent_temperature);
    snprintf(min_text, sizeof(min_text), "%.1f \xC2\xB0"
                                         "C",
             weather_data.min_temperature);
    snprintf(max_text, sizeof(max_text), "%.1f \xC2\xB0"
                                         "C",
             weather_data.max_temperature);
  }
  else
  {
    snprintf(text, sizeof(text), "-- \xC2\xB0"
                                 "C");
    snprintf(apparent_text, sizeof(apparent_text),
             "-- \xC2\xB0"
             "C");
    snprintf(min_text, sizeof(min_text), "-- \xC2\xB0"
                                         "C");
    snprintf(max_text, sizeof(max_text), "-- \xC2\xB0"
                                         "C");
  }

  SetFont(day_font, foreground_color);
  DrawTextRect(x + 170, y + 15, width / 2 - 170, 140,
               text, ALIGN_LEFT | VALIGN_TOP);

  draw_feels_like_icon(feels_x, y + 80);

  SetFont(date_font, foreground_color);
  DrawTextRect(feels_text_x, y + 40,
               limits_x - feels_text_x - 20, 80,
               apparent_text, ALIGN_LEFT | VALIGN_MIDDLE);

  draw_temperature_arrow(limits_x + 10, y + 50, 1);
  draw_temperature_arrow(limits_x + 10, y + 110, 0);

  DrawTextRect(limits_x + 30, y + 20, 175, 60,
               max_text, ALIGN_LEFT | VALIGN_MIDDLE);
  DrawTextRect(limits_x + 30, y + 80, 175, 60,
               min_text, ALIGN_LEFT | VALIGN_MIDDLE);

  char updated_text[32] = "--:--";
  char footer[80];

  if (weather_data.updated_at != 0)
  {
    const struct tm *updated = localtime(&weather_data.updated_at);

    if (updated == NULL || strftime(updated_text, sizeof(updated_text),
                                    "%d.%m %H:%M:%S", updated) == 0)
    {
      fprintf(stderr, "Weather: cannot format update time\n");
      snprintf(updated_text, sizeof(updated_text), "--:--");
    }
  }

  snprintf(footer, sizeof(footer), "%s%s",
           weather_failed ? "! " : "", updated_text);

  SetFont(view.footer_font, foreground_color);

  const int footer_width = StringWidth(footer);
  const int footer_x = x + width - 10 - footer_width;
  weather_refresh_icon_x = footer_x - 35;

  /* Leave room for the refresh button's 64-pixel touch target and a gap. */
  const int location_width = weather_refresh_icon_x - 32 - 15 - x;
  draw_location_label(x, y + 140, location_width);

  draw_refresh_icon(weather_refresh_icon_x, y + 160);
  DrawTextRect(footer_x, y + 140, footer_width, 40,
               footer, ALIGN_LEFT | VALIGN_TOP);
}

static int weather_parse_daily(struct json_object *root,
                               double *minimum, double *maximum)
{
  /* forecast_days=1 makes each daily array contain today's single value. */
  struct json_object *daily = NULL;
  struct json_object *min_array = NULL;
  struct json_object *max_array = NULL;

  if (!json_object_object_get_ex(root, "daily", &daily) || !json_object_is_type(daily, json_type_object) || !json_object_object_get_ex(daily, "temperature_2m_min", &min_array) || !json_object_object_get_ex(daily, "temperature_2m_max", &max_array) || !json_object_is_type(min_array, json_type_array) || !json_object_is_type(max_array, json_type_array) || json_object_array_length(min_array) != 1 || json_object_array_length(max_array) != 1)
  {
    fprintf(stderr, "Weather: missing or invalid daily arrays\n");
    return 0;
  }

  struct json_object *min_value = json_object_array_get_idx(min_array, 0);
  struct json_object *max_value = json_object_array_get_idx(max_array, 0);

  if ((!json_object_is_type(min_value, json_type_double) && !json_object_is_type(min_value, json_type_int)) || (!json_object_is_type(max_value, json_type_double) && !json_object_is_type(max_value, json_type_int)))
  {
    fprintf(stderr, "Weather: invalid daily temperature types\n");
    return 0;
  }

  const double low = json_object_get_double(min_value);
  const double high = json_object_get_double(max_value);

  if (!isfinite(low) || !isfinite(high) || low < -100.0 || high > 70.0 || low > high)
  {
    fprintf(stderr, "Weather: invalid daily temperature values\n");
    return 0;
  }

  *minimum = low;
  *maximum = high;
  return 1;
}

static int weather_parse(const char *text)
{
  enum json_tokener_error error;
  struct json_object *root = json_tokener_parse_verbose(text, &error);
  struct json_object *current = NULL;
  struct json_object *temperature = NULL;
  struct json_object *apparent = NULL;
  struct json_object *code = NULL;
  struct json_object *day = NULL;
  int valid = 0;

  if (root == NULL || error != json_tokener_success)
  {
    fprintf(stderr, "Weather: invalid JSON\n");
    goto done;
  }

  if (!json_object_is_type(root, json_type_object) || !json_object_object_get_ex(root, "current", &current) || !json_object_is_type(current, json_type_object) || !json_object_object_get_ex(current, "temperature_2m", &temperature) || !json_object_object_get_ex(current, "apparent_temperature", &apparent) || !json_object_object_get_ex(current, "weather_code", &code) || !json_object_object_get_ex(current, "is_day", &day))
  {
    fprintf(stderr, "Weather: missing current weather fields\n");
    goto done;
  }

  if ((!json_object_is_type(temperature, json_type_double) && !json_object_is_type(temperature, json_type_int)) || (!json_object_is_type(apparent, json_type_double) && !json_object_is_type(apparent, json_type_int)) || !json_object_is_type(code, json_type_int) || !json_object_is_type(day, json_type_int))
  {
    fprintf(stderr, "Weather: unexpected field types\n");
    goto done;
  }

  const double value = json_object_get_double(temperature);
  const double apparent_value = json_object_get_double(apparent);
  const int64_t code_value = json_object_get_int64(code);
  const int64_t day_value = json_object_get_int64(day);

  if (!isfinite(value) || value < -100.0 || value > 70.0 || !isfinite(apparent_value) || apparent_value < -150.0 || apparent_value > 100.0 || code_value < 0 || code_value > 99 || (day_value != 0 && day_value != 1))
  {
    fprintf(stderr, "Weather: invalid field values\n");
    goto done;
  }

  double minimum;
  double maximum;

  if (!weather_parse_daily(root, &minimum, &maximum))
  {
    goto done;
  }

  /* Commit only after both current and daily fields have passed validation. */
  weather_data.min_temperature = minimum;
  weather_data.max_temperature = maximum;

  weather_data.temperature = value;
  weather_data.apparent_temperature = apparent_value;
  weather_data.code = (int)code_value;
  weather_data.is_day = (int)day_value;
  weather_data.valid = 1;
  valid = 1;

  fprintf(stderr, "Weather: %.1f C, feels like %.1f C, code=%d, day=%d\n", weather_data.temperature, weather_data.apparent_temperature, weather_data.code, weather_data.is_day);
  fprintf(stderr, "Weather: today min=%.1f C, max=%.1f C\n", weather_data.min_temperature, weather_data.max_temperature);

done:
  if (root != NULL)
  {
    json_object_put(root);
  }
  return valid;
}

static void weather_close_file(void)
{
  if (weather_file != NULL)
  {
    if (fclose(weather_file) != 0)
    {
      perror("Weather: fclose");
    }
    weather_file = NULL;
  }
}

static int weather_cache_save(void)
{
  struct json_object *root = json_object_new_object();
  if (root == NULL)
  {
    fprintf(stderr, "Weather cache: cannot allocate JSON\n");
    return 0;
  }

  json_object_object_add(root, "version", json_object_new_int(1));
  json_object_object_add(root, "latitude", json_object_new_double(weather_latitude));
  json_object_object_add(root, "longitude", json_object_new_double(weather_longitude));
  json_object_object_add(root, "updated_at", json_object_new_int64((int64_t)weather_data.updated_at));
  json_object_object_add(root, "temperature", json_object_new_double(weather_data.temperature));
  json_object_object_add(root, "apparent_temperature", json_object_new_double(weather_data.apparent_temperature));
  json_object_object_add(root, "min_temperature", json_object_new_double(weather_data.min_temperature));
  json_object_object_add(root, "max_temperature", json_object_new_double(weather_data.max_temperature));
  json_object_object_add(root, "code", json_object_new_int(weather_data.code));
  json_object_object_add(root, "is_day", json_object_new_int(weather_data.is_day));

  const char *text = json_object_to_json_string(root);
  if (text == NULL)
  {
    fprintf(stderr, "Weather cache: cannot serialize\n");
    json_object_put(root);
    return 0;
  }

  /* Same-directory rename keeps the old cache intact until writing succeeds. */
  char temporary[] = WEATHER_CACHE_PATH ".XXXXXX";
  const int fd = mkstemp(temporary);
  if (fd < 0)
  {
    perror("Weather cache: create temporary file");
    json_object_put(root);
    return 0;
  }

  int success = 0;
  FILE *file = fdopen(fd, "wb");
  if (file == NULL)
  {
    perror("Weather cache: fdopen");
    if (close(fd) != 0)
    {
      perror("Weather cache: close temporary file");
    }
  }
  else
  {
    const size_t length = strlen(text);
    success = fwrite(text, 1, length, file) == length && fflush(file) == 0 && fsync(fd) == 0;
    if (!success)
    {
      perror("Weather cache: write");
    }
    if (fclose(file) != 0)
    {
      perror("Weather cache: close");
      success = 0;
    }
  }
  json_object_put(root);

  if (success && rename(temporary, WEATHER_CACHE_PATH) != 0)
  {
    perror("Weather cache: replace");
    success = 0;
  }
  if (!success && unlink(temporary) != 0)
  {
    perror("Weather cache: remove temporary file");
  }
  return success;
}

static int weather_cache_number(struct json_object *root, const char *key,
                                double *value)
{
  struct json_object *item = NULL;

  if (!json_object_object_get_ex(root, key, &item) || (!json_object_is_type(item, json_type_double) && !json_object_is_type(item, json_type_int)))
  {
    return 0;
  }
  *value = json_object_get_double(item);
  return isfinite(*value);
}

/* Accept only a valid cache for the configured location received today. */
static int weather_cache_load(void)
{
  FILE *file = fopen(WEATHER_CACHE_PATH, "rb");
  if (file == NULL)
  {
    if (errno != ENOENT)
    {
      perror("Weather cache: open");
    }
    return 0;
  }

  char text[4097];
  const size_t length = fread(text, 1, sizeof(text) - 1, file);
  const int read_failed = ferror(file);
  if (fclose(file) != 0)
  {
    perror("Weather cache: close");
  }

  if (read_failed || length == 0 || length == sizeof(text) - 1 || memchr(text, '\0', length) != NULL)
  {
    fprintf(stderr, "Weather cache: unreadable or oversized\n");
    return 0;
  }
  text[length] = '\0';

  enum json_tokener_error error;
  struct json_object *root = json_tokener_parse_verbose(text, &error);
  if (root == NULL || error != json_tokener_success || !json_object_is_type(root, json_type_object))
  {
    fprintf(stderr, "Weather cache: invalid JSON\n");
    if (root != NULL)
    {
      json_object_put(root);
    }
    return 0;
  }

  double version;
  double latitude;
  double longitude;
  double updated;
  double temperature;
  double apparent;
  double minimum;
  double maximum;
  double code;
  double is_day;

  const int complete = weather_cache_number(root, "version", &version) && weather_cache_number(root, "latitude", &latitude) && weather_cache_number(root, "longitude", &longitude) && weather_cache_number(root, "updated_at", &updated) && weather_cache_number(root, "temperature", &temperature) && weather_cache_number(root, "apparent_temperature", &apparent) && weather_cache_number(root, "min_temperature", &minimum) && weather_cache_number(root, "max_temperature", &maximum) && weather_cache_number(root, "code", &code) && weather_cache_number(root, "is_day", &is_day);
  json_object_put(root);

  if (!complete || version != 1.0 || temperature < -100.0 || temperature > 70.0 || apparent < -150.0 || apparent > 100.0 || minimum < -100.0 || maximum > 70.0 || minimum > maximum || code < 0.0 || code > 99.0 || code != floor(code) || (is_day != 0.0 && is_day != 1.0) || updated < 1.0 || updated > 4102444800.0)
  {
    fprintf(stderr, "Weather cache: ignored (invalid)\n");
    return 0;
  }

  if (fabs(latitude - weather_latitude) > 0.000001 || fabs(longitude - weather_longitude) > 0.000001)
  {
    fprintf(stderr, "Weather cache: ignored (other location)\n");
    return 0;
  }

  const time_t updated_at = (time_t)updated;
  const time_t now = time(NULL);
  if (now == (time_t)-1 || updated_at > now + 60)
  {
    fprintf(stderr, "Weather cache: ignored (timestamp in the future)\n");
    return 0;
  }

  /* Yesterday's daily min/max must not be presented as today's forecast. */
  struct tm cached;
  struct tm current;
  if (localtime_r(&updated_at, &cached) == NULL || localtime_r(&now, &current) == NULL || cached.tm_year != current.tm_year || cached.tm_yday != current.tm_yday)
  {
    fprintf(stderr, "Weather cache: ignored (not from today)\n");
    return 0;
  }

  weather_data.temperature = temperature;
  weather_data.apparent_temperature = apparent;
  weather_data.min_temperature = minimum;
  weather_data.max_temperature = maximum;
  weather_data.code = (int)code;
  weather_data.is_day = (int)is_day;
  weather_data.updated_at = updated_at;
  weather_data.valid = 1;
  fprintf(stderr, "Weather cache: loaded\n");
  return 1;
}

static int weather_read_result(void)
{
  char text[8193];

  if (fseek(weather_file, 0, SEEK_SET) != 0)
  {
    perror("Weather: fseek");
    return 0;
  }

  const size_t length = fread(text, 1, sizeof(text) - 1, weather_file);

  if (ferror(weather_file))
  {
    fprintf(stderr, "Weather: cannot read response\n");
    return 0;
  }

  if (length == 0 || length == sizeof(text) - 1 || memchr(text, '\0', length) != NULL)
  {
    fprintf(stderr, "Weather: empty, oversized or invalid response\n");
    return 0;
  }

  text[length] = '\0';

  if (!weather_parse(text))
  {
    return 0;
  }

  weather_data.updated_at = time(NULL);
  if (weather_data.updated_at == (time_t)-1)
  {
    fprintf(stderr, "Weather: cannot read update time\n");
    weather_data.updated_at = 0;
  }

  /* A cache write failure must not hide freshly received weather. */
  if (weather_data.updated_at != 0 && !weather_cache_save())
  {
    fprintf(stderr, "Weather cache: cannot save\n");
  }

  return 1;
}

static int weather_start_request(void)
{
  /* tmpfile is automatically unlinked; no persistent response file. */
  weather_file = tmpfile();
  if (weather_file == NULL)
  {
    perror("Weather: tmpfile");
    return 0;
  }

  const int output_fd = fileno(weather_file);
  if (output_fd < 0)
  {
    perror("Weather: fileno");
    weather_close_file();
    return 0;
  }

  /* Only curl runs in the child; InkView remains on the application's thread. */
  const pid_t pid = fork();

  if (pid < 0)
  {
    perror("Weather: fork");
    weather_close_file();
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
          "--connect-timeout", "10",
          "--max-time", "20",
          "--max-filesize", "8191",
          "--output", "-",
          weather_url, (char *)NULL);
    _exit(127);
  }

  weather_pid = pid;
  return 1;
}

static void weather_refresh(void)
{
  if (foreground)
  {
    weather_draw();
    PartialUpdate(view.x, view.y, view.width, WEATHER_HEIGHT);
  }
}

static int weather_network_callback(int status);

static void weather_tick(void)
{
  /* One timer alternates between polling a child and waiting for a refresh. */
  ClearTimer(weather_tick);

  /* Poll only while an asynchronous connection attempt is outstanding. */
  if (weather_network_pending)
  {
    if (time(NULL) < weather_network_deadline)
    {
      SetHardTimer("weather-network", weather_tick, 500);
      return;
    }

    fprintf(stderr, "Weather: async network connection timed out\n");
    weather_network_pending = 0;
    weather_network_ready = 0;
    weather_failed = 1;
    weather_next_request = time(NULL) + WEATHER_RETRY_SECONDS;
    weather_refresh();
  }

  if (weather_pid > 0)
  {
    int status;
    const pid_t result = waitpid(weather_pid, &status, WNOHANG);

    if (result == 0 || (result < 0 && errno == EINTR))
    {
      SetHardTimer("weather-poll", weather_tick, 500);
      return;
    }

    int success = 0;

    if (result < 0)
    {
      perror("Weather: waitpid");
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
      success = weather_read_result();
    }
    else if (WIFEXITED(status))
    {
      fprintf(stderr, "Weather: curl exited with code %d\n",
              WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
      fprintf(stderr, "Weather: curl terminated by signal %d\n",
              WTERMSIG(status));
    }

    weather_pid = -1;
    weather_close_file();
    /* Retry failures after a minute without discarding the last good values. */
    weather_failed = !success;
    weather_next_request = time(NULL) + (success ? weather_refresh_seconds : WEATHER_RETRY_SECONDS);
    weather_refresh();
  }

  if (!foreground)
  {
    return;
  }

  if (time(NULL) >= weather_next_request)
  {
    if (weather_network_ready)
    {
      /* Each successful connection allows one HTTPS request. */
      weather_network_ready = 0;
      if (weather_start_request())
      {
        SetHardTimer("weather-poll", weather_tick, 500);
        return;
      }

      weather_failed = 1;
      weather_next_request = time(NULL) + WEATHER_RETRY_SECONDS;
      weather_refresh();
    }
    else
    {
      weather_network_pending = 1;
      weather_network_deadline = time(NULL) + 30;

      const int result = NetConnectAsync(weather_network_callback);
      fprintf(stderr, "Weather: NetConnectAsync returned %d\n", result);

      /* A synchronous callback takes precedence over the start result. */
      if (result < 0 && weather_network_pending)
      {
        fprintf(stderr, "Weather: cannot start network connection\n");
        weather_network_pending = 0;
        weather_failed = 1;
        weather_next_request = time(NULL) + WEATHER_RETRY_SECONDS;
        weather_refresh();
      }
      else
      {
        SetHardTimer("weather-network", weather_tick, 500);
        return;
      }
    }
  }

  /* Sleep until the next request instead of waking every second. */
  double remaining = difftime(weather_next_request, time(NULL));

  /* Bound the delay in case the system clock changes. */
  if (remaining < 1.0)
  {
    remaining = 1.0;
  }
  else if (remaining > weather_refresh_seconds)
  {
    remaining = weather_refresh_seconds;
  }

  SetHardTimer("weather-refresh", weather_tick,
               (int)(remaining * 1000));
}

void weather_shutdown(void)
{
  foreground = 0;
  weather_network_pending = 0;
  weather_network_ready = 0;
  ClearTimer(weather_tick);

  if (weather_pid > 0)
  {
    /* Terminate only our own curl process and reap it before exit. */
    if (kill(weather_pid, SIGKILL) != 0 && errno != ESRCH)
    {
      perror("Weather: kill");
    }

    pid_t result;
    do
    {
      result = waitpid(weather_pid, NULL, 0);
    } while (result < 0 && errno == EINTR);

    if (result < 0)
    {
      perror("Weather: waitpid on exit");
    }
    weather_pid = -1;
  }

  weather_close_file();
}

void weather_set_view(const WeatherView *settings)
{
  view = *settings;
  day_font = view.temperature_font;
  date_font = view.detail_font;
  foreground_color = view.foreground_color;
  background_color = view.background_color;
}

void weather_set_active(int active)
{
  if (active && !weather_configured)
  {
    fprintf(stderr, "Weather: location has not been configured\n");
    return;
  }
  foreground = active;
  if (active)
  {
    weather_tick();
  }
  else if (weather_pid <= 0 && !weather_network_pending)
  {
    ClearTimer(weather_tick);
  }
  /* An in-flight request is reaped in the background, without drawing. */
}

static int weather_network_callback(int status)
{
  fprintf(stderr, "Weather: async network callback status=%d\n", status);

  if (!weather_network_pending)
  {
    return 0;
  }

  weather_network_pending = 0;
  if (status == NET_OK)
  {
    weather_network_ready = 1;
    weather_next_request = 0;
  }
  else
  {
    weather_network_ready = 0;
    weather_failed = 1;
    weather_next_request = time(NULL) + WEATHER_RETRY_SECONDS;
    weather_refresh();
  }

  /* Continue on the existing timer, not recursively inside the callback. */
  return 0;
}

int weather_handle_tap(int x, int y)
{
  const int center_y = view.y + 160;

  if (x < weather_refresh_icon_x - 32 || x >= weather_refresh_icon_x + 32 || y < center_y - 32 || y >= center_y + 32)
  {
    return 0;
  }

  if (weather_pid > 0 || weather_network_pending)
  {
    fprintf(stderr, "Weather: refresh already in progress\n");
    return 1;
  }

  if (!foreground)
  {
    return 1;
  }

  fprintf(stderr, "Weather: manual refresh\n");
  weather_network_ready = 0;
  weather_next_request = 0;
  weather_tick();
  return 1;
}

int weather_can_configure(void)
{
  return !foreground && weather_pid <= 0 && !weather_network_pending;
}

int weather_configure(double latitude, double longitude,
                      int refresh_minutes)
{
  if (!isfinite(latitude) || latitude < -90.0 || latitude > 90.0 || !isfinite(longitude) || longitude < -180.0 || longitude > 180.0 || (refresh_minutes != 15 && refresh_minutes != 30 && refresh_minutes != 60 && refresh_minutes != 120))
  {
    fprintf(stderr, "Weather: invalid configuration\n");
    return 0;
  }

  /* Do not replace a location underneath an outstanding request/callback. */
  if (!weather_can_configure())
  {
    fprintf(stderr, "Weather: cannot configure while active or busy\n");
    return 0;
  }

  char url[sizeof(weather_url)];

  const int length = snprintf(
      url, sizeof(url),
      "https://api.open-meteo.com/v1/forecast"
      "?latitude=%.6f&longitude=%.6f"
      "&current=temperature_2m,apparent_temperature,weather_code,is_day"
      "&daily=temperature_2m_min,temperature_2m_max"
      "&forecast_days=1&timezone=auto",
      latitude, longitude);

  if (length < 0 || (size_t)length >= sizeof(url))
  {
    fprintf(stderr, "Weather: configuration URL is too long\n");
    return 0;
  }

  const int location_changed = strcmp(weather_url, url) != 0;

  memcpy(weather_url, url, (size_t)length + 1);
  weather_configured = 1;
  weather_latitude = latitude;
  weather_longitude = longitude;
  weather_refresh_seconds = refresh_minutes * 60;
  weather_network_ready = 0;

  if (location_changed)
  {
    /* Never show the previous city's weather as the new city's data. */
    weather_data.valid = 0;
    weather_failed = 0;
    weather_data.updated_at = 0;
    weather_next_request = 0;

    if (weather_cache_load())
    {
      weather_next_request = weather_data.updated_at + weather_refresh_seconds;
    }
  }
  else
  {
    weather_next_request = weather_data.updated_at != 0
                               ? weather_data.updated_at + weather_refresh_seconds
                               : 0;
  }

  fprintf(stderr, "Weather: configured %.6f, %.6f, interval=%d min\n",
          latitude, longitude, refresh_minutes);
  return 1;
}