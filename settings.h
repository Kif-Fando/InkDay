#ifndef POCKETBOOK_SETTINGS_H
#define POCKETBOOK_SETTINGS_H

#include <inkview.h>
#include "calendar.h"

typedef enum
{
  SETTINGS_NONE,
  SETTINGS_CANCEL,
  SETTINGS_SAVE
} SettingsAction;

typedef struct
{
  char city[128];
  char region[128];
  char country[128];
  char timezone[64];
  double latitude;
  double longitude;
  int location_selected;
  int refresh_minutes;
} WeatherSettings;

/* Start editing copies; the live settings remain unchanged until saved.
   Google connect/disconnect take effect immediately, outside the draft. */
void settings_begin(const WeatherSettings *current, const CalendarSettings *calendar);
const WeatherSettings *settings_get_draft(void);
const CalendarSettings *settings_get_calendar_draft(void);
void settings_set_active(int active);
void settings_end(void);

/* The application owns the font and handles screen transitions. */
/* Font for the title and section headings; NULL falls back to the regular font. */
void settings_set_bold_font(ifont *font);
void settings_draw(ifont *font, int foreground_color, int background_color);
SettingsAction settings_handle_tap(int x, int y);
/* Horizontal swipe that started at (x, y); direction 1: next page, -1: previous. */
int settings_handle_swipe(int x, int y, int direction);

#endif
