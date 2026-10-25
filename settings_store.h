#ifndef POCKETBOOK_SETTINGS_STORE_H
#define POCKETBOOK_SETTINGS_STORE_H

#include "settings.h"

/* Load returns 1 on success, 0 for first run, -1 for a logged error.
   The destination is unchanged unless the entire file is valid. */
int settings_store_load(WeatherSettings *settings);
int settings_store_save(const WeatherSettings *settings);

#endif
