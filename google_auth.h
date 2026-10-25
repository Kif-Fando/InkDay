#ifndef INKDAY_GOOGLE_AUTH_H
#define INKDAY_GOOGLE_AUTH_H

#include <stddef.h>

#define GOOGLE_TOKEN_URL "https://oauth2.googleapis.com/token"

/* Google sign-in for the reader. The device has no working loopback, so the
   OAuth redirect goes to a static relay page (docs/oauth.html) which saves the
   authorization code as a download; the app picks it up from Downloads. */

/* Nonzero when client credentials were compiled in (see Makefile / .env). */
int google_auth_configured(void);
/* Load the stored refresh token and an unfinished sign-in, if any. */
void google_auth_load(void);
int google_auth_connected(void);

/* Create PKCE/state and open the relay page in the firmware browser. */
int google_auth_begin(void);
int google_auth_signing_in(void);
void google_auth_cancel_sign_in(void);
/* 1: authorization code received, 0: still waiting, -1: denied or expired. */
int google_auth_check_downloads(void);
int google_auth_has_code(void);

/* Token endpoint form bodies; contain secrets, never log them. */
int google_auth_exchange_body(char *body, size_t capacity);
int google_auth_refresh_body(char *body, size_t capacity);
/* 1: success, 0: transient failure, -1: grant rejected (sign in again). */
int google_auth_handle_token_response(const char *text, int exchange);

/* Valid access token or NULL when a refresh is needed. */
const char *google_auth_access_token(void);
void google_auth_invalidate_access(void);
/* Forget all tokens and delete the token file. */
void google_auth_disconnect(void);

#endif
