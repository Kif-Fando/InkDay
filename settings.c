#include "settings.h"
#include "geocoding.h"
#include "i18n.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

enum
{
  SETTINGS_MARGIN = 40,
  SETTINGS_GAP = 30,
  SETTINGS_BOTTOM_OFFSET = 140,
  SETTINGS_BUTTON_HEIGHT = 90,
  CITY_Y = 230,
  CITY_HEIGHT = 90,
  SEARCH_Y = 340,
  SEARCH_HEIGHT = 80,
  INTERVAL_Y = 510,
  INTERVAL_HEIGHT = 80,
  RESULTS_Y = 660,
  RESULT_HEIGHT = 105,
  /* The calendar section shares the area below the weather with search results. */
  CAL_SEPARATOR_Y = 665,
  CAL_HEADER_Y = 680,
  CAL_HEADER_HEIGHT = 80,
  CAL_STATUS_Y = 770,
  CAL_STATUS_HEIGHT = 55,
  CAL_DAYS_Y = 835,
  CAL_DAYS_HEIGHT = 75,
  CAL_LIST_Y = 930,
  CAL_ROW_HEIGHT = 60,
  CAL_CHECKBOX = 36,
  CAL_PAGER_WIDTH = 200
};

static const int refresh_options[] = {15, 30, 60, 120};
static const int day_options[] = {1, 3, 5, 7};

static WeatherSettings draft;
static CalendarSettings calendar_draft;
static int calendar_page;
static char city_input[sizeof(draft.city)];
static ifont *settings_font;
static ifont *settings_bold_font;
static int settings_foreground;
static int settings_background;
static int screen_open;
static int screen_active;
static int keyboard_open;
static int search_busy;
static int search_by_ip;
static int network_outstanding;
static int network_finished;
static int network_status;
static time_t network_deadline;
static GeoPlace results[GEOCODING_MAX_RESULTS];
static int result_count;
static int selected_result = -1;
static char search_message[160];
static void connection_poll(void);

static void redraw_settings(void)
{
  if (screen_open && screen_active && !keyboard_open && settings_font != NULL)
  {
    settings_draw(settings_font, settings_foreground, settings_background);
  }
}

static void search_error(const char *message)
{
  fprintf(stderr, "Settings search: %s\n", message);
  search_busy = 0;
  snprintf(search_message, sizeof(search_message), "%s", message);
  redraw_settings();
}

static void search_completed(const GeoPlace *places, int count, const char *error)
{
  if (!screen_open)
  {
    return;
  }
  if (error != NULL)
  {
    /* Detailed geocoding errors stay in the log; the screen gets a translatable one. */
    fprintf(stderr, "Settings search: %s\n", error);
    search_error(tr(STR_SEARCH_FAILED));
    return;
  }
  search_busy = 0;
  if (search_by_ip && count == 1)
  {
    const GeoPlace *place = &places[0];

    memcpy(draft.city, place->city, sizeof(draft.city));
    memcpy(draft.region, place->region, sizeof(draft.region));
    memcpy(draft.country, place->country, sizeof(draft.country));
    memcpy(draft.timezone, place->timezone, sizeof(draft.timezone));
    draft.latitude = place->latitude;
    draft.longitude = place->longitude;
    draft.location_selected = 1;

    result_count = 0;
    selected_result = -1;

    snprintf(search_message, sizeof(search_message),
             "%s: %.60s, %.60s", tr(STR_IP_LOCATION), draft.city, draft.country);
    redraw_settings();

    return;
  }

  result_count = count;
  for (int i = 0; i < count; ++i)
  {
    results[i] = places[i];
  }
  snprintf(search_message, sizeof(search_message), "%s",
           count
               ? (search_by_ip ? tr(STR_SELECT_IP_LOCATION)
                               : tr(STR_SELECT_CITY))
               : tr(STR_NOTHING_FOUND));
  redraw_settings();
}

static int connection_completed(int status)
{
  network_outstanding = 0;
  /* Callback only records completion; the timer owns the next transition. */
  if (screen_open && search_busy)
  {
    network_status = status;
    network_finished = 1;
  }
  return 0;
}

static void connection_poll(void)
{
  ClearTimer(connection_poll);
  if (!screen_open || !search_busy)
  {
    return;
  }
  if (network_finished)
  {
    network_finished = 0;
    if (network_status != NET_OK)
    {
      search_error(tr(STR_NETWORK_ERROR));
      return;
    }
    snprintf(search_message, sizeof(search_message), "%s", tr(STR_SEARCHING));
    redraw_settings();
    const int started = search_by_ip
                            ? geocoding_detect_ip(search_completed)
                            : geocoding_start(draft.city, search_completed);

    if (!started)
    {
      search_error(tr(STR_CANNOT_START_LOOKUP));
    }
    return;
  }
  if (time(NULL) >= network_deadline)
  {
    /* InkView offers no per-attempt cancel token: wait for its late callback
       before allowing another network attempt. */
    search_error(tr(STR_TIMEOUT));
    return;
  }
  SetHardTimer("settings-network", connection_poll, 500);
}

static void start_search(int by_ip)
{
  if (search_busy)
  {
    return;
  }
  if (network_outstanding)
  {
    search_error(tr(STR_PREVIOUS_CONNECTION_BUSY));
    return;
  }
  if (!by_ip && (draft.city[0] == '\0' || strspn(draft.city, " \t\r\n") == strlen(draft.city)))
  {
    search_error(tr(STR_ENTER_CITY));
    return;
  }
  const time_t now = time(NULL);
  if (now == (time_t)-1)
  {
    search_error(tr(STR_CANNOT_START_CONNECTION));
    return;
  }
  result_count = 0;
  selected_result = -1;
  search_by_ip = by_ip;
  search_busy = 1;
  network_finished = 0;
  network_outstanding = 1;
  network_deadline = now + 30;
  snprintf(search_message, sizeof(search_message), "%s...", tr(STR_CONNECTING));
  redraw_settings();

  const int status = NetConnectAsync(connection_completed);
  if (status < 0 && network_outstanding)
  {
    network_outstanding = 0;
    search_error(tr(STR_CANNOT_START_CONNECTION));
    return;
  }
  SetHardTimer("settings-network", connection_poll, 500);
}

static int button_width(void)
{
  return (ScreenWidth() - 2 * SETTINGS_MARGIN - SETTINGS_GAP) / 2;
}

static int interval_width(void)
{
  return (ScreenWidth() - 2 * SETTINGS_MARGIN - 3 * SETTINGS_GAP) / 4;
}

void settings_begin(const WeatherSettings *current, const CalendarSettings *calendar)
{
  settings_end();
  draft = *current;
  calendar_draft = *calendar;
  calendar_page = 0;
  screen_open = 1;
  screen_active = 1;
  result_count = 0;
  selected_result = -1;
  search_message[0] = '\0';
  calendar_set_listener(redraw_settings);
}

const WeatherSettings *settings_get_draft(void)
{
  return &draft;
}

const CalendarSettings *settings_get_calendar_draft(void)
{
  return &calendar_draft;
}

static int calendar_label_width(void)
{
  return (ScreenWidth() - 2 * SETTINGS_MARGIN) / 4;
}

static int calendar_option_width(void)
{
  const int content_width = ScreenWidth() - 2 * SETTINGS_MARGIN;
  return (content_width - calendar_label_width() - 3 * SETTINGS_GAP) / 4;
}

static int calendar_rows_fit(void)
{
  const int bottom = ScreenHeight() - SETTINGS_BOTTOM_OFFSET - 15;
  const int fit = (bottom - CAL_LIST_Y) / CAL_ROW_HEIGHT;
  return fit > 2 ? fit : 2;
}

/* With more calendars than rows, the last row becomes a page switcher. */
static int calendar_per_page(int count)
{
  const int fit = calendar_rows_fit();
  return count <= fit ? fit : fit - 1;
}

static int calendar_page_count(int count)
{
  const int per_page = calendar_per_page(count);
  return count <= per_page ? 1 : (count + per_page - 1) / per_page;
}

static ifont *heading_font(ifont *font)
{
  return settings_bold_font != NULL ? settings_bold_font : font;
}

void settings_set_bold_font(ifont *font)
{
  settings_bold_font = font;
}

static void draw_calendar_section(ifont *font, int foreground_color, int background_color)
{
  const int content_width = ScreenWidth() - 2 * SETTINGS_MARGIN;
  const int half_width = button_width();
  const int button_x = SETTINGS_MARGIN + half_width + SETTINGS_GAP;
  const CalendarStatus status = calendar_status();

  DrawLine(SETTINGS_MARGIN, CAL_SEPARATOR_Y, ScreenWidth() - SETTINGS_MARGIN, CAL_SEPARATOR_Y,
           foreground_color);
  SetFont(heading_font(font), foreground_color);
  DrawTextRect(SETTINGS_MARGIN, CAL_HEADER_Y, half_width, CAL_HEADER_HEIGHT,
               tr(STR_GOOGLE_CALENDAR), ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
  SetFont(font, foreground_color);

  const char *button = NULL;
  switch (status)
  {
  case CALENDAR_DISCONNECTED:
    button = tr(STR_CONNECT);
    break;
  case CALENDAR_SIGNING_IN:
    button = tr(STR_CANCEL);
    break;
  case CALENDAR_CONNECTING:
    button = tr(STR_PLEASE_WAIT);
    break;
  case CALENDAR_SYNCING:
  case CALENDAR_CONNECTED:
    button = tr(STR_DISCONNECT);
    break;
  default:
    break;
  }
  if (button != NULL)
  {
    DrawRect(button_x, CAL_HEADER_Y, half_width, CAL_HEADER_HEIGHT, foreground_color);
    DrawTextRect(button_x, CAL_HEADER_Y, half_width, CAL_HEADER_HEIGHT, button,
                 ALIGN_CENTER | VALIGN_MIDDLE | DOTS);
  }

  const char *message = calendar_message();
  switch (status)
  {
  case CALENDAR_UNAVAILABLE:
    message = tr(STR_CALENDAR_UNAVAILABLE);
    break;
  case CALENDAR_SIGNING_IN:
    message = tr(STR_WAITING_FOR_BROWSER);
    break;
  case CALENDAR_CONNECTING:
    message = tr(STR_SIGNING_IN);
    break;
  case CALENDAR_SYNCING:
    message = tr(STR_LOADING_CALENDARS);
    break;
  case CALENDAR_DISCONNECTED:
    message = message != NULL ? message : tr(STR_NOT_CONNECTED);
    break;
  case CALENDAR_CONNECTED:
    message = message != NULL ? message : tr(STR_CONNECTED);
    break;
  }
  DrawTextRect(SETTINGS_MARGIN, CAL_STATUS_Y, content_width, CAL_STATUS_HEIGHT, message,
               ALIGN_LEFT | VALIGN_MIDDLE | DOTS);

  if (status == CALENDAR_UNAVAILABLE)
  {
    return;
  }

  DrawTextRect(SETTINGS_MARGIN, CAL_DAYS_Y, calendar_label_width(), CAL_DAYS_HEIGHT,
               tr(STR_DAYS), ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
  const int option_width = calendar_option_width();
  for (int i = 0; i < 4; ++i)
  {
    const int option_x = SETTINGS_MARGIN + calendar_label_width() + i * (option_width + SETTINGS_GAP);
    const int selected = calendar_draft.days == day_options[i];
    char label[8];
    snprintf(label, sizeof(label), "%d", day_options[i]);
    if (selected)
    {
      FillArea(option_x, CAL_DAYS_Y, option_width, CAL_DAYS_HEIGHT, foreground_color);
    }
    else
    {
      DrawRect(option_x, CAL_DAYS_Y, option_width, CAL_DAYS_HEIGHT, foreground_color);
    }
    SetFont(font, selected ? background_color : foreground_color);
    DrawTextRect(option_x, CAL_DAYS_Y, option_width, CAL_DAYS_HEIGHT, label,
                 ALIGN_CENTER | VALIGN_MIDDLE);
  }
  SetFont(font, foreground_color);

  const CalendarInfo *list;
  const int count = calendar_list(&list);
  if (count == 0)
  {
    DrawTextRect(SETTINGS_MARGIN, CAL_LIST_Y, content_width, CAL_ROW_HEIGHT,
                 tr(STR_NO_CALENDARS), ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
    return;
  }
  const int per_page = calendar_per_page(count);
  const int pages = calendar_page_count(count);
  if (calendar_page >= pages)
  {
    calendar_page = pages - 1;
  }
  for (int row = 0; row < per_page; ++row)
  {
    const int index = calendar_page * per_page + row;
    if (index >= count)
    {
      break;
    }
    const int row_y = CAL_LIST_Y + row * CAL_ROW_HEIGHT;
    const int box_y = row_y + (CAL_ROW_HEIGHT - CAL_CHECKBOX) / 2;
    DrawRect(SETTINGS_MARGIN, box_y, CAL_CHECKBOX, CAL_CHECKBOX, foreground_color);
    DrawRect(SETTINGS_MARGIN + 1, box_y + 1, CAL_CHECKBOX - 2, CAL_CHECKBOX - 2, foreground_color);
    if (calendar_is_selected(&calendar_draft, &list[index]))
    {
      FillArea(SETTINGS_MARGIN + 7, box_y + 7, CAL_CHECKBOX - 14, CAL_CHECKBOX - 14, foreground_color);
    }
    DrawTextRect(SETTINGS_MARGIN + CAL_CHECKBOX + 20, row_y, content_width - CAL_CHECKBOX - 20,
                 CAL_ROW_HEIGHT, list[index].name, ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
  }
  if (pages > 1)
  {
    const int nav_y = CAL_LIST_Y + per_page * CAL_ROW_HEIGHT;
    calendar_draw_pager(SETTINGS_MARGIN + (content_width - CAL_PAGER_WIDTH) / 2, nav_y,
                        CAL_PAGER_WIDTH, CAL_ROW_HEIGHT, calendar_page, pages, font,
                        foreground_color);
  }
}

/* Returns nonzero when the tap was inside the calendar section. */
static int calendar_section_tap(int x, int y)
{
  /* Full width: the margins also count, which enlarges the page switcher. */
  if (y < CAL_HEADER_Y || y >= ScreenHeight() - SETTINGS_BOTTOM_OFFSET - 15)
  {
    return 0;
  }
  const CalendarStatus status = calendar_status();
  const int button_x = SETTINGS_MARGIN + button_width() + SETTINGS_GAP;

  if (y < CAL_HEADER_Y + CAL_HEADER_HEIGHT)
  {
    if (x < button_x)
    {
      return 1;
    }
    if (status == CALENDAR_DISCONNECTED)
    {
      /* The browser takes over the screen; the result is picked up on return. */
      calendar_connect();
      redraw_settings();
    }
    else if (status == CALENDAR_SIGNING_IN)
    {
      calendar_cancel_sign_in();
      redraw_settings();
    }
    else if (status == CALENDAR_SYNCING || status == CALENDAR_CONNECTED)
    {
      calendar_disconnect();
      calendar_draft.configured = 0;
      calendar_draft.count = 0;
      calendar_page = 0;
      redraw_settings();
    }
    return 1;
  }
  if (status == CALENDAR_UNAVAILABLE)
  {
    return 1;
  }
  if (y >= CAL_DAYS_Y && y < CAL_DAYS_Y + CAL_DAYS_HEIGHT)
  {
    const int option_width = calendar_option_width();
    for (int i = 0; i < 4; ++i)
    {
      const int option_x = SETTINGS_MARGIN + calendar_label_width() + i * (option_width + SETTINGS_GAP);
      if (x >= option_x && x < option_x + option_width)
      {
        calendar_draft.days = day_options[i];
        redraw_settings();
      }
    }
    return 1;
  }
  if (y >= CAL_LIST_Y)
  {
    const CalendarInfo *list;
    const int count = calendar_list(&list);
    const int per_page = calendar_per_page(count);
    const int pages = calendar_page_count(count);
    const int row = (y - CAL_LIST_Y) / CAL_ROW_HEIGHT;
    if (pages > 1 && row >= per_page)
    {
      /* The whole row and the gap below it act as a large page switcher. */
      if (x < ScreenWidth() / 2)
      {
        calendar_page = (calendar_page + pages - 1) % pages;
      }
      else
      {
        calendar_page = (calendar_page + 1) % pages;
      }
      redraw_settings();
      return 1;
    }
    const int index = calendar_page * per_page + row;
    if (row < per_page && index < count)
    {
      calendar_toggle(&calendar_draft, &list[index]);
      redraw_settings();
    }
  }
  return 1;
}

int settings_handle_swipe(int x, int y, int direction)
{
  (void)x;
  if (!screen_open || keyboard_open || result_count != 0 || y < CAL_LIST_Y
      || y >= ScreenHeight() - SETTINGS_BOTTOM_OFFSET - 15)
  {
    return 0;
  }
  const CalendarInfo *list;
  const int pages = calendar_page_count(calendar_list(&list));
  if (pages > 1)
  {
    calendar_page = (calendar_page + direction + pages) % pages;
    redraw_settings();
  }
  return 1;
}

static void city_keyboard_done(char *text)
{
  keyboard_open = 0;
  if (!screen_open)
  {
    return;
  }
  /* The keyboard edits a separate buffer so cancellation preserves the draft. */
  if (text != NULL)
  {
    const size_t length = strlen(text);

    if (length >= sizeof(draft.city))
    {
      fprintf(stderr, "Settings: city name is too long\n");
      snprintf(search_message, sizeof(search_message), "%s", tr(STR_CITY_TOO_LONG));
    }
    else if (strcmp(draft.city, text) != 0)
    {
      memcpy(draft.city, text, length + 1);

      /* Edited text must be resolved before it becomes a usable location. */
      draft.location_selected = 0;
      draft.region[0] = '\0';
      draft.country[0] = '\0';
      draft.timezone[0] = '\0';
      draft.latitude = 0.0;
      draft.longitude = 0.0;
      result_count = 0;
      selected_result = -1;
      search_message[0] = '\0';
    }
  }

  redraw_settings();
}

void settings_draw(ifont *font, int foreground_color, int background_color)
{
  settings_font = font;
  settings_foreground = foreground_color;
  settings_background = background_color;

  if (keyboard_open)
  {
    return;
  }

  const int content_width = ScreenWidth() - 2 * SETTINGS_MARGIN;
  const int width = button_width();
  const int y = ScreenHeight() - SETTINGS_BOTTOM_OFFSET;
  const int save_x = SETTINGS_MARGIN + width + SETTINGS_GAP;

  FillArea(0, 0, ScreenWidth(), ScreenHeight(), background_color);
  SetFont(heading_font(font), foreground_color);

  DrawTextRect(SETTINGS_MARGIN, 40, content_width, 80,
               tr(STR_SETTINGS), ALIGN_CENTER | VALIGN_MIDDLE);

  DrawLine(SETTINGS_MARGIN, 130, ScreenWidth() - SETTINGS_MARGIN, 130,
           foreground_color);

  DrawTextRect(SETTINGS_MARGIN, 155, content_width, 60,
               tr(STR_WEATHER_CITY), ALIGN_LEFT | VALIGN_MIDDLE);
  SetFont(font, foreground_color);

  DrawRect(SETTINGS_MARGIN, CITY_Y, content_width, CITY_HEIGHT,
           foreground_color);
  DrawTextRect(SETTINGS_MARGIN + 15, CITY_Y,
               content_width - 30, CITY_HEIGHT,
               draft.city, ALIGN_LEFT | VALIGN_MIDDLE);

  const int search_width = button_width();
  const int detect_x = SETTINGS_MARGIN + search_width + SETTINGS_GAP;

  DrawRect(SETTINGS_MARGIN, SEARCH_Y, search_width, SEARCH_HEIGHT,
           foreground_color);
  DrawTextRect(SETTINGS_MARGIN, SEARCH_Y, search_width, SEARCH_HEIGHT,
               search_busy && !search_by_ip ? tr(STR_SEARCHING) : tr(STR_SEARCH),
               ALIGN_CENTER | VALIGN_MIDDLE);

  DrawRect(detect_x, SEARCH_Y, search_width, SEARCH_HEIGHT,
           foreground_color);
  DrawTextRect(detect_x, SEARCH_Y, search_width, SEARCH_HEIGHT,
               search_busy && search_by_ip ? tr(STR_DETECTING) : tr(STR_DETECT_BY_IP),
               ALIGN_CENTER | VALIGN_MIDDLE);

  char interval_title[160];
  snprintf(interval_title, sizeof(interval_title), "%s (%s)",
           tr(STR_UPDATE_INTERVAL), tr(STR_MINUTES_SHORT));
  SetFont(heading_font(font), foreground_color);
  DrawTextRect(SETTINGS_MARGIN, 435, content_width, 60,
               interval_title, ALIGN_LEFT | VALIGN_MIDDLE);
  SetFont(font, foreground_color);

  const int option_width = interval_width();

  for (int i = 0; i < 4; ++i)
  {
    const int option_x = SETTINGS_MARGIN + i * (option_width + SETTINGS_GAP);
    const int selected = draft.refresh_minutes == refresh_options[i];
    char label[16];

    snprintf(label, sizeof(label), "%d", refresh_options[i]);

    if (selected)
    {
      FillArea(option_x, INTERVAL_Y, option_width, INTERVAL_HEIGHT,
               foreground_color);
    }
    else
    {
      DrawRect(option_x, INTERVAL_Y, option_width, INTERVAL_HEIGHT,
               foreground_color);
    }

    SetFont(font, selected ? background_color : foreground_color);
    DrawTextRect(option_x, INTERVAL_Y, option_width, INTERVAL_HEIGHT,
                 label, ALIGN_CENTER | VALIGN_MIDDLE);
  }

  SetFont(font, foreground_color);

  DrawTextRect(SETTINGS_MARGIN, 600, content_width, 55,
               search_message, ALIGN_LEFT | VALIGN_MIDDLE);

  /* Bound the list to the available space above Save/Cancel. */
  for (int i = 0; i < result_count; ++i)
  {
    const int row_y = RESULTS_Y + i * RESULT_HEIGHT;
    if (row_y + RESULT_HEIGHT > y - 15)
    {
      break;
    }
    const int selected = i == selected_result;
    char details[260];
    snprintf(details, sizeof(details), "%s%s%s",
             results[i].region,
             results[i].region[0] && results[i].country[0] ? ", " : "",
             results[i].country);
    if (selected)
    {
      FillArea(SETTINGS_MARGIN, row_y, content_width, RESULT_HEIGHT,
               foreground_color);
    }
    else
    {
      DrawRect(SETTINGS_MARGIN, row_y, content_width, RESULT_HEIGHT,
               foreground_color);
    }
    SetFont(font, selected ? background_color : foreground_color);
    DrawTextRect(SETTINGS_MARGIN + 12, row_y, content_width - 24, 50,
                 results[i].city, ALIGN_LEFT | VALIGN_MIDDLE);
    DrawTextRect(SETTINGS_MARGIN + 12, row_y + 50, content_width - 24, 50,
                 details, ALIGN_LEFT | VALIGN_MIDDLE);
  }
  SetFont(font, foreground_color);

  if (result_count == 0)
  {
    draw_calendar_section(font, foreground_color, background_color);
    SetFont(font, foreground_color);
  }

  DrawRect(SETTINGS_MARGIN, y, width, SETTINGS_BUTTON_HEIGHT,
           foreground_color);
  DrawTextRect(SETTINGS_MARGIN, y, width, SETTINGS_BUTTON_HEIGHT,
               tr(STR_CANCEL), ALIGN_CENTER | VALIGN_MIDDLE);

  DrawRect(save_x, y, width, SETTINGS_BUTTON_HEIGHT, foreground_color);
  DrawTextRect(save_x, y, width, SETTINGS_BUTTON_HEIGHT,
               tr(STR_SAVE), ALIGN_CENTER | VALIGN_MIDDLE);

  FullUpdate();
}

SettingsAction settings_handle_tap(int x, int y)
{
  const int content_right = ScreenWidth() - SETTINGS_MARGIN;

  if (x >= SETTINGS_MARGIN && x < content_right && y >= CITY_Y && y < CITY_Y + CITY_HEIGHT)
  {
    if (search_busy)
    {
      return SETTINGS_NONE;
    }
    memcpy(city_input, draft.city, sizeof(city_input));

    /* Keep the buffer alive for the entire keyboard session. */
    keyboard_open = 1;
    OpenKeyboard(tr(STR_CITY), city_input, sizeof(city_input) - 1,
                 KBD_NORMAL, city_keyboard_done);
    return SETTINGS_NONE;
  }

  if (y >= SEARCH_Y && y < SEARCH_Y + SEARCH_HEIGHT)
  {
    const int search_width = button_width();
    const int detect_x = SETTINGS_MARGIN + search_width + SETTINGS_GAP;

    if (x >= SETTINGS_MARGIN && x < SETTINGS_MARGIN + search_width)
    {
      start_search(0);
    }
    else if (x >= detect_x && x < detect_x + search_width)
    {
      start_search(1);
    }

    return SETTINGS_NONE;
  }

  if (result_count == 0 && calendar_section_tap(x, y))
  {
    return SETTINGS_NONE;
  }

  if (!search_busy && x >= SETTINGS_MARGIN && x < content_right && y >= RESULTS_Y && y < ScreenHeight() - SETTINGS_BOTTOM_OFFSET - 15)
  {
    const int index = (y - RESULTS_Y) / RESULT_HEIGHT;
    if (index < result_count && RESULTS_Y + (index + 1) * RESULT_HEIGHT <= ScreenHeight() - SETTINGS_BOTTOM_OFFSET - 15)
    {
      const GeoPlace *place = &results[index];
      memcpy(draft.city, place->city, sizeof(draft.city));
      memcpy(draft.region, place->region, sizeof(draft.region));
      memcpy(draft.country, place->country, sizeof(draft.country));
      memcpy(draft.timezone, place->timezone, sizeof(draft.timezone));
      draft.latitude = place->latitude;
      draft.longitude = place->longitude;
      draft.location_selected = 1;
      selected_result = -1;
      result_count = 0;
      snprintf(search_message, sizeof(search_message), "%s: %.60s, %.60s", tr(STR_SELECTED), draft.city, draft.country);
      redraw_settings();
    }
    return SETTINGS_NONE;
  }

  if (y >= INTERVAL_Y && y < INTERVAL_Y + INTERVAL_HEIGHT)
  {
    const int option_width = interval_width();

    for (int i = 0; i < 4; ++i)
    {
      const int option_x = SETTINGS_MARGIN + i * (option_width + SETTINGS_GAP);

      if (x >= option_x && x < option_x + option_width)
      {
        draft.refresh_minutes = refresh_options[i];
        settings_draw(settings_font, settings_foreground,
                      settings_background);
        return SETTINGS_NONE;
      }
    }
  }

  const int width = button_width();
  const int button_y = ScreenHeight() - SETTINGS_BOTTOM_OFFSET;
  const int save_x = SETTINGS_MARGIN + width + SETTINGS_GAP;

  if (y < button_y || y >= button_y + SETTINGS_BUTTON_HEIGHT)
  {
    return SETTINGS_NONE;
  }

  if (x >= SETTINGS_MARGIN && x < SETTINGS_MARGIN + width)
  {
    return SETTINGS_CANCEL;
  }

  if (x >= save_x && x < save_x + width)
  {
    if (search_busy || !draft.location_selected)
    {
      fprintf(stderr, "Settings: select a resolved city before saving\n");
      snprintf(search_message, sizeof(search_message), "%s",
               search_busy ? tr(STR_PLEASE_WAIT) : tr(STR_SEARCH_AND_SELECT_FIRST));
      redraw_settings();
      return SETTINGS_NONE;
    }
    return SETTINGS_SAVE;
  }
  return SETTINGS_NONE;
}

void settings_set_active(int active)
{
  screen_active = active;
}

void settings_end(void)
{
  calendar_set_listener(NULL);
  screen_open = 0;
  screen_active = 0;
  keyboard_open = 0;
  search_busy = 0;
  network_finished = 0;
  ClearTimer(connection_poll);
  geocoding_cancel();
}