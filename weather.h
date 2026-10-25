#ifndef POCKETBOOK_WEATHER_H
#define POCKETBOOK_WEATHER_H

#include <inkview.h>

enum
{
  WEATHER_HEIGHT = 180
};

/* The application owns these fonts and supplies content-area coordinates. */
typedef struct
{
  int x;
  int y;
  int width;
  int foreground_color;
  int background_color;
  ifont *temperature_font;
  ifont *detail_font;
  ifont *footer_font;
  const char *city;
  const char *region;
} WeatherView;

/* Configure before drawing or activation, and again after theme/layout changes. */
void weather_set_view(const WeatherView *settings);
/* Draw without refreshing; full-screen refresh remains the caller's decision. */
void weather_draw(void);
/* Background mode finishes an existing request but starts no new ones. */
void weather_set_active(int active);
/* Cancel timers and release the child/file before the application closes fonts. */
void weather_shutdown(void);
/* Return nonzero when the manual refresh button consumes the tap. */
int weather_handle_tap(int x, int y);
/* Apply a resolved location and refresh interval while the widget is inactive. */
int weather_configure(double latitude, double longitude, int refresh_minutes);
/* Configuration is only safe while inactive and with no outstanding request. */
int weather_can_configure(void);

#endif
