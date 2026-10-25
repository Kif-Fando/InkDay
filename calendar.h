#ifndef INKDAY_CALENDAR_H
#define INKDAY_CALENDAR_H

#include <inkview.h>

enum
{
  CALENDAR_MAX = 16,
  CALENDAR_ID_SIZE = 192,
  CALENDAR_NAME_SIZE = 96
};

/* Persisted in inkday-calendar.json. Until the user picks calendars
   (configured == 0) the primary calendar is shown. */
typedef struct
{
  int days;
  int configured;
  int count;
  char ids[CALENDAR_MAX][CALENDAR_ID_SIZE];
} CalendarSettings;

typedef struct
{
  char id[CALENDAR_ID_SIZE];
  char name[CALENDAR_NAME_SIZE];
  int primary;
} CalendarInfo;

/* The application owns the fonts and supplies content-area coordinates. */
typedef struct
{
  int x;
  int y;
  int width;
  int height;
  int foreground_color;
  int background_color;
  ifont *header_font;
  ifont *event_font;
  ifont *small_font;
} CalendarView;

typedef enum
{
  CALENDAR_OFF,       /* background: finish a running sync, draw nothing */
  CALENDAR_DASHBOARD, /* draw and refresh the dashboard block */
  CALENDAR_SETTINGS   /* sync allowed, changes reported to the listener */
} CalendarMode;

typedef enum
{
  CALENDAR_UNAVAILABLE, /* built without Google client credentials */
  CALENDAR_DISCONNECTED,
  CALENDAR_SIGNING_IN, /* waiting for the browser */
  CALENDAR_CONNECTING, /* exchanging the authorization code */
  CALENDAR_SYNCING,
  CALENDAR_CONNECTED
} CalendarStatus;

/* Load settings, tokens and cached events. */
void calendar_init(void);
void calendar_set_view(const CalendarView *view);
/* Draw without refreshing; the caller decides on the screen update. */
void calendar_draw(void);
void calendar_set_mode(CalendarMode mode);
/* Sync interval in minutes; follows the weather refresh interval. */
void calendar_set_interval(int minutes);
/* Sync shortly after a manual weather refresh. */
void calendar_sync_now(void);
/* Called after state changes while in CALENDAR_SETTINGS mode. */
void calendar_set_listener(void (*changed)(void));
/* Kill a running request and cancel timers before fonts are closed. */
void calendar_shutdown(void);
/* Page switching; return nonzero when consumed. */
int calendar_handle_tap(int x, int y);
int calendar_handle_key(int key);
/* Horizontal swipe that started at (x, y); direction 1: next page, -1: previous. */
int calendar_handle_swipe(int x, int y, int direction);
/* Page switcher "<  n/m  >" with filled arrows at the ends of the box. */
void calendar_draw_pager(int x, int y, int width, int height, int page, int pages,
                         ifont *font, int color);
/* Call once a minute: redraws the block after midnight. */
void calendar_check_day(void);

const CalendarSettings *calendar_get_settings(void);
int calendar_apply_settings(const CalendarSettings *settings);
int calendar_is_selected(const CalendarSettings *settings, const CalendarInfo *calendar);
/* Toggle a calendar in a draft, materializing the default selection first. */
void calendar_toggle(CalendarSettings *settings, const CalendarInfo *calendar);
int calendar_list(const CalendarInfo **list);

CalendarStatus calendar_status(void);
/* Translated status/error text for settings, or NULL. */
const char *calendar_message(void);
int calendar_connect(void);
void calendar_cancel_sign_in(void);
void calendar_disconnect(void);

#endif
