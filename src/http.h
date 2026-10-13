/* Minimal HTTP/1.1 client over a NetConn: one request at a time, keep-alive,
 * Content-Length and chunked bodies, streamed reading (the body is never held
 * in memory as a whole).
 *
 *   http_request(h, "GET", host, path, extra_headers, NULL, 0);
 *   http_read_response(h);           -> h->status, h->location, h->retry_after
 *   while ((n = http_read_body(h, buf, sizeof buf)) > 0) ...
 *   if (!h->keep_alive) reconnect before the next request
 */
#ifndef HTTP_H
#define HTTP_H

#include "net.h"

#define HTTP_RBUF_SIZE     4096
#define HTTP_LOCATION_SIZE 2048    /* pre-authenticated download URLs are long */

typedef struct {
    NetConn *conn;

    /* receive buffer */
    char rbuf[HTTP_RBUF_SIZE];
    int  rpos, rlen;

    /* current response */
    int  status;                   /* e.g. 200, 302, 429 */
    int  keep_alive;               /* connection may be reused after the body */
    long retry_after;              /* seconds from Retry-After, -1 = none */
    char location[HTTP_LOCATION_SIZE];
    char content_type[64];

    /* body state */
    int  head_only;                /* response to HEAD (never has a body) */
    int  chunked;
    int  until_close;              /* no length given: body ends with the connection */
    int  body_done;
    unsigned long remaining;       /* bytes left in the body or the current chunk */
} Http;

void http_init(Http *h, NetConn *conn);

/* Send a request. extra_headers is NULL or complete "Name: value\r\n" lines.
 * A body (body_len > 0) is sent with Content-Length. */
int http_request(Http *h, const char *method, const char *host, const char *path,
                 const char *extra_headers, const void *body, unsigned long body_len);

/* Read the status line and headers (1xx responses are skipped). Returns AD_OK. */
int http_read_response(Http *h);

/* Read body bytes. Returns > 0 bytes, 0 at the end of the body, or AD_ERR_*. */
long http_read_body(Http *h, void *buf, long len);

/* Read and discard the rest of the body (needed before reusing the connection). */
int http_skip_body(Http *h);

#endif
