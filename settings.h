#ifndef POCKETBOOK_SETTINGS_H
#define POCKETBOOK_SETTINGS_H

#include <inkview.h>

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

/* Start editing a copy; the live settings remain unchanged until saved. */
void settings_begin(const WeatherSettings *current);
const WeatherSettings *settings_get_draft(void);
void settings_set_active(int active);
void settings_end(void);

/* The application owns the font and handles screen transitions. */
void settings_draw(ifont *font, int foreground_color, int background_color);
SettingsAction settings_handle_tap(int x, int y);

#endif
