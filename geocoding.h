#ifndef POCKETBOOK_GEOCODING_H
#define POCKETBOOK_GEOCODING_H

enum { GEOCODING_MAX_RESULTS = 5 };

typedef struct
{
  char city[128];
  char region[128];
  char country[128];
  char timezone[64];
  double latitude;
  double longitude;
} GeoPlace;

/* Results are borrowed; copy any data needed after the callback returns. */
typedef void (*GeoSearchCallback)(const GeoPlace *places, int count,
                                  const char *error);

/* Establish connectivity before starting. Returns zero on a logged error. */
int geocoding_start(const char *city, GeoSearchCallback callback);
/* Explicit user-requested lookup using the connection's public IP. */
int geocoding_detect_ip(GeoSearchCallback callback);
/* Cancel without calling the completion callback. */
void geocoding_cancel(void);

#endif