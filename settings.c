#include "settings.h"
#include "geocoding.h"
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
  RESULT_HEIGHT = 105
};

static const int refresh_options[] = {15, 30, 60, 120};

static WeatherSettings draft;
static char city_input[sizeof(draft.city)];
static ifont *settings_font;
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
    search_error(error);
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
             "IP location: %.60s, %.60s", draft.city, draft.country);
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
               ? (search_by_ip ? "Approximate IP location: tap to select"
                               : "Select a city:")
               : "No cities found");
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
      search_error("Network connection failed");
      return;
    }
    snprintf(search_message, sizeof(search_message), "Searching...");
    redraw_settings();
    const int started = search_by_ip
                            ? geocoding_detect_ip(search_completed)
                            : geocoding_start(draft.city, search_completed);

    if (!started)
    {
      search_error("Cannot start location lookup");
    }
    return;
  }
  if (time(NULL) >= network_deadline)
  {
    /* InkView offers no per-attempt cancel token: wait for its late callback
       before allowing another network attempt. */
    search_error("Connection timed out; try again after network settles");
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
    search_error("Previous network connection is still finishing");
    return;
  }
  if (!by_ip && (draft.city[0] == '\0' || strspn(draft.city, " \t\r\n") == strlen(draft.city)))
  {
    search_error("Enter a city name");
    return;
  }
  const time_t now = time(NULL);
  if (now == (time_t)-1)
  {
    search_error("Cannot read connection time");
    return;
  }
  result_count = 0;
  selected_result = -1;
  search_by_ip = by_ip;
  search_busy = 1;
  network_finished = 0;
  network_outstanding = 1;
  network_deadline = now + 30;
  snprintf(search_message, sizeof(search_message), "Connecting...");
  redraw_settings();

  const int status = NetConnectAsync(connection_completed);
  if (status < 0 && network_outstanding)
  {
    network_outstanding = 0;
    search_error("Cannot start network connection");
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

void settings_begin(const WeatherSettings *current)
{
  settings_end();
  draft = *current;
  screen_open = 1;
  screen_active = 1;
  result_count = 0;
  selected_result = -1;
  search_message[0] = '\0';
}

const WeatherSettings *settings_get_draft(void)
{
  return &draft;
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
      snprintf(search_message, sizeof(search_message), "City name is too long");
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
  SetFont(font, foreground_color);

  DrawTextRect(SETTINGS_MARGIN, 40, content_width, 80,
               GetLangText("@Settings"), ALIGN_LEFT | VALIGN_MIDDLE);

  DrawLine(SETTINGS_MARGIN, 130, ScreenWidth() - SETTINGS_MARGIN, 130,
           foreground_color);

  /* Temporary English labels; application translations will use these meanings. */
  DrawTextRect(SETTINGS_MARGIN, 155, content_width, 60,
               "Weather / City", ALIGN_LEFT | VALIGN_MIDDLE);

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
               search_busy && !search_by_ip ? "Searching..." : "Search",
               ALIGN_CENTER | VALIGN_MIDDLE);

  DrawRect(detect_x, SEARCH_Y, search_width, SEARCH_HEIGHT,
           foreground_color);
  DrawTextRect(detect_x, SEARCH_Y, search_width, SEARCH_HEIGHT,
               search_busy && search_by_ip ? "Detecting..." : "Detect by IP",
               ALIGN_CENTER | VALIGN_MIDDLE);

  DrawTextRect(SETTINGS_MARGIN, 435, content_width, 60,
               "Update interval (min)", ALIGN_LEFT | VALIGN_MIDDLE);

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

  DrawRect(SETTINGS_MARGIN, y, width, SETTINGS_BUTTON_HEIGHT,
           foreground_color);
  DrawTextRect(SETTINGS_MARGIN, y, width, SETTINGS_BUTTON_HEIGHT,
               GetLangText("@Cancel"), ALIGN_CENTER | VALIGN_MIDDLE);

  DrawRect(save_x, y, width, SETTINGS_BUTTON_HEIGHT, foreground_color);
  DrawTextRect(save_x, y, width, SETTINGS_BUTTON_HEIGHT,
               GetLangText("@Save"), ALIGN_CENTER | VALIGN_MIDDLE);

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
    OpenKeyboard("City", city_input, sizeof(city_input) - 1,
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
      snprintf(search_message, sizeof(search_message), "Selected: %.60s, %.60s", draft.city, draft.country);
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
               search_busy ? "Wait for search or cancel" : "Search and select a city first");
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
  screen_open = 0;
  screen_active = 0;
  keyboard_open = 0;
  search_busy = 0;
  network_finished = 0;
  ClearTimer(connection_poll);
  geocoding_cancel();
}