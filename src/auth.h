/* Sign-in to Microsoft: OAuth 2.0 device code flow and token refresh.
 *
 *   auth_device_start()  -> user_code + verification URI to show to the user
 *   auth_device_wait()   -> polls the token endpoint until the user signed in
 *   auth_refresh()       -> new access token (and a new refresh token) when expired
 *
 * Tokens are secrets: never print or log them. Refresh tokens rotate - the new one
 * must be saved after every refresh.
 */
#ifndef AUTH_H
#define AUTH_H

#include <stddef.h>

#include "session.h"

#define AUTH_TOKEN_SIZE 4096
#define AUTH_SCOPE      "Files.ReadWrite offline_access"

typedef struct {
    char device_code[AUTH_TOKEN_SIZE];   /* Microsoft sends ~1 KB and more */
    char user_code[32];
    char uri[128];
    long expires_in;             /* seconds the code is valid */
    long interval;               /* seconds between polls */
} AuthDevice;

typedef struct {
    char access[AUTH_TOKEN_SIZE];
    char refresh[AUTH_TOKEN_SIZE];
    unsigned long expires;       /* access token expiry, UTC seconds since 1978 */
} AuthTokens;

typedef struct {
    const char *client_id;
    const char *tenant;          /* "common", "consumers", "organizations" or a tenant id */
} AuthApp;

/* Each returns AD_OK, AD_ERR_AUTH (sign in again), AD_ERR_HTTP / AD_ERR_NET / ... with
 * a message in err. */
int auth_device_start(Session *login, const AuthApp *app, AuthDevice *d, char *err, int errsize);
int auth_device_wait(Session *login, const AuthApp *app, const AuthDevice *d, AuthTokens *t,
                     char *err, int errsize);
int auth_refresh(Session *login, const AuthApp *app, AuthTokens *t, char *err, int errsize);

/* Access token present and valid for at least 5 more minutes? */
int auth_access_valid(const AuthTokens *t, unsigned long now);

/* Token file text ("refresh=...", "access=...", "expires=..."). */
int auth_format(const AuthTokens *t, char *buf, size_t size);
int auth_parse(AuthTokens *t, const char *text);

/* Token file on disk (written safely, see plat_write_file). Load returns AD_ERR_AUTH
 * when there is no usable token (not signed in). */
int auth_load(AuthTokens *t, const char *path);
int auth_save(const AuthTokens *t, const char *path);

/* Make sure the access token is valid: refresh it when needed and save the new
 * tokens to path at once (the old refresh token stops working). With force set the
 * token is refreshed even if it looks valid (after a 401). */
int auth_ensure(Session *login, const AuthApp *app, AuthTokens *t, const char *path, int force,
                char *err, int errsize);

/* "a b&c" -> "a%20b%26c". Returns the length or AD_ERR_TOOBIG. */
int url_encode(const char *in, char *out, size_t size);

#endif
