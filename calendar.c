#include "calendar.h"
#include "file_util.h"
#include "google_auth.h"
#include "http_job.h"
#include "i18n.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <json-c/json.h>

#define SETTINGS_PATH "/mnt/ext1/system/config/inkday-calendar.json"
#define CACHE_PATH "/mnt/ext1/system/config/inkday-calendar-cache.json"
#define API_BASE "https://www.googleapis.com/calendar/v3"

enum
{
  EVENT_MAX = 150,
  TITLE_SIZE = 128,
  LOCATION_SIZE = 128,
  ROW_MAX = 400,
  PAGE_MAX = 40,
  DEFAULT_SYNC_SECONDS = 30 * 60,
  /* Keeps a sync from racing the weather widget for the network connection. */
  NETWORK_STAGGER_SECONDS = 5,
  RETRY_SECONDS = 2 * 60,
  NETWORK_TIMEOUT_SECONDS = 30,
  SIGN_IN_POLL_MS = 2000,
  LIST_MAX_BYTES = 128 * 1024,
  EVENTS_MAX_BYTES = 256 * 1024,
  TOKEN_MAX_BYTES = 16 * 1024,
  CACHE_MAX_BYTES = 256 * 1024,
  /* Layout of the dashboard block. */
  HEADER_ROW = 58,
  EVENT_ROW = 46,
  LOCATION_ROW = 36,
  FOOTER_ROW = 64,
  PAGER_WIDTH = 200,
  MARKER_SIZE = 14
};

typedef struct
{
  time_t start;
  time_t end;
  int all_day;
  int calendar;
  char title[TITLE_SIZE];
  char location[LOCATION_SIZE];
} CalendarEvent;

typedef enum
{
  STEP_IDLE,
  STEP_NETWORK,
  STEP_EXCHANGE,
  STEP_REFRESH,
  STEP_LIST,
  STEP_EVENTS
} Step;

typedef struct
{
  int day;
  int event; /* -1 for a day header */
} Row;

static CalendarSettings settings = {.days = 7};
static CalendarInfo calendars[CALENDAR_MAX];
static int calendar_count;
static CalendarEvent events[EVENT_MAX];
static int event_count;
static time_t updated_at;

/* A sync fills staging copies and publishes them only when complete. */
static CalendarInfo staged_calendars[CALENDAR_MAX];
static int staged_calendar_count;
static CalendarEvent staged_events[EVENT_MAX];
static int staged_event_count;

static CalendarMode mode;
static void (*listener)(void);
static CalendarView view;
static HttpJob job;
static Step step;
static int step_calendar;
static int auth_retried;
static int network_done;
static int network_status;
static time_t network_deadline;
static time_t next_sync;
static int sync_seconds = DEFAULT_SYNC_SECONDS;
static int message_id = -1;
static int reconnect_needed;

static Row rows[ROW_MAX];
static int row_count;
static int page_starts[PAGE_MAX + 1];
static int page_count;
static int page;
static int drawn_yday = -1;
static time_t day_starts[8];
static time_t next_highlight_change;

static const char *const weekday_keys[] = {
    "@Sunday", "@Monday", "@Tuesday", "@Wednesday",
    "@Thursday", "@Friday", "@Saturday"};

static void calendar_tick(void);

/* ---------- Settings ---------- */

static int valid_days(int days)
{
  return days == 1 || days == 3 || days == 5 || days == 7;
}

static void load_settings(void)
{
  int missing;
  char *text = file_read_text(SETTINGS_PATH, 16384, &missing);
  if (text == NULL)
  {
    return;
  }
  struct json_object *root = json_tokener_parse(text);
  free(text);
  struct json_object *value = NULL;
  CalendarSettings loaded = {.days = 7};
  int valid = root != NULL && json_object_is_type(root, json_type_object);
  if (valid && json_object_object_get_ex(root, "days", &value) && json_object_is_type(value, json_type_int))
  {
    loaded.days = (int)json_object_get_int64(value);
  }
  valid = valid && valid_days(loaded.days);
  if (valid && json_object_object_get_ex(root, "calendars", &value)
      && json_object_is_type(value, json_type_array))
  {
    loaded.configured = 1;
    const size_t length = json_object_array_length(value);
    for (size_t i = 0; i < length && loaded.count < CALENDAR_MAX; ++i)
    {
      struct json_object *item = json_object_array_get_idx(value, i);
      if (json_object_is_type(item, json_type_string)
          && (size_t)json_object_get_string_len(item) < CALENDAR_ID_SIZE)
      {
        snprintf(loaded.ids[loaded.count++], CALENDAR_ID_SIZE, "%s", json_object_get_string(item));
      }
    }
  }
  if (root != NULL)
  {
    json_object_put(root);
  }
  if (valid)
  {
    settings = loaded;
  }
  else
  {
    fprintf(stderr, "Calendar: invalid settings file ignored\n");
  }
}

static int save_settings(const CalendarSettings *values)
{
  struct json_object *root = json_object_new_object();
  if (root == NULL)
  {
    return 0;
  }
  json_object_object_add(root, "version", json_object_new_int(1));
  json_object_object_add(root, "days", json_object_new_int(values->days));
  if (values->configured)
  {
    struct json_object *ids = json_object_new_array();
    for (int i = 0; ids != NULL && i < values->count; ++i)
    {
      json_object_array_add(ids, json_object_new_string(values->ids[i]));
    }
    json_object_object_add(root, "calendars", ids);
  }
  const char *text = json_object_to_json_string(root);
  const int saved = text != NULL && file_write_atomic(SETTINGS_PATH, text);
  json_object_put(root);
  return saved;
}

const CalendarSettings *calendar_get_settings(void)
{
  return &settings;
}

int calendar_is_selected(const CalendarSettings *values, const CalendarInfo *calendar)
{
  if (!values->configured)
  {
    return calendar->primary;
  }
  for (int i = 0; i < values->count; ++i)
  {
    if (strcmp(values->ids[i], calendar->id) == 0)
    {
      return 1;
    }
  }
  return 0;
}

void calendar_toggle(CalendarSettings *values, const CalendarInfo *calendar)
{
  if (!values->configured)
  {
    values->configured = 1;
    values->count = 0;
    for (int i = 0; i < calendar_count && values->count < CALENDAR_MAX; ++i)
    {
      if (calendars[i].primary)
      {
        memcpy(values->ids[values->count++], calendars[i].id, CALENDAR_ID_SIZE);
      }
    }
  }
  for (int i = 0; i < values->count; ++i)
  {
    if (strcmp(values->ids[i], calendar->id) == 0)
    {
      memmove(values->ids[i], values->ids[i + 1],
              (size_t)(values->count - i - 1) * CALENDAR_ID_SIZE);
      --values->count;
      return;
    }
  }
  if (values->count < CALENDAR_MAX)
  {
    memcpy(values->ids[values->count++], calendar->id, CALENDAR_ID_SIZE);
  }
}

int calendar_list(const CalendarInfo **list)
{
  *list = calendars;
  return calendar_count;
}

/* ---------- Time helpers ---------- */

static long long days_from_civil(int year, int month, int day)
{
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const int year_of_era = year - era * 400;
  const int day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return (long long)era * 146097 + day_of_era - 719468;
}

/* RFC 3339 with an explicit offset, e.g. 2025-10-27T10:00:00+01:00. */
static int parse_date_time(const char *text, time_t *out)
{
  int year, month, day, hour, minute, second, consumed = 0;
  if (sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &month, &day, &hour, &minute, &second,
             &consumed) != 6)
  {
    return 0;
  }
  const char *p = text + consumed;
  if (*p == '.')
  {
    ++p;
    while (*p >= '0' && *p <= '9')
    {
      ++p;
    }
  }
  long offset = 0;
  if (*p == '+' || *p == '-')
  {
    int offset_hours, offset_minutes;
    if (sscanf(p + 1, "%2d:%2d", &offset_hours, &offset_minutes) != 2)
    {
      return 0;
    }
    offset = (offset_hours * 60L + offset_minutes) * 60L * (*p == '-' ? -1 : 1);
  }
  else if (*p != 'Z')
  {
    return 0;
  }
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60)
  {
    return 0;
  }
  const long long seconds = days_from_civil(year, month, day) * 86400LL + hour * 3600LL
                            + minute * 60LL + second - offset;
  *out = (time_t)seconds;
  return 1;
}

/* All-day dates are local calendar days. */
static int parse_date(const char *text, time_t *out)
{
  struct tm date = {0};
  if (sscanf(text, "%4d-%2d-%2d", &date.tm_year, &date.tm_mon, &date.tm_mday) != 3)
  {
    return 0;
  }
  date.tm_year -= 1900;
  date.tm_mon -= 1;
  date.tm_isdst = -1;
  *out = mktime(&date);
  return *out != (time_t)-1;
}

static time_t local_midnight(time_t now, int day_offset)
{
  struct tm local;
  localtime_r(&now, &local);
  local.tm_mday += day_offset;
  local.tm_hour = 0;
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_isdst = -1;
  return mktime(&local);
}

static void format_utc(time_t value, char *out, size_t capacity)
{
  struct tm utc;
  gmtime_r(&value, &utc);
  strftime(out, capacity, "%Y-%m-%dT%H:%M:%SZ", &utc);
}

/* ---------- Cache ---------- */

static void copy_text(char *destination, size_t capacity, const char *text)
{
  snprintf(destination, capacity, "%s", text != NULL ? text : "");
  /* Do not leave a truncated UTF-8 sequence at the end. */
  size_t length = strlen(destination);
  if (length + 1 == capacity)
  {
    while (length > 0 && ((unsigned char)destination[length - 1] & 0xC0) == 0x80)
    {
      --length;
    }
    if (length > 0 && ((unsigned char)destination[length - 1] & 0xC0) == 0xC0)
    {
      --length;
    }
    destination[length] = '\0';
  }
}

static const char *object_string(struct json_object *root, const char *key)
{
  struct json_object *value = NULL;
  if (json_object_object_get_ex(root, key, &value) && json_object_is_type(value, json_type_string))
  {
    return json_object_get_string(value);
  }
  return NULL;
}

static int64_t object_int(struct json_object *root, const char *key, int64_t fallback)
{
  struct json_object *value = NULL;
  if (json_object_object_get_ex(root, key, &value) && json_object_is_type(value, json_type_int))
  {
    return json_object_get_int64(value);
  }
  return fallback;
}

static int object_bool(struct json_object *root, const char *key)
{
  struct json_object *value = NULL;
  return json_object_object_get_ex(root, key, &value) && json_object_get_boolean(value);
}

static void save_cache(void)
{
  struct json_object *root = json_object_new_object();
  struct json_object *list = json_object_new_array();
  struct json_object *items = json_object_new_array();
  if (root == NULL || list == NULL || items == NULL)
  {
    if (root != NULL)
      json_object_put(root);
    if (list != NULL)
      json_object_put(list);
    if (items != NULL)
      json_object_put(items);
    return;
  }
  json_object_object_add(root, "version", json_object_new_int(1));
  json_object_object_add(root, "updated_at", json_object_new_int64((int64_t)updated_at));
  for (int i = 0; i < calendar_count; ++i)
  {
    struct json_object *item = json_object_new_object();
    json_object_object_add(item, "id", json_object_new_string(calendars[i].id));
    json_object_object_add(item, "name", json_object_new_string(calendars[i].name));
    json_object_object_add(item, "primary", json_object_new_boolean(calendars[i].primary));
    json_object_array_add(list, item);
  }
  for (int i = 0; i < event_count; ++i)
  {
    struct json_object *item = json_object_new_object();
    json_object_object_add(item, "c", json_object_new_int(events[i].calendar));
    json_object_object_add(item, "s", json_object_new_int64((int64_t)events[i].start));
    json_object_object_add(item, "e", json_object_new_int64((int64_t)events[i].end));
    json_object_object_add(item, "a", json_object_new_boolean(events[i].all_day));
    json_object_object_add(item, "t", json_object_new_string(events[i].title));
    json_object_object_add(item, "l", json_object_new_string(events[i].location));
    json_object_array_add(items, item);
  }
  json_object_object_add(root, "calendars", list);
  json_object_object_add(root, "events", items);
  const char *text = json_object_to_json_string(root);
  if (text == NULL || !file_write_atomic(CACHE_PATH, text))
  {
    fprintf(stderr, "Calendar: cannot save cache\n");
  }
  json_object_put(root);
}

static void load_cache(void)
{
  int missing;
  char *text = file_read_text(CACHE_PATH, CACHE_MAX_BYTES, &missing);
  if (text == NULL)
  {
    return;
  }
  struct json_object *root = json_tokener_parse(text);
  free(text);
  struct json_object *list = NULL;
  struct json_object *items = NULL;
  if (root == NULL || object_int(root, "version", 0) != 1
      || !json_object_object_get_ex(root, "calendars", &list) || !json_object_is_type(list, json_type_array)
      || !json_object_object_get_ex(root, "events", &items) || !json_object_is_type(items, json_type_array))
  {
    fprintf(stderr, "Calendar: invalid cache ignored\n");
    if (root != NULL)
    {
      json_object_put(root);
    }
    return;
  }
  calendar_count = 0;
  const size_t list_length = json_object_array_length(list);
  for (size_t i = 0; i < list_length && calendar_count < CALENDAR_MAX; ++i)
  {
    struct json_object *item = json_object_array_get_idx(list, i);
    const char *id = object_string(item, "id");
    if (id == NULL || strlen(id) >= CALENDAR_ID_SIZE)
    {
      continue;
    }
    CalendarInfo *calendar = &calendars[calendar_count++];
    copy_text(calendar->id, sizeof(calendar->id), id);
    copy_text(calendar->name, sizeof(calendar->name), object_string(item, "name"));
    calendar->primary = object_bool(item, "primary");
  }
  event_count = 0;
  const size_t item_length = json_object_array_length(items);
  for (size_t i = 0; i < item_length && event_count < EVENT_MAX; ++i)
  {
    struct json_object *item = json_object_array_get_idx(items, i);
    CalendarEvent *event = &events[event_count];
    event->calendar = (int)object_int(item, "c", -1);
    event->start = (time_t)object_int(item, "s", 0);
    event->end = (time_t)object_int(item, "e", 0);
    event->all_day = object_bool(item, "a");
    copy_text(event->title, sizeof(event->title), object_string(item, "t"));
    copy_text(event->location, sizeof(event->location), object_string(item, "l"));
    if (event->calendar >= 0 && event->calendar < calendar_count && event->start > 0
        && event->end >= event->start)
    {
      ++event_count;
    }
  }
  updated_at = (time_t)object_int(root, "updated_at", 0);
  json_object_put(root);
  fprintf(stderr, "Calendar: cache loaded (%d calendars, %d events)\n", calendar_count, event_count);
}

/* ---------- Layout and drawing ---------- */

static int compare_events(const void *left, const void *right)
{
  const CalendarEvent *a = left;
  const CalendarEvent *b = right;
  if (a->start != b->start)
  {
    return a->start < b->start ? -1 : 1;
  }
  if (a->all_day != b->all_day)
  {
    return a->all_day ? -1 : 1;
  }
  return strcmp(a->title, b->title);
}

static int event_on_day(const CalendarEvent *event, int day, time_t day_start, time_t day_end)
{
  if (event->all_day)
  {
    return event->start < day_end && event->end > day_start;
  }
  /* Timed events appear on their start day; ones already running appear today. */
  return (event->start >= day_start && event->start < day_end)
         || (day == 0 && event->start < day_start && event->end > day_start);
}

/* All-day events are never highlighted: they would stay inverted all day. */
static int event_running(const CalendarEvent *event, time_t now)
{
  return !event->all_day && event->start <= now && now < event->end;
}

static int row_height(int index)
{
  if (rows[index].event < 0)
  {
    return HEADER_ROW;
  }
  return EVENT_ROW + (events[rows[index].event].location[0] != '\0' ? LOCATION_ROW : 0);
}

static void build_layout(void)
{
  const time_t now = time(NULL);
  struct tm local;
  localtime_r(&now, &local);
  drawn_yday = local.tm_yday;

  row_count = 0;
  const int days = settings.days;
  for (int day = 0; day <= days && day < 8; ++day)
  {
    day_starts[day] = local_midnight(now, day);
  }
  for (int day = 0; day < days; ++day)
  {
    const int header = row_count;
    /* All-day events first, then timed events in start order. */
    for (int pass = 0; pass < 2; ++pass)
    {
      for (int i = 0; i < event_count && row_count < ROW_MAX - 1; ++i)
      {
        if (events[i].all_day == (pass == 0) && events[i].calendar < calendar_count
            && calendar_is_selected(&settings, &calendars[events[i].calendar])
            && event_on_day(&events[i], day, day_starts[day], day_starts[day + 1]))
        {
          if (row_count == header)
          {
            rows[row_count++] = (Row){day, -1};
          }
          rows[row_count++] = (Row){day, i};
        }
      }
    }
  }

  /* The highlight changes when a shown event starts or ends. */
  next_highlight_change = 0;
  for (int i = 0; i < row_count; ++i)
  {
    if (rows[i].event < 0 || events[rows[i].event].all_day)
    {
      continue;
    }
    const CalendarEvent *event = &events[rows[i].event];
    const time_t change = event->start > now ? event->start : event->end;
    if (change > now && (next_highlight_change == 0 || change < next_highlight_change))
    {
      next_highlight_change = change;
    }
  }

  const int available = view.height - FOOTER_ROW;
  page_count = 0;
  page_starts[0] = 0;
  int used = 0;
  for (int i = 0; i < row_count; ++i)
  {
    int needed = row_height(i);
    /* A header is kept together with its first event. */
    if (rows[i].event < 0 && i + 1 < row_count)
    {
      needed += row_height(i + 1);
    }
    if (used > 0 && used + needed > available && page_count < PAGE_MAX - 1)
    {
      page_starts[++page_count] = i;
      used = rows[i].event >= 0 ? HEADER_ROW : 0;
    }
    used += row_height(i);
  }
  page_starts[++page_count] = row_count;
  if (page >= page_count)
  {
    page = page_count - 1;
  }
  if (page < 0)
  {
    page = 0;
  }
}

static int muted_color(void)
{
  return view.foreground_color == BLACK ? DGRAY : LGRAY;
}

/* Filled triangle, size pixels from base to tip; direction -1 points left. */
static void fill_arrow(int center_x, int center_y, int size, int direction, int color)
{
  for (int i = 0; i <= size; ++i)
  {
    const int x = direction < 0 ? center_x - size / 2 + i : center_x + size / 2 - i;
    DrawLine(x, center_y - i, x, center_y + i, color);
  }
}

void calendar_draw_pager(int x, int y, int width, int height, int page_index, int pages,
                         ifont *font, int color)
{
  const int size = height * 2 / 5;
  const int center_y = y + height / 2;
  char text[32];
  snprintf(text, sizeof(text), "%d/%d", page_index + 1, pages);
  fill_arrow(x + size, center_y, size, -1, color);
  fill_arrow(x + width - size, center_y, size, 1, color);
  SetFont(font, color);
  DrawTextRect(x + 2 * size, y, width - 4 * size, height, text, ALIGN_CENTER | VALIGN_MIDDLE);
}

static void draw_marker(int x, int y, int marker, int color)
{
  const int half = MARKER_SIZE / 2;
  const int filled = marker < 4;
  switch (marker % 4)
  {
  case 0:
    for (int r = filled ? 1 : half - 1; r <= half; ++r)
    {
      DrawCircle(x, y, r, color);
    }
    break;
  case 1:
    if (filled)
    {
      FillArea(x - half, y - half, MARKER_SIZE, MARKER_SIZE, color);
    }
    else
    {
      DrawRect(x - half, y - half, MARKER_SIZE, MARKER_SIZE, color);
      DrawRect(x - half + 1, y - half + 1, MARKER_SIZE - 2, MARKER_SIZE - 2, color);
    }
    break;
  case 2:
    for (int dy = 0; dy <= MARKER_SIZE; ++dy)
    {
      const int w = dy / 2;
      if (filled || dy >= MARKER_SIZE - 1)
      {
        DrawLine(x - w, y - half + dy, x + w, y - half + dy, color);
      }
      else
      {
        DrawPixel(x - w, y - half + dy, color);
        DrawPixel(x + w, y - half + dy, color);
        DrawPixel(x - w + 1, y - half + dy, color);
        DrawPixel(x + w - 1, y - half + dy, color);
      }
    }
    break;
  default:
    for (int dy = -half; dy <= half; ++dy)
    {
      const int w = half - (dy < 0 ? -dy : dy);
      if (filled)
      {
        DrawLine(x - w, y + dy, x + w, y + dy, color);
      }
      else
      {
        DrawPixel(x - w, y + dy, color);
        DrawPixel(x + w, y + dy, color);
        DrawPixel(x - w + (w > 0), y + dy, color);
        DrawPixel(x + w - (w > 0), y + dy, color);
      }
    }
    break;
  }
}

/* Calendars keep a stable marker based on their position among the selected ones. */
static int marker_for(int calendar)
{
  int marker = 0;
  for (int i = 0; i < calendar && i < calendar_count; ++i)
  {
    if (calendar_is_selected(&settings, &calendars[i]))
    {
      ++marker;
    }
  }
  return marker % 8;
}

static void day_title(int day, char *out, size_t capacity)
{
  if (day == 0)
  {
    snprintf(out, capacity, "%s", tr(STR_TODAY));
    return;
  }
  if (day == 1)
  {
    snprintf(out, capacity, "%s", tr(STR_TOMORROW));
    return;
  }
  struct tm local;
  localtime_r(&day_starts[day], &local);
  snprintf(out, capacity, "%s, %02d.%02d", GetLangText(weekday_keys[local.tm_wday]),
           local.tm_mday, local.tm_mon + 1);
}

static void draw_header_row(int y, int day)
{
  char title[96];
  day_title(day, title, sizeof(title));
  SetFont(view.header_font, view.foreground_color);
  DrawTextRect(view.x, y, view.width, HEADER_ROW - 8, title, ALIGN_LEFT | VALIGN_BOTTOM | DOTS);
  DrawLine(view.x, y + HEADER_ROW - 4, view.x + view.width - 1, y + HEADER_ROW - 4, view.foreground_color);
}

static void format_time_range(const CalendarEvent *event, int day, char *out, size_t capacity)
{
  if (event->all_day)
  {
    snprintf(out, capacity, "%s", tr(STR_ALL_DAY));
    return;
  }
  char start[8] = "\xE2\x80\xA6";
  char end[8] = "\xE2\x80\xA6";
  struct tm local;
  if (event->start >= day_starts[day])
  {
    localtime_r(&event->start, &local);
    strftime(start, sizeof(start), "%H:%M", &local);
  }
  if (event->end <= day_starts[day + 1])
  {
    localtime_r(&event->end, &local);
    strftime(end, sizeof(end), "%H:%M", &local);
  }
  snprintf(out, capacity, "%s\xE2\x80\x93%s", start, end);
}

static void draw_event_row(int y, int day, const CalendarEvent *event, int time_width)
{
  const int text_x = view.x + 28;
  const int title_x = text_x + time_width;
  const int title_width = view.x + view.width - title_x;
  const int has_location = event->location[0] != '\0';
  const int height = EVENT_ROW + (has_location ? LOCATION_ROW : 0);
  /* A running event is drawn inverted: the strongest emphasis on e-ink. */
  const int running = event_running(event, time(NULL));
  const int text_color = running ? view.background_color : view.foreground_color;
  char time_text[32];

  if (running)
  {
    FillArea(view.x, y, view.width, height - 2, view.foreground_color);
  }
  draw_marker(view.x + 9, y + EVENT_ROW / 2, marker_for(event->calendar), text_color);
  format_time_range(event, day, time_text, sizeof(time_text));
  SetFont(view.event_font, text_color);
  DrawTextRect(text_x, y, time_width, EVENT_ROW, time_text, ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
  DrawTextRect(title_x, y, title_width, EVENT_ROW,
               event->title[0] != '\0' ? event->title : tr(STR_NO_TITLE),
               ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
  if (has_location)
  {
    SetFont(view.small_font, running ? view.background_color : muted_color());
    DrawTextRect(title_x, y + EVENT_ROW - 6, title_width, LOCATION_ROW, event->location,
                 ALIGN_LEFT | VALIGN_TOP | DOTS);
  }
}

static void draw_footer(void)
{
  const int y = view.y + view.height - FOOTER_ROW;
  char status[128] = "";
  if (message_id >= 0)
  {
    snprintf(status, sizeof(status), "%s", tr((StringId)message_id));
  }
  else if (updated_at > 0)
  {
    struct tm local;
    char stamp[16];
    const time_t now = time(NULL);
    struct tm today;
    localtime_r(&now, &today);
    localtime_r(&updated_at, &local);
    strftime(stamp, sizeof(stamp),
             local.tm_yday == today.tm_yday && local.tm_year == today.tm_year ? "%H:%M" : "%d.%m %H:%M",
             &local);
    snprintf(status, sizeof(status), "%s %s", tr(STR_UPDATED), stamp);
  }
  int pager_width = 0;
  if (page_count > 1)
  {
    pager_width = PAGER_WIDTH;
    calendar_draw_pager(view.x + view.width - pager_width, y, pager_width, FOOTER_ROW, page,
                        page_count, view.event_font, view.foreground_color);
  }
  SetFont(view.small_font, muted_color());
  DrawTextRect(view.x, y, view.width - pager_width, FOOTER_ROW, status,
               ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
}

static int block_visible(void)
{
  return google_auth_connected() || reconnect_needed || google_auth_signing_in()
         || google_auth_has_code() || event_count > 0;
}

void calendar_draw(void)
{
  if (view.width <= 0 || view.height <= FOOTER_ROW || view.event_font == NULL)
  {
    return;
  }
  FillArea(view.x, view.y, view.width, view.height, view.background_color);
  build_layout();
  if (!block_visible())
  {
    return;
  }

  SetFont(view.event_font, view.foreground_color);
  int time_width = StringWidth("00:00\xE2\x80\x93" "00:00");
  const int all_day_width = StringWidth(tr(STR_ALL_DAY));
  if (all_day_width > time_width)
  {
    time_width = all_day_width;
  }
  time_width += 24;
  if (time_width > view.width / 2)
  {
    time_width = view.width / 2;
  }

  if (row_count == 0)
  {
    DrawTextRect(view.x, view.y, view.width, EVENT_ROW * 2,
                 google_auth_connected() && updated_at > 0 ? tr(STR_NO_EVENTS) : "",
                 ALIGN_LEFT | VALIGN_MIDDLE);
  }
  int y = view.y;
  const int first = page_starts[page];
  const int last = page_starts[page + 1];
  if (first < last && rows[first].event >= 0)
  {
    draw_header_row(y, rows[first].day);
    y += HEADER_ROW;
  }
  for (int i = first; i < last; ++i)
  {
    if (rows[i].event < 0)
    {
      draw_header_row(y, rows[i].day);
    }
    else
    {
      draw_event_row(y, rows[i].day, &events[rows[i].event], time_width);
    }
    y += row_height(i);
  }
  draw_footer();
}

static void notify(void)
{
  if (mode == CALENDAR_DASHBOARD)
  {
    calendar_draw();
    PartialUpdate(view.x, view.y, view.width, view.height);
  }
  else if (mode == CALENDAR_SETTINGS && listener != NULL)
  {
    listener();
  }
}

/* Progress changes matter to settings only; the dashboard is not redrawn. */
static void notify_settings(void)
{
  if (mode == CALENDAR_SETTINGS && listener != NULL)
  {
    listener();
  }
}

void calendar_set_view(const CalendarView *values)
{
  view = *values;
}

void calendar_set_listener(void (*changed)(void))
{
  listener = changed;
}

void calendar_check_day(void)
{
  const time_t now = time(NULL);
  struct tm local;
  localtime_r(&now, &local);
  if (drawn_yday >= 0 && local.tm_yday != drawn_yday)
  {
    page = 0;
    notify();
  }
  else if (mode == CALENDAR_DASHBOARD && next_highlight_change > 0 && now >= next_highlight_change)
  {
    notify();
  }
}

static int change_page(int delta)
{
  if (page_count <= 1)
  {
    return 0;
  }
  page = (page + delta + page_count) % page_count;
  notify();
  return 1;
}

int calendar_handle_tap(int x, int y)
{
  if (mode != CALENDAR_DASHBOARD || page_count <= 1 || x < view.x || x >= view.x + view.width
      || y < view.y || y >= view.y + view.height)
  {
    return 0;
  }
  /* The pager sits at the right edge, so both of its arrows are in the right half
     of the block: in the footer, split the pager itself instead. */
  const int pager_x = view.x + view.width - PAGER_WIDTH;
  if (y >= view.y + view.height - FOOTER_ROW && x >= pager_x)
  {
    return change_page(x < pager_x + PAGER_WIDTH / 2 ? -1 : 1);
  }
  return change_page(x < view.x + view.width / 2 ? -1 : 1);
}

int calendar_handle_swipe(int x, int y, int direction)
{
  if (mode != CALENDAR_DASHBOARD || x < view.x || x >= view.x + view.width || y < view.y
      || y >= view.y + view.height)
  {
    return 0;
  }
  change_page(direction);
  /* Consumed even on a single page so a swipe is never treated as a tap. */
  return 1;
}

int calendar_handle_key(int key)
{
  if (mode != CALENDAR_DASHBOARD)
  {
    return 0;
  }
  if (key == IV_KEY_PREV || key == IV_KEY_LEFT || key == IV_KEY_PREV2)
  {
    return change_page(-1);
  }
  if (key == IV_KEY_NEXT || key == IV_KEY_RIGHT || key == IV_KEY_NEXT2)
  {
    return change_page(1);
  }
  return 0;
}

/* ---------- Google API responses ---------- */

/* 1: ok, 0: error, -2: access token rejected. *root receives the parsed object. */
static int parse_api_response(const char *text, struct json_object **root)
{
  *root = json_tokener_parse(text);
  if (*root == NULL || !json_object_is_type(*root, json_type_object))
  {
    fprintf(stderr, "Calendar: response is not a JSON object\n");
    return 0;
  }
  struct json_object *error = NULL;
  if (json_object_object_get_ex(*root, "error", &error))
  {
    const int64_t code = json_object_is_type(error, json_type_object) ? object_int(error, "code", 0) : 0;
    const char *text_message = json_object_is_type(error, json_type_object) ? object_string(error, "message") : NULL;
    fprintf(stderr, "Calendar: API error %lld: %s\n", (long long)code,
            text_message != NULL ? text_message : "");
    return code == 401 ? -2 : 0;
  }
  return 1;
}

static int handle_list(const char *text)
{
  struct json_object *root;
  const int result = parse_api_response(text, &root);
  struct json_object *items = NULL;
  if (result == 1 && json_object_object_get_ex(root, "items", &items)
      && json_object_is_type(items, json_type_array))
  {
    staged_calendar_count = 0;
    const size_t length = json_object_array_length(items);
    /* Primary first; the rest keep Google's order. */
    for (int pass = 0; pass < 2; ++pass)
    {
      for (size_t i = 0; i < length && staged_calendar_count < CALENDAR_MAX; ++i)
      {
        struct json_object *item = json_object_array_get_idx(items, i);
        const char *id = object_string(item, "id");
        const int primary = object_bool(item, "primary");
        if (id == NULL || strlen(id) >= CALENDAR_ID_SIZE || primary != (pass == 0))
        {
          continue;
        }
        const char *name = object_string(item, "summaryOverride");
        if (name == NULL)
        {
          name = object_string(item, "summary");
        }
        CalendarInfo *calendar = &staged_calendars[staged_calendar_count++];
        copy_text(calendar->id, sizeof(calendar->id), id);
        copy_text(calendar->name, sizeof(calendar->name), name != NULL ? name : id);
        calendar->primary = primary;
      }
    }
    json_object_put(root);
    return 1;
  }
  if (root != NULL)
  {
    json_object_put(root);
  }
  return result == 1 ? 0 : result;
}

static int read_event_time(struct json_object *item, const char *key, time_t *out, int *all_day)
{
  struct json_object *value = NULL;
  if (!json_object_object_get_ex(item, key, &value))
  {
    return 0;
  }
  const char *date_time = object_string(value, "dateTime");
  const char *date = object_string(value, "date");
  *all_day = date_time == NULL;
  return date_time != NULL ? parse_date_time(date_time, out) : date != NULL && parse_date(date, out);
}

static int handle_events(const char *text, int calendar)
{
  struct json_object *root;
  const int result = parse_api_response(text, &root);
  struct json_object *items = NULL;
  if (result == 1 && json_object_object_get_ex(root, "items", &items)
      && json_object_is_type(items, json_type_array))
  {
    const size_t length = json_object_array_length(items);
    for (size_t i = 0; i < length && staged_event_count < EVENT_MAX; ++i)
    {
      struct json_object *item = json_object_array_get_idx(items, i);
      const char *status = object_string(item, "status");
      CalendarEvent *event = &staged_events[staged_event_count];
      int end_all_day;
      if ((status != NULL && strcmp(status, "cancelled") == 0)
          || !read_event_time(item, "start", &event->start, &event->all_day)
          || !read_event_time(item, "end", &event->end, &end_all_day))
      {
        continue;
      }
      if (event->end < event->start)
      {
        event->end = event->start;
      }
      event->calendar = calendar;
      copy_text(event->title, sizeof(event->title), object_string(item, "summary"));
      copy_text(event->location, sizeof(event->location), object_string(item, "location"));
      /* Multi-line locations would not fit a single row. */
      for (char *p = event->location; *p != '\0'; ++p)
      {
        if (*p == '\n' || *p == '\r')
        {
          *p = ' ';
        }
      }
      ++staged_event_count;
    }
    if (staged_event_count == EVENT_MAX)
    {
      fprintf(stderr, "Calendar: event limit reached, some events are not shown\n");
    }
    json_object_put(root);
    return 1;
  }
  if (root != NULL)
  {
    json_object_put(root);
  }
  return result == 1 ? 0 : result;
}

/* ---------- Synchronization ---------- */

static void finish_step(int message)
{
  step = STEP_IDLE;
  auth_retried = 0;
  message_id = message;
}

static void fail(int message)
{
  fprintf(stderr, "Calendar: sync failed\n");
  finish_step(message);
  next_sync = time(NULL) + RETRY_SECONDS;
  notify();
}

static int next_selected(int from)
{
  for (int i = from; i < staged_calendar_count; ++i)
  {
    if (calendar_is_selected(&settings, &staged_calendars[i]))
    {
      return i;
    }
  }
  return -1;
}

static void publish(void)
{
  memcpy(calendars, staged_calendars, sizeof(calendars));
  calendar_count = staged_calendar_count;
  memcpy(events, staged_events, sizeof(events[0]) * (size_t)staged_event_count);
  event_count = staged_event_count;
  qsort(events, (size_t)event_count, sizeof(events[0]), compare_events);
  updated_at = time(NULL);
  save_cache();
  finish_step(-1);
  next_sync = updated_at + sync_seconds;
  fprintf(stderr, "Calendar: synced %d calendars, %d events\n", calendar_count, event_count);
  notify();
}

static int start_request(Step next)
{
  char url[1024];
  static char body[8192];
  const char *bearer = NULL;
  const char *form = NULL;
  long limit = TOKEN_MAX_BYTES;

  step = next;
  if (next == STEP_EXCHANGE || next == STEP_REFRESH)
  {
    snprintf(url, sizeof(url), "%s", GOOGLE_TOKEN_URL);
    if (!(next == STEP_EXCHANGE ? google_auth_exchange_body(body, sizeof(body))
                                : google_auth_refresh_body(body, sizeof(body))))
    {
      return 0;
    }
    form = body;
  }
  else
  {
    bearer = google_auth_access_token();
    if (bearer == NULL)
    {
      return 0;
    }
    if (next == STEP_LIST)
    {
      snprintf(url, sizeof(url), "%s",
               API_BASE "/users/me/calendarList?maxResults=100&minAccessRole=reader"
                        "&fields=items(id,summary,summaryOverride,primary)");
      limit = LIST_MAX_BYTES;
    }
    else
    {
      char id[CALENDAR_ID_SIZE * 3];
      char time_min[32];
      char time_max[32];
      const time_t now = time(NULL);
      url_encode(id, sizeof(id), staged_calendars[step_calendar].id);
      format_utc(local_midnight(now, 0), time_min, sizeof(time_min));
      format_utc(local_midnight(now, settings.days), time_max, sizeof(time_max));
      snprintf(url, sizeof(url),
               API_BASE "/calendars/%s/events?singleEvents=true&orderBy=startTime&maxResults=100"
                        "&timeMin=%s&timeMax=%s&fields=items(status,summary,location,start,end)",
               id, time_min, time_max);
      limit = EVENTS_MAX_BYTES;
    }
  }
  const int started = http_job_start(&job, url, bearer, form, limit);
  memset(body, 0, sizeof(body));
  return started;
}

static void request_or_fail(Step next, int message)
{
  if (!start_request(next))
  {
    if (next == STEP_EXCHANGE)
    {
      google_auth_cancel_sign_in();
    }
    fail(message);
  }
}

static void request_data(void)
{
  request_or_fail(google_auth_access_token() != NULL ? STEP_LIST : STEP_REFRESH,
                  STR_CALENDAR_SYNC_FAILED);
}

static void handle_response(int outcome, const char *text)
{
  const Step finished = step;
  if (outcome != HTTP_DONE)
  {
    if (finished == STEP_EXCHANGE)
    {
      google_auth_cancel_sign_in();
      fail(STR_SIGN_IN_FAILED);
    }
    else
    {
      fail(STR_NETWORK_ERROR);
    }
    return;
  }

  int result;
  switch (finished)
  {
  case STEP_EXCHANGE:
  case STEP_REFRESH:
    result = google_auth_handle_token_response(text, finished == STEP_EXCHANGE);
    if (result == 1)
    {
      reconnect_needed = 0;
      request_or_fail(STEP_LIST, STR_CALENDAR_SYNC_FAILED);
    }
    else if (finished == STEP_EXCHANGE)
    {
      fail(STR_SIGN_IN_FAILED);
    }
    else if (result < 0)
    {
      /* The refresh token expired or was revoked: a new sign-in is required. */
      google_auth_disconnect();
      reconnect_needed = 1;
      finish_step(STR_RECONNECT_REQUIRED);
      notify();
    }
    else
    {
      fail(STR_CALENDAR_SYNC_FAILED);
    }
    return;

  case STEP_LIST:
  case STEP_EVENTS:
    if (finished == STEP_LIST)
    {
      staged_event_count = 0;
      result = handle_list(text);
    }
    else
    {
      result = handle_events(text, step_calendar);
    }
    if (result == -2 && !auth_retried)
    {
      auth_retried = 1;
      google_auth_invalidate_access();
      request_or_fail(STEP_REFRESH, STR_CALENDAR_SYNC_FAILED);
      return;
    }
    if (result != 1)
    {
      fail(STR_CALENDAR_SYNC_FAILED);
      return;
    }
    if (finished == STEP_LIST)
    {
      /* Settings need the list right away (first connection); old events may
         briefly show other markers until the sync completes. */
      memcpy(calendars, staged_calendars, sizeof(calendars));
      calendar_count = staged_calendar_count;
      notify_settings();
    }
    step_calendar = next_selected(finished == STEP_LIST ? 0 : step_calendar + 1);
    if (step_calendar < 0)
    {
      publish();
      return;
    }
    request_or_fail(STEP_EVENTS, STR_CALENDAR_SYNC_FAILED);
    return;

  default:
    return;
  }
}

static int network_callback(int status)
{
  if (step == STEP_NETWORK)
  {
    network_status = status;
    network_done = 1;
  }
  return 0;
}

static void start_sync(void)
{
  step = STEP_NETWORK;
  network_done = 0;
  network_deadline = time(NULL) + NETWORK_TIMEOUT_SECONDS;
  message_id = -1;
  notify_settings();
  const int result = NetConnectAsync(network_callback);
  if (result < 0 && !network_done)
  {
    fail(STR_CANNOT_START_CONNECTION);
  }
}

static void calendar_tick(void)
{
  ClearTimer(calendar_tick);

  if (step == STEP_NETWORK)
  {
    if (network_done)
    {
      network_done = 0;
      if (network_status != NET_OK)
      {
        if (google_auth_has_code())
        {
          google_auth_cancel_sign_in();
        }
        fail(STR_NETWORK_ERROR);
      }
      else if (google_auth_has_code())
      {
        request_or_fail(STEP_EXCHANGE, STR_SIGN_IN_FAILED);
      }
      else
      {
        request_data();
      }
    }
    else if (time(NULL) >= network_deadline)
    {
      fail(STR_TIMEOUT);
    }
  }
  else if (step != STEP_IDLE)
  {
    char *text = NULL;
    const int outcome = http_job_poll(&job, &text);
    if (outcome != HTTP_RUNNING)
    {
      handle_response(outcome, text);
      free(text);
    }
  }

  if (step != STEP_IDLE)
  {
    SetHardTimer("calendar-poll", calendar_tick, 500);
    return;
  }
  if (mode == CALENDAR_OFF)
  {
    return;
  }

  if (google_auth_signing_in())
  {
    const int result = google_auth_check_downloads();
    if (result == 0)
    {
      SetHardTimer("calendar-sign-in", calendar_tick, SIGN_IN_POLL_MS);
      return;
    }
    if (result < 0)
    {
      message_id = STR_ACCESS_DENIED;
      notify();
      return;
    }
    fprintf(stderr, "Calendar: exchanging the authorization code\n");
  }

  const time_t now = time(NULL);
  if (google_auth_has_code() || (google_auth_connected() && now >= next_sync))
  {
    start_sync();
    if (step != STEP_IDLE)
    {
      SetHardTimer("calendar-poll", calendar_tick, 500);
    }
    return;
  }
  if (google_auth_connected())
  {
    double remaining = difftime(next_sync, now);
    if (remaining < 1.0)
    {
      remaining = 1.0;
    }
    else if (remaining > sync_seconds)
    {
      remaining = sync_seconds;
    }
    SetHardTimer("calendar-refresh", calendar_tick, (int)(remaining * 1000));
  }
}

/* ---------- Public control ---------- */

void calendar_init(void)
{
  http_job_init(&job);
  load_settings();
  google_auth_load();
  load_cache();
  const time_t now = time(NULL);
  /* Cached events show immediately; a stale cache is refreshed shortly after
     start, staggered after the weather request. */
  next_sync = updated_at > 0 && now - updated_at < sync_seconds ? updated_at + sync_seconds
                                                                : now + NETWORK_STAGGER_SECONDS;
  /* Disconnecting in settings deletes the cache, so a cache without a token
     means Google rejected the token (Testing-mode tokens last 7 days). */
  if (!google_auth_connected() && event_count + calendar_count > 0 && google_auth_configured())
  {
    reconnect_needed = 1;
    message_id = STR_RECONNECT_REQUIRED;
  }
}

void calendar_set_interval(int minutes)
{
  if (minutes <= 0 || minutes * 60 == sync_seconds)
  {
    return;
  }
  /* Only a regular schedule moves; pending retries and stale-cache syncs stay. */
  if (updated_at > 0 && next_sync == updated_at + sync_seconds)
  {
    next_sync = updated_at + minutes * 60;
  }
  sync_seconds = minutes * 60;
  if (mode != CALENDAR_OFF && step == STEP_IDLE)
  {
    calendar_tick();
  }
}

void calendar_sync_now(void)
{
  if (!google_auth_connected() || step != STEP_IDLE)
  {
    return;
  }
  fprintf(stderr, "Calendar: manual sync\n");
  next_sync = time(NULL) + NETWORK_STAGGER_SECONDS;
  if (mode != CALENDAR_OFF)
  {
    calendar_tick();
  }
}

void calendar_set_mode(CalendarMode value)
{
  mode = value;
  if (mode != CALENDAR_OFF)
  {
    calendar_tick();
  }
  else if (step == STEP_IDLE)
  {
    ClearTimer(calendar_tick);
  }
}

void calendar_shutdown(void)
{
  mode = CALENDAR_OFF;
  listener = NULL;
  ClearTimer(calendar_tick);
  http_job_cancel(&job);
  step = STEP_IDLE;
}

int calendar_apply_settings(const CalendarSettings *values)
{
  if (!valid_days(values->days) || values->count < 0 || values->count > CALENDAR_MAX)
  {
    return 0;
  }
  const int changed = memcmp(values, &settings, sizeof(settings)) != 0;
  if (!changed)
  {
    return 1;
  }
  if (!save_settings(values))
  {
    return 0;
  }
  settings = *values;
  page = 0;
  /* A wider range or new calendars need fresh data. */
  next_sync = 0;
  return 1;
}

CalendarStatus calendar_status(void)
{
  if (!google_auth_configured())
  {
    return CALENDAR_UNAVAILABLE;
  }
  if (google_auth_signing_in())
  {
    return CALENDAR_SIGNING_IN;
  }
  if (google_auth_has_code() || step == STEP_EXCHANGE)
  {
    return CALENDAR_CONNECTING;
  }
  if (google_auth_connected())
  {
    return step != STEP_IDLE ? CALENDAR_SYNCING : CALENDAR_CONNECTED;
  }
  return CALENDAR_DISCONNECTED;
}

const char *calendar_message(void)
{
  return message_id >= 0 ? tr((StringId)message_id) : NULL;
}

int calendar_connect(void)
{
  if (step != STEP_IDLE)
  {
    return 0;
  }
  message_id = -1;
  if (!google_auth_begin())
  {
    message_id = STR_SIGN_IN_FAILED;
    return 0;
  }
  reconnect_needed = 0;
  calendar_tick();
  return 1;
}

void calendar_cancel_sign_in(void)
{
  google_auth_cancel_sign_in();
  message_id = -1;
  calendar_tick();
}

void calendar_disconnect(void)
{
  http_job_cancel(&job);
  step = STEP_IDLE;
  google_auth_disconnect();
  reconnect_needed = 0;
  message_id = -1;
  calendar_count = 0;
  event_count = 0;
  updated_at = 0;
  page = 0;
  if (unlink(CACHE_PATH) != 0 && errno != ENOENT)
  {
    perror("Calendar: remove cache");
  }
  /* The selection belongs to the disconnected account. */
  CalendarSettings reset = {.days = settings.days};
  if (save_settings(&reset))
  {
    settings = reset;
  }
  ClearTimer(calendar_tick);
}
