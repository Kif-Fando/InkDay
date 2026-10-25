#include <inkview.h>
#include "weather.h"
#include "settings.h"
#include "settings_store.h"
#include "geocoding.h"
#include "i18n.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Available on the device firmware, absent from SDK_481 headers. */
extern void SetPanelInvert(int enabled) __attribute__((weak));

/* Header coordinates are relative to the current InkView content area. */
enum
{
  HEADER_X = 10,
  HEADER_Y = 0,
  HEADER_HEIGHT = 250,
  HEADER_RIGHT_MARGIN = 10,
  CLOCK_WIDTH = 620,
  DATE_GAP = 10,
  DATE_Y = 30,
  DAY_HEIGHT = 100,
  DATE_LINE_HEIGHT = 50
};

/* The application owns fonts, theme and the full-screen refresh policy. */
static ifont *clock_font;
static ifont *day_font;
static ifont *date_font;
static ifont *weather_footer_font;
static int foreground;
static int full_redraw = 1;
static int panel_visible;
static int settings_visible;
static int foreground_color = BLACK;
static int background_color = WHITE;
static WeatherSettings weather_settings = {
    .refresh_minutes = 30};
static int initial_location_pending;
static int settings_load_failed;
enum
{
  LOCATION_IDLE,
  LOCATION_CONNECTING,
  LOCATION_LOOKUP,
  LOCATION_READY,
  LOCATION_FAILED
};
static int initial_location_state;
static int initial_network_done;
static int initial_network_status;
static time_t initial_network_deadline;
static WeatherSettings detected_settings;
static char initial_location_error[160];

/* InkView translation keys follow tm_wday (Sunday first) and tm_mon. */
static const char *const weekdays[] = {
    "@Sunday", "@Monday", "@Tuesday", "@Wednesday",
    "@Thursday", "@Friday", "@Saturday"};

static const char *const months[] = {
    "@of_January", "@of_February", "@of_March", "@of_April",
    "@of_May", "@of_June", "@of_July", "@of_August",
    "@of_September", "@of_October", "@of_November", "@of_December"};

static void configure_weather_view(void)
{
  const WeatherView view = {
      .x = HEADER_X,
      .y = HEADER_Y + HEADER_HEIGHT + 20,
      .width = ScreenWidth() - HEADER_X - HEADER_RIGHT_MARGIN,
      .foreground_color = foreground_color,
      .background_color = background_color,
      .temperature_font = day_font,
      .detail_font = date_font,
      .footer_font = weather_footer_font,
      .city = initial_location_state != LOCATION_IDLE
                  ? tr(STR_DETECTING_LOCATION)
                  : weather_settings.city,
      .region = weather_settings.region};
  weather_set_view(&view);
}

static void draw_clock(const char *text)
{
  SetFont(clock_font, foreground_color);
  DrawTextRect(HEADER_X, HEADER_Y, CLOCK_WIDTH, HEADER_HEIGHT,
               text, ALIGN_LEFT | VALIGN_TOP);
}

static void draw_date(const struct tm *local)
{
  const int date_x = HEADER_X + CLOCK_WIDTH + DATE_GAP;
  const int date_width = ScreenWidth() - date_x - HEADER_RIGHT_MARGIN;
  char day[3];

  snprintf(day, sizeof(day), "%02d", local->tm_mday);

  SetFont(day_font, foreground_color);
  DrawTextRect(date_x, DATE_Y, date_width, DAY_HEIGHT,
               day, ALIGN_LEFT | VALIGN_TOP);

  SetFont(date_font, foreground_color);
  DrawTextRect(date_x, DATE_Y + DAY_HEIGHT, date_width, DATE_LINE_HEIGHT,
               GetLangText(months[local->tm_mon]), ALIGN_LEFT | VALIGN_TOP);
  DrawTextRect(date_x, DATE_Y + DAY_HEIGHT + DATE_LINE_HEIGHT,
               date_width, DATE_LINE_HEIGHT,
               GetLangText(weekdays[local->tm_wday]), ALIGN_LEFT | VALIGN_TOP);
}

static void draw_battery(void)
{
  int battery = GetBatteryPower();
  char battery_text[16];

  if (battery >= 0 && battery <= 100)
  {
    snprintf(battery_text, sizeof(battery_text), "%d%%", battery);
  }
  else
  {
    fprintf(stderr, "Unexpected battery level: %d\n", battery);
    snprintf(battery_text, sizeof(battery_text), "N/A");
  }

  /* Anchor the percentage to the right edge and place the icon beside it. */
  SetFont(date_font, foreground_color);
  const int right = ScreenWidth() - 20;
  const int text_width = StringWidth(battery_text);
  const int text_x = right - text_width;
  const int battery_y = DATE_Y + (DAY_HEIGHT - DATE_LINE_HEIGHT) / 2;

  DrawTextRect(text_x, battery_y, text_width, DATE_LINE_HEIGHT,
               battery_text, ALIGN_RIGHT | VALIGN_MIDDLE);

  const int icon_width = 48;
  const int icon_height = 24;
  const int icon_x = text_x - 12 - icon_width - 4;
  const int icon_y = battery_y + (DATE_LINE_HEIGHT - icon_height) / 2;

  /* Two outlines form a two-pixel border; the small block is the terminal. */
  DrawRect(icon_x, icon_y, icon_width, icon_height, foreground_color);
  DrawRect(icon_x + 1, icon_y + 1,
           icon_width - 2, icon_height - 2, foreground_color);

  FillArea(icon_x + icon_width, icon_y + 7, 4, 10, foreground_color);

  if (battery > 0 && battery <= 100)
  {
    int fill_width = (icon_width - 8) * battery / 100;
    if (fill_width > 0)
    {
      FillArea(icon_x + 4, icon_y + 4,
               fill_width, icon_height - 8, foreground_color);
    }
  }
}

static void draw_settings_icon(void)
{
  const int x = ScreenWidth() - 55;
  const int y = DATE_Y + DAY_HEIGHT + 25;

  /* Eight teeth around the gear, relative to its center. */
  static const int points[][2] = {
      {28, -6}, {28, 6}, {21, 6}, {19, 11}, {24, 16}, {16, 24}, {11, 19}, {6, 21}, {6, 28}, {-6, 28}, {-6, 21}, {-11, 19}, {-16, 24}, {-24, 16}, {-19, 11}, {-21, 6}, {-28, 6}, {-28, -6}, {-21, -6}, {-19, -11}, {-24, -16}, {-16, -24}, {-11, -19}, {-6, -21}, {-6, -28}, {6, -28}, {6, -21}, {11, -19}, {16, -24}, {24, -16}, {19, -11}, {21, -6}};

  /* Use the same 3x3 brush as the weather icons. */
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dy = -1; dy <= 1; ++dy)
    {
      for (int i = 0; i < 32; ++i)
      {
        const int next = (i + 1) % 32;
        DrawLine(x + points[i][0] + dx,
                 y + points[i][1] + dy,
                 x + points[next][0] + dx,
                 y + points[next][1] + dy,
                 foreground_color);
      }

      DrawCircle(x + dx, y + dy, 8, foreground_color);
    }
  }
}

static void draw_settings(void)
{
  settings_draw(date_font, foreground_color, background_color);
}

static void draw_header(const char *time_text, const struct tm *local)
{
  const int header_width = ScreenWidth() - HEADER_X - HEADER_RIGHT_MARGIN;

  /* Minute updates clear only the header, leaving widget pixels intact. */
  if (full_redraw)
  {
    FillArea(0, 0, ScreenWidth(), ScreenHeight(), background_color);
  }
  else
  {
    FillArea(HEADER_X, HEADER_Y, header_width, HEADER_HEIGHT,
             background_color);
  }

  draw_clock(time_text);
  draw_date(local);
  draw_battery();
  draw_settings_icon();

  /* Restoration and panel changes redraw widgets before the full refresh. */
  if (full_redraw)
  {
    configure_weather_view();
    weather_draw();
    if (panel_visible)
    {
      if (SetPanelInvert != NULL)
      {
        SetPanelInvert(background_color == BLACK);
      }
      else
      {
        fprintf(stderr, "Panel: SetPanelInvert is unavailable\n");
      }
      DrawPanel(NULL, "", "Clock", -1);
    }
    FullUpdate();
    full_redraw = 0;
  }
  else
  {
    PartialUpdate(HEADER_X, HEADER_Y, header_width, HEADER_HEIGHT);
  }
}

static void update_clock(void)
{
  struct timeval now;
  char text[6];

  /* Keep a single timer even when a tap or window event triggers a redraw. */
  ClearTimer(update_clock);

  if (!foreground)
  {
    return;
  }

  if (settings_visible)
  {
    draw_settings();
    return;
  }

  if (gettimeofday(&now, NULL) != 0)
  {
    perror("gettimeofday");
    CloseApp();
    return;
  }

  const struct tm *local = localtime(&now.tv_sec);
  if (local == NULL || strftime(text, sizeof(text), "%H:%M", local) == 0)
  {
    fprintf(stderr, "Cannot format local time\n");
    CloseApp();
    return;
  }

  /* InkView may also use localtime(), whose result lives in shared storage. */
  const struct tm local_time = *local;
  const int delay_ms = (60 - local_time.tm_sec) * 1000 - (int)(now.tv_usec / 1000);

  draw_header(text, &local_time);

  /* Align the next update to the next minute rather than waiting 60 seconds. */
  SetHardTimer("clock-minute", update_clock, delay_ms);
}

static void set_panel_visible(int visible)
{
  panel_visible = visible;
  SetPanelType(visible ? PANEL_ENABLED : PANEL_DISABLED);
  SetShowPanelReader(visible);
}

static void open_settings(void)
{
  settings_begin(&weather_settings);
  settings_visible = 1;

  /* Suspend dashboard drawing without discarding weather data. */
  ClearTimer(update_clock);
  weather_set_active(0);

  /* Hide the panel temporarily, preserving the dashboard preference. */
  SetPanelType(PANEL_DISABLED);
  SetShowPanelReader(0);

  draw_settings();
}

static void close_settings(int save)
{
  if (save)
  {
    const WeatherSettings *draft = settings_get_draft();

    if (!draft->location_selected)
    {
      fprintf(stderr, "Settings: location has not been selected\n");
      Message(ICON_WARNING, tr(STR_SETTINGS),
              tr(STR_SEARCH_AND_SELECT_FIRST), 3000);
      return;
    }

    if (!weather_can_configure())
    {
      Message(ICON_WARNING, tr(STR_SETTINGS), tr(STR_SETTINGS_BUSY), 4000);
      return;
    }

    if (!settings_store_save(draft))
    {
      Message(ICON_WARNING, tr(STR_SETTINGS),
              tr(STR_SETTINGS_SAVE_FAILED), 4000);
      return;
    }
    /* No event dispatch occurs between the readiness check and application. */
    if (!weather_configure(draft->latitude, draft->longitude,
                           draft->refresh_minutes))
    {
      fprintf(stderr, "Settings: saved configuration could not be applied\n");
      Message(ICON_WARNING, tr(STR_SETTINGS),
              tr(STR_SETTINGS_APPLY_FAILED), 4000);
      return;
    }
    weather_settings = *draft;
    fprintf(stderr, "Settings: applied city=%s, interval=%d min\n",
            weather_settings.city, weather_settings.refresh_minutes);
  }

  settings_end();
  settings_visible = 0;
  set_panel_visible(panel_visible);
  full_redraw = 1;
  update_clock();
  weather_set_active(weather_settings.location_selected);
}

static void initial_location_tick(void);

static void refresh_location_block(void)
{
  if (foreground && !settings_visible)
  {
    configure_weather_view();
    weather_draw();
    PartialUpdate(HEADER_X, HEADER_Y + HEADER_HEIGHT + 20,
                  ScreenWidth() - HEADER_X - HEADER_RIGHT_MARGIN,
                  WEATHER_HEIGHT);
  }
}

static void initial_location_fail(const char *message)
{
  fprintf(stderr, "Initial location: %s\n", message);
  snprintf(initial_location_error, sizeof(initial_location_error), "%s", message);
  initial_location_state = LOCATION_FAILED;
}

static int initial_network_completed(int status)
{
  if (initial_location_state == LOCATION_CONNECTING)
  {
    initial_network_status = status;
    initial_network_done = 1;
  }
  return 0;
}

static void initial_lookup_completed(const GeoPlace *places, int count,
                                     const char *error)
{
  if (initial_location_state != LOCATION_LOOKUP)
  {
    return;
  }
  if (error != NULL || count != 1)
  {
    if (error != NULL)
    {
      fprintf(stderr, "Initial location: %s\n", error);
    }
    initial_location_fail(tr(STR_LOCATION_NOT_DETECTED));
    return;
  }
  detected_settings = weather_settings;
  memcpy(detected_settings.city, places[0].city, sizeof(detected_settings.city));
  memcpy(detected_settings.region, places[0].region, sizeof(detected_settings.region));
  memcpy(detected_settings.country, places[0].country, sizeof(detected_settings.country));
  memcpy(detected_settings.timezone, places[0].timezone, sizeof(detected_settings.timezone));
  detected_settings.latitude = places[0].latitude;
  detected_settings.longitude = places[0].longitude;
  detected_settings.location_selected = 1;
  initial_location_state = LOCATION_READY;
}

static void initial_location_tick(void)
{
  ClearTimer(initial_location_tick);
  /* Background callbacks may finish, but UI and application wait for foreground. */
  if (!foreground || initial_location_state == LOCATION_IDLE)
  {
    return;
  }
  if (initial_location_state == LOCATION_CONNECTING)
  {
    if (initial_network_done)
    {
      if (initial_network_status != NET_OK)
      {
        initial_location_fail(tr(STR_NETWORK_ERROR));
      }
      else
      {
        initial_location_state = LOCATION_LOOKUP;
        if (!geocoding_detect_ip(initial_lookup_completed))
        {
          initial_location_fail(tr(STR_CANNOT_START_LOOKUP));
        }
      }
    }
    else if (time(NULL) >= initial_network_deadline)
    {
      initial_location_fail(tr(STR_TIMEOUT));
    }
  }
  if (initial_location_state == LOCATION_READY)
  {
    if (!weather_can_configure())
    {
      initial_location_fail(tr(STR_SETTINGS_BUSY));
    }
    else if (!settings_store_save(&detected_settings))
    {
      initial_location_fail(tr(STR_SETTINGS_SAVE_FAILED));
    }
    else if (!weather_configure(detected_settings.latitude,
                                detected_settings.longitude,
                                detected_settings.refresh_minutes))
    {
      initial_location_fail(tr(STR_SETTINGS_APPLY_FAILED));
    }
    else
    {
      weather_settings = detected_settings;
      initial_location_state = LOCATION_IDLE;
      refresh_location_block();
      weather_set_active(1);
      return;
    }
  }
  if (initial_location_state == LOCATION_FAILED)
  {
    initial_location_state = LOCATION_IDLE;
    geocoding_cancel();
    open_settings();
    Message(ICON_WARNING, tr(STR_SETTINGS), initial_location_error, 4000);
    return;
  }
  SetHardTimer("initial-location", initial_location_tick, 500);
}

static void start_initial_location(void)
{
  const time_t now = time(NULL);
  initial_location_state = LOCATION_CONNECTING;
  initial_network_done = 0;
  refresh_location_block();
  fprintf(stderr, "Initial location: requesting approximate location from ipwho.is\n");
  if (now == (time_t)-1)
  {
    initial_location_fail(tr(STR_CANNOT_START_CONNECTION));
  }
  else
  {
    initial_network_deadline = now + 30;
    const int result = NetConnectAsync(initial_network_completed);
    if (result < 0 && !initial_network_done)
    {
      initial_location_fail(tr(STR_CANNOT_START_CONNECTION));
    }
  }
  SetHardTimer("initial-location", initial_location_tick, 500);
}

static int main_handler(int event, int par1, int par2)
{
  (void)par1;
  (void)par2;

  switch (event)
  {
  case EVT_INIT:
  {
    const int loaded = settings_store_load(&weather_settings);

    initial_location_pending = loaded == 0;
    settings_load_failed = loaded < 0;

    if (loaded == 1 && !weather_configure(weather_settings.latitude,
                                          weather_settings.longitude,
                                          weather_settings.refresh_minutes))
    {
      fprintf(stderr, "Settings: cannot initialize saved weather configuration\n");
      CloseApp();
      return 0;
    }

    /* Fonts are shared with widgets but opened and closed only here. */
    clock_font = OpenFont("Roboto", 240, 1);
    if (clock_font == NULL)
    {
      fprintf(stderr, "Cannot open clock font\n");
      CloseApp();
      return 0;
    }

    date_font = OpenFont("Roboto", 45, 1);
    if (date_font == NULL)
    {
      fprintf(stderr, "Cannot open date font\n");
      CloseApp();
      return 0;
    }

    day_font = OpenFont("Roboto", 100, 1);
    if (day_font == NULL)
    {
      fprintf(stderr, "Cannot open day font\n");
      CloseApp();
      return 0;
    }

    weather_footer_font = OpenFont("Roboto", 34, 0);
    if (weather_footer_font == NULL)
    {
      fprintf(stderr, "Cannot open weather footer font\n");
      CloseApp();
      return 0;
    }

    set_panel_visible(0);
    /* SSH launches need explicit activation to bring the app to the front. */
    SetActiveTask(GetCurrentTask(), 0);
    break;
  }

  case EVT_FOREGROUND:
    foreground = 1;
    full_redraw = 1;
    /* Read a fresh config without writing back system settings. */
    iconfig *config = OpenConfig(GLOBALCONFIGFILE, NULL);
    if (config == NULL)
    {
      fprintf(stderr, "Cannot access global configuration\n");
    }
    else
    {
      int inverted = ReadInt(config, "screen_mode_inverted", -1);
      if (inverted == 0 || inverted == 1)
      {
        foreground_color = inverted ? WHITE : BLACK;
        background_color = inverted ? BLACK : WHITE;
      }
      else
      {
        fprintf(stderr, "Invalid screen_mode_inverted: %d\n", inverted);
      }
      CloseConfigNoSave(config);
    }
    /* Configure/draw the widget before resuming its asynchronous timer. */
    update_clock();
    settings_set_active(settings_visible);

    if (settings_load_failed)
    {
      settings_load_failed = 0;
      open_settings();
      Message(ICON_WARNING, tr(STR_SETTINGS),
              tr(STR_SETTINGS_UNREADABLE), 4000);
    }
    else if (initial_location_pending)
    {
      /* Attempt once per launch, not on every foreground event. */
      initial_location_pending = 0;
      start_initial_location();
    }

    if (initial_location_state != LOCATION_IDLE)
    {
      initial_location_tick();
    }
    weather_set_active(!settings_visible && weather_settings.location_selected);
    break;

  case EVT_SHOW:
  case EVT_REPAINT:
    if (foreground)
    {
      full_redraw = 1;
      update_clock();
    }
    break;

  case EVT_BACKGROUND:
  case EVT_HIDE:
    /* Stop screen updates; weather may still reap an in-flight child. */
    foreground = 0;
    ClearTimer(initial_location_tick);
    settings_set_active(0);
    ClearTimer(update_clock);
    weather_set_active(0);
    break;

  case EVT_POINTERUP:
    if (foreground)
    {
      if (initial_location_state != LOCATION_IDLE)
      {
        /* Avoid concurrent network-manager requests from settings. */
        fprintf(stderr, "Initial location: please wait for detection to finish\n");
        break;
      }
      if (settings_visible)
      {
        const SettingsAction action = settings_handle_tap(par1, par2);
        if (action != SETTINGS_NONE)
        {
          close_settings(action == SETTINGS_SAVE);
        }
        break;
      }

      /* Give the gear a larger touch target than its visible outline. */
      const int gear_x = ScreenWidth() - 55;
      const int gear_y = DATE_Y + DAY_HEIGHT + 25;

      if (par1 >= gear_x - 40 && par1 < gear_x + 40 && par2 >= gear_y - 40 && par2 < gear_y + 40)
      {
        open_settings();
        break;
      }

      if (weather_settings.location_selected && weather_handle_tap(par1, par2))
      {
        break;
      }

      set_panel_visible(!panel_visible);
      full_redraw = 1;
      update_clock();
    }
    break;

  case EVT_EXIT:
    foreground = 0;
    initial_location_state = LOCATION_IDLE;
    ClearTimer(initial_location_tick);
    settings_end();
    ClearTimer(update_clock);
    /* Stop widget callbacks before releasing their borrowed fonts. */
    weather_shutdown();
    if (clock_font != NULL)
    {
      CloseFont(clock_font);
      clock_font = NULL;
    }
    if (date_font != NULL)
    {
      CloseFont(date_font);
      date_font = NULL;
    }
    if (day_font != NULL)
    {
      CloseFont(day_font);
      day_font = NULL;
    }
    if (weather_footer_font != NULL)
    {
      CloseFont(weather_footer_font);
      weather_footer_font = NULL;
    }
    break;
  }

  return 0;
}

int main(void)
{
  InkViewMain(main_handler);
  return 0;
}
