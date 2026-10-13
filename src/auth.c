/* OAuth 2.0 device code flow and token refresh against login.microsoftonline.com
 * (see auth.h). Portable: network through session.c, time and waiting through plat.h. */
#include <string.h>

#include "amidrive.h"
#include "auth.h"
#include "config.h"
#include "json.h"
#include "plat.h"

#define FORM_HEADERS "Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\n"
#define MIN_VALID    300         /* refresh when less than 5 minutes are left */
#define MAX_NET_ERRORS 5         /* consecutive network failures while polling */

static char resp[16384];         /* token responses: access + refresh token, ~2-6 KB */
static char form[3 * AUTH_TOKEN_SIZE + 512];
static jsmntok_t tok[64];

/* ---- helpers -------------------------------------------------------------------- */

static void set_err(char *err, int errsize, const char *a, const char *b)
{
    int n = 0;

    if (errsize <= 0)
        return;
    while (*a && n < errsize - 1)
        err[n++] = *a++;
    if (b && *b && n < errsize - 3) {
        err[n++] = ':';
        err[n++] = ' ';
        while (*b && *b != '\r' && *b != '\n' && n < errsize - 1)
            err[n++] = *b++;
    }
    err[n] = '\0';
}

int url_encode(const char *in, char *out, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;

    for (; *in; in++) {
        unsigned char c = (unsigned char)*in;
        int plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '_' || c == '.' || c == '~';
        if (n + (plain ? 1 : 3) >= size)
            return AD_ERR_TOOBIG;
        if (plain) {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = '\0';
    return (int)n;
}

/* Append "&name=<encoded value>" (no '&' at the start). */
static int form_add(size_t *len, const char *name, const char *value)
{
    size_t k = strlen(name);
    int v;

    if (*len + k + 3 >= sizeof(form))
        return AD_ERR_TOOBIG;
    if (*len)
        form[(*len)++] = '&';
    memcpy(form + *len, name, k);
    *len += k;
    form[(*len)++] = '=';
    v = url_encode(value, form + *len, sizeof(form) - *len);
    if (v < 0)
        return v;
    *len += (size_t)v;
    return AD_OK;
}

static int tenant_ok(const char *t)
{
    if (!*t)
        return 0;
    for (; *t; t++)
        if (!((*t >= 'a' && *t <= 'z') || (*t >= 'A' && *t <= 'Z') ||
              (*t >= '0' && *t <= '9') || *t == '-' || *t == '.'))
            return 0;
    return 1;
}

/* POST the form to /<tenant>/oauth2/v2.0/<endpoint>; the JSON answer is parsed into j.
 * Returns the HTTP status (> 0) or AD_ERR_*. */
static int post(Session *s, const AuthApp *app, const char *endpoint, size_t len, Json *j,
                char *err, int errsize)
{
    char path[128];
    long n;
    int rc;

    if (!app->client_id || !*app->client_id) {
        set_err(err, errsize, "no application ID: set CLIENTID (see README)", NULL);
        return AD_ERR_AUTH;
    }
    if (!tenant_ok(app->tenant) || strlen(app->tenant) > 60) {
        set_err(err, errsize, "invalid TENANT", app->tenant);
        return AD_ERR_PROTO;
    }
    strcpy(path, "/");
    strcat(path, app->tenant);
    strcat(path, "/oauth2/v2.0/");
    strcat(path, endpoint);

    if ((rc = sess_request(s, "POST", path, FORM_HEADERS, form, (unsigned long)len)) != AD_OK) {
        set_err(err, errsize, "cannot reach " AD_LOGIN_HOST, s->err);
        return rc;
    }
    if ((n = sess_read_all(s, resp, (long)sizeof(resp))) < 0) {
        set_err(err, errsize, "cannot read the answer of " AD_LOGIN_HOST,
                n == AD_ERR_TOOBIG ? "too long" : s->err);
        return (int)n;
    }
    if (json_parse(j, resp, (size_t)n, tok, sizeof(tok) / sizeof(tok[0])) != AD_OK ||
        j->tok[0].type != JSMN_OBJECT) {
        if (s->http.status == 429 || s->http.status == 503)
            return s->http.status;            /* busy pages are not always JSON */
        set_err(err, errsize, "unexpected answer from " AD_LOGIN_HOST, NULL);
        return AD_ERR_PROTO;
    }
    return s->http.status;
}

/* "error" of a failed OAuth answer, "" when there is none. */
static const char *oauth_error(const Json *j)
{
    static char e[48];

    if (json_str(j, json_get(j, 0, "error"), e, sizeof(e)) < 0)
        e[0] = '\0';
    return e;
}

static void error_text(const Json *j, const char *what, char *err, int errsize)
{
    static char desc[200];
    const char *code = oauth_error(j);

    /* error_description is long ("AADSTS700016: Application with identifier ... was not
     * found ..."): its first line is enough */
    int rc = json_str(j, json_get(j, 0, "error_description"), desc, sizeof(desc));

    char *trace;

    if (rc < 0 && rc != AD_ERR_TOOBIG)  /* too long: the cut text is fine */
        desc[0] = '\0';
    if ((trace = strstr(desc, " Trace ID:")) != NULL)
        *trace = '\0';                  /* "... Trace ID: ... Correlation ID: ... Timestamp: ..." */
    set_err(err, errsize, what, *desc ? desc : *code ? code : "unknown error");
}

static long get_long(const Json *j, const char *key, long dflt)
{
    unsigned long v;

    if (json_ulong(j, json_get(j, 0, key), &v) != AD_OK || v > 0x7FFFFFFFUL)
        return dflt;
    return (long)v;
}

/* Successful token answer -> t. Returns AD_OK or AD_ERR_PROTO. */
static int take_tokens(const Json *j, AuthTokens *t, int need_refresh, char *err, int errsize)
{
    static char refresh[AUTH_TOKEN_SIZE];
    long expires = get_long(j, "expires_in", 3600);

    if (json_str(j, json_get(j, 0, "access_token"), t->access, sizeof(t->access)) <= 0) {
        set_err(err, errsize, "no access token in the answer", NULL);
        return AD_ERR_PROTO;
    }
    if (json_str(j, json_get(j, 0, "refresh_token"), refresh, sizeof(refresh)) > 0) {
        strcpy(t->refresh, refresh);    /* refresh tokens rotate: keep the new one */
    } else if (need_refresh) {
        set_err(err, errsize, "no refresh token in the answer (offline_access missing?)", NULL);
        return AD_ERR_PROTO;
    }                                   /* else: refresh answer without one, keep the old */
    t->expires = plat_now() + (unsigned long)expires;
    return AD_OK;
}

/* ---- API ------------------------------------------------------------------------ */

int auth_device_start(Session *login, const AuthApp *app, AuthDevice *d, char *err, int errsize)
{
    size_t len = 0;
    Json j;
    int status;

    memset(d, 0, sizeof(*d));
    if (form_add(&len, "client_id", app->client_id ? app->client_id : "") ||
        form_add(&len, "scope", AUTH_SCOPE))
        return AD_ERR_TOOBIG;

    status = post(login, app, "devicecode", len, &j, err, errsize);
    if (status < 0)
        return status;
    if (status != 200) {
        if (status == 429 || status == 503)
            set_err(err, errsize, "sign-in service busy, try again later", NULL);
        else
            error_text(&j, "sign-in refused", err, errsize);
        return AD_ERR_HTTP;
    }
    if (json_str(&j, json_get(&j, 0, "device_code"), d->device_code, sizeof(d->device_code)) <= 0 ||
        json_str(&j, json_get(&j, 0, "user_code"), d->user_code, sizeof(d->user_code)) <= 0 ||
        json_str(&j, json_get(&j, 0, "verification_uri"), d->uri, sizeof(d->uri)) <= 0) {
        set_err(err, errsize, "incomplete device code answer", NULL);
        return AD_ERR_PROTO;
    }
    d->expires_in = get_long(&j, "expires_in", 900);
    d->interval = get_long(&j, "interval", 5);
    if (d->interval < 1)
        d->interval = 5;
    return AD_OK;
}

int auth_device_wait(Session *login, const AuthApp *app, const AuthDevice *d, AuthTokens *t,
                     char *err, int errsize)
{
    unsigned long deadline = plat_now() + (unsigned long)d->expires_in;
    long interval = d->interval;
    int net_errors = 0;

    for (;;) {
        size_t len = 0;
        const char *e;
        Json j;
        int status, rc;

        if ((rc = plat_sleep(interval)) != AD_OK)
            return rc;
        if (plat_now() >= deadline) {
            set_err(err, errsize, "the code has expired, run LOGIN again", NULL);
            return AD_ERR_AUTH;
        }

        if (form_add(&len, "grant_type", "urn:ietf:params:oauth:grant-type:device_code") ||
            form_add(&len, "client_id", app->client_id ? app->client_id : "") ||
            form_add(&len, "device_code", d->device_code))
            return AD_ERR_TOOBIG;

        status = post(login, app, "token", len, &j, err, errsize);
        if (status == AD_ERR_NET || status == AD_ERR_TLS) {
            if (++net_errors >= MAX_NET_ERRORS)
                return status;
            continue;                         /* keep waiting, the session reconnects */
        }
        if (status < 0)
            return status;
        net_errors = 0;

        if (status == 200)
            return take_tokens(&j, t, 1, err, errsize);
        if (status == 429 || status == 503) {
            if (login->http.retry_after > interval)
                interval = login->http.retry_after;
            continue;
        }
        e = oauth_error(&j);
        if (strcmp(e, "authorization_pending") == 0)
            continue;
        if (strcmp(e, "slow_down") == 0) {
            interval += 5;
            continue;
        }
        if (strcmp(e, "authorization_declined") == 0) {
            set_err(err, errsize, "sign-in was declined", NULL);
            return AD_ERR_AUTH;
        }
        if (strcmp(e, "expired_token") == 0) {
            set_err(err, errsize, "the code has expired, run LOGIN again", NULL);
            return AD_ERR_AUTH;
        }
        error_text(&j, "sign-in failed", err, errsize);
        return AD_ERR_AUTH;
    }
}

int auth_refresh(Session *login, const AuthApp *app, AuthTokens *t, char *err, int errsize)
{
    size_t len = 0;
    Json j;
    int status;

    if (!t->refresh[0]) {
        set_err(err, errsize, "not signed in: run AmiDrive LOGIN", NULL);
        return AD_ERR_AUTH;
    }
    if (form_add(&len, "grant_type", "refresh_token") ||
        form_add(&len, "client_id", app->client_id ? app->client_id : "") ||
        form_add(&len, "refresh_token", t->refresh) ||
        form_add(&len, "scope", AUTH_SCOPE))
        return AD_ERR_TOOBIG;

    status = post(login, app, "token", len, &j, err, errsize);
    if (status < 0)
        return status;
    if (status == 200)
        return take_tokens(&j, t, 0, err, errsize);
    if (status == 429 || status == 503 || status >= 500) {
        set_err(err, errsize, "sign-in service busy, try again later", NULL);
        return AD_ERR_HTTP;
    }
    if (strcmp(oauth_error(&j), "invalid_grant") == 0) {
        set_err(err, errsize, "sign-in expired or revoked: run AmiDrive LOGIN", NULL);
        return AD_ERR_AUTH;
    }
    error_text(&j, "token refresh failed", err, errsize);
    return AD_ERR_AUTH;
}

int auth_access_valid(const AuthTokens *t, unsigned long now)
{
    return t->access[0] && t->expires > now + MIN_VALID;
}

int auth_format(const AuthTokens *t, char *buf, size_t size)
{
    size_t a = strlen(t->access), r = strlen(t->refresh), n;
    char num[12];
    unsigned long v = t->expires;
    int i = (int)sizeof(num) - 1;

    num[i] = '\0';
    do {
        num[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);

    n = 8 + r + 1 + 7 + a + 1 + 8 + strlen(num + i) + 1;
    if (n >= size)
        return AD_ERR_TOOBIG;
    strcpy(buf, "refresh=");
    strcat(buf, t->refresh);
    strcat(buf, "\naccess=");
    strcat(buf, t->access);
    strcat(buf, "\nexpires=");
    strcat(buf, num + i);
    strcat(buf, "\n");
    return (int)strlen(buf);
}

static char filebuf[2 * AUTH_TOKEN_SIZE + 64];

int auth_load(AuthTokens *t, const char *path)
{
    memset(t, 0, sizeof(*t));
    int rc = AD_ERR_AUTH;

    if (plat_read_file(path, filebuf, (long)sizeof(filebuf)) >= 0)
        rc = auth_parse(t, filebuf);
    memset(filebuf, 0, sizeof(filebuf));
    return rc;
}

int auth_save(const AuthTokens *t, const char *path)
{
    int n = auth_format(t, filebuf, sizeof(filebuf));
    int rc = n < 0 ? n : plat_write_file(path, filebuf, n);

    memset(filebuf, 0, sizeof(filebuf));       /* do not leave tokens in memory */
    return rc;
}

int auth_ensure(Session *login, const AuthApp *app, AuthTokens *t, const char *path, int force,
                char *err, int errsize)
{
    int rc;

    if (!force && auth_access_valid(t, plat_now()))
        return AD_OK;
    if ((rc = auth_refresh(login, app, t, err, errsize)) != AD_OK)
        return rc;
    if (auth_save(t, path) != AD_OK) {
        set_err(err, errsize, "cannot save the token file", path);
        return AD_ERR;
    }
    return AD_OK;
}

int auth_parse(AuthTokens *t, const char *text)
{
    char num[16];
    unsigned long v = 0;
    const char *p;

    memset(t, 0, sizeof(*t));
    if (kv_get(text, "refresh", t->refresh, sizeof(t->refresh)) <= 0)
        return AD_ERR_AUTH;
    if (kv_get(text, "access", t->access, sizeof(t->access)) < 0)
        t->access[0] = '\0';
    if (kv_get(text, "expires", num, sizeof(num)) > 0)
        for (p = num; *p >= '0' && *p <= '9'; p++)
            v = v * 10 + (unsigned long)(*p - '0');
    t->expires = v;
    return AD_OK;
}
