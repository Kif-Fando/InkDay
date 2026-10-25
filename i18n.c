#include "i18n.h"
#include <inkview.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
  /* Firmware language key, or NULL when the firmware has no suitable text. */
  const char *system_key;
  const char *english;
} StringEntry;

/* Keys were checked against /ebrmain/language/{en,ru}.txt on the device.
   Own translations for strings without system keys will be added here later. */
static const StringEntry strings[STR_COUNT] = {
    [STR_SETTINGS] = {"@Settings", "Settings"},
    [STR_SAVE] = {"@Save", "Save"},
    [STR_CANCEL] = {"@Cancel", "Cancel"},
    [STR_WEATHER_CITY] = {NULL, "Weather / City"},
    [STR_CITY] = {NULL, "City"},
    [STR_SEARCH] = {"@Search", "Search"},
    [STR_SEARCHING] = {NULL, "Searching..."},
    [STR_DETECT_BY_IP] = {NULL, "Detect by IP"},
    [STR_DETECTING] = {NULL, "Detecting..."},
    [STR_DETECTING_LOCATION] = {NULL, "Detecting location..."},
    [STR_UPDATE_INTERVAL] = {"@Update_interval", "Update interval"},
    [STR_MINUTES_SHORT] = {"@LegimiMinPoint", "min"},
    [STR_CONNECTING] = {"@Connecting", "Connecting"},
    [STR_NETWORK_ERROR] = {"@NetworkError", "Network error"},
    [STR_TIMEOUT] = {"@TimeOutError", "Request is timed out"},
    [STR_PREVIOUS_CONNECTION_BUSY] = {NULL, "Previous network connection is still finishing"},
    [STR_CANNOT_START_CONNECTION] = {NULL, "Cannot start network connection"},
    [STR_CANNOT_START_LOOKUP] = {NULL, "Cannot start location lookup"},
    [STR_SEARCH_FAILED] = {NULL, "City search failed"},
    [STR_LOCATION_NOT_DETECTED] = {NULL, "Could not detect a city"},
    [STR_NOTHING_FOUND] = {"@Nothing_found", "Nothing found"},
    [STR_ENTER_CITY] = {NULL, "Enter a city name"},
    [STR_CITY_TOO_LONG] = {NULL, "City name is too long"},
    [STR_SELECT_CITY] = {NULL, "Select a city:"},
    [STR_SELECT_IP_LOCATION] = {NULL, "Approximate IP location: tap to select"},
    [STR_SELECTED] = {NULL, "Selected"},
    [STR_IP_LOCATION] = {NULL, "IP location"},
    [STR_PLEASE_WAIT] = {"@Please_wait", "Please wait..."},
    [STR_SEARCH_AND_SELECT_FIRST] = {NULL, "Search and select a city first"},
    [STR_SETTINGS_UNREADABLE] = {NULL, "Cannot read saved settings. Select a location and save again."},
    [STR_SETTINGS_BUSY] = {NULL, "Cannot apply settings. A weather request may still be running. Try again shortly."},
    [STR_SETTINGS_SAVE_FAILED] = {NULL, "Cannot save settings. Previous settings were kept."},
    [STR_SETTINGS_APPLY_FAILED] = {NULL, "Settings were saved but could not be applied. Restart the app."},
};

const char *tr(StringId id)
{
  if ((unsigned)id >= STR_COUNT || strings[id].english == NULL)
  {
    fprintf(stderr, "i18n: unknown string id %d\n", (int)id);
    return "?";
  }

  const StringEntry *entry = &strings[id];
  if (entry->system_key != NULL)
  {
    const char *text = GetLangText(entry->system_key);

    /* A missing key is returned unchanged (with or without '@'). */
    if (text != NULL && text[0] != '\0' && strcmp(text, entry->system_key) != 0 && strcmp(text, entry->system_key + 1) != 0)
    {
      return text;
    }
  }
  return entry->english;
}
