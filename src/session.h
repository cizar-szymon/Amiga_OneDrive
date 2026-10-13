/* One kept-alive HTTPS connection to one host (login or Graph).
 *
 * The TLS handshake costs seconds on a 68030/68060, so the connection is reused.
 * A reused connection may have been closed by the server in the meantime: then the
 * request is sent once more on a fresh connection.
 *
 *   sess_request(s, "GET", path, headers, NULL, 0);   -> s->http.status ...
 *   sess_read_all(s, buf, size)  or  http_read_body(&s->http, ...) + sess_done(s)
 */
#ifndef SESSION_H
#define SESSION_H

#include "http.h"

#define SESS_TIMEOUT 60          /* seconds without data before giving up */

typedef struct {
    const char *host;
    char        host_buf[128];   /* host set with sess_set_host() */
    NetConn    *conn;
    int         fresh;           /* connection not used for a request yet */
    Http        http;
    char        err[200];        /* message of the last failure */
} Session;

void sess_init(Session *s, const char *host);
void sess_close(Session *s);

/* Point the session at another host (copied), e.g. the download server of a
 * redirect. The connection is kept when the host stays the same.
 * Returns AD_OK or AD_ERR_TOOBIG. */
int sess_set_host(Session *s, const char *host, int len);

/* Send a request and read the response headers. Returns AD_OK (any HTTP status),
 * or AD_ERR_NET / AD_ERR_TLS / AD_ERR_PROTO / AD_ERR_BREAK with s->err set. */
int sess_request(Session *s, const char *method, const char *path,
                 const char *headers, const void *body, unsigned long body_len);

/* Read the whole (small) body into buf, zero terminated. Returns the length, or
 * AD_ERR_TOOBIG (the rest is skipped) or another AD_ERR_*. Calls sess_done(). */
long sess_read_all(Session *s, char *buf, long size);

/* After the body was read: drop the connection if the server will not keep it. */
void sess_done(Session *s);

#endif
