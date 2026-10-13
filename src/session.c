/* Kept-alive HTTPS connection with one retry on a stale connection (see session.h). */
#include <string.h>

#include "amidrive.h"
#include "session.h"

static void set_err(Session *s, const char *msg)
{
    strncpy(s->err, msg, sizeof(s->err) - 1);
    s->err[sizeof(s->err) - 1] = '\0';
}

void sess_init(Session *s, const char *host)
{
    memset(s, 0, sizeof(*s));
    s->host = host;
}

void sess_close(Session *s)
{
    if (s->conn) {
        net_close(s->conn);
        s->conn = NULL;
    }
}

int sess_set_host(Session *s, const char *host, int len)
{
    if (len <= 0 || len >= (int)sizeof(s->host_buf))
        return AD_ERR_TOOBIG;
    if (s->host && (int)strlen(s->host) == len && memcmp(s->host, host, (size_t)len) == 0)
        return AD_OK;
    sess_close(s);
    memcpy(s->host_buf, host, (size_t)len);
    s->host_buf[len] = '\0';
    s->host = s->host_buf;
    return AD_OK;
}

static int connect_now(Session *s)
{
    s->conn = net_connect(s->host, 443, SESS_TIMEOUT, s->err, (int)sizeof(s->err));
    if (!s->conn)
        return AD_ERR_NET;
    http_init(&s->http, s->conn);
    s->fresh = 1;
    return AD_OK;
}

int sess_request(Session *s, const char *method, const char *path,
                 const char *headers, const void *body, unsigned long body_len)
{
    int attempt, rc = AD_ERR_NET;

    for (attempt = 0; attempt < 2; attempt++) {
        int reused;

        if (!s->conn && (rc = connect_now(s)) != AD_OK)
            return rc;
        reused = !s->fresh;
        s->fresh = 0;

        rc = http_request(&s->http, method, s->host, path, headers, body, body_len);
        if (rc == AD_OK)
            rc = http_read_response(&s->http);
        if (rc == AD_OK)
            return AD_OK;

        sess_close(s);
        /* only a reused connection gets a second chance: the server may have
         * closed it while idle. Anything else is a real failure. */
        if (rc != AD_ERR_NET || !reused)
            break;
    }
    if (rc == AD_ERR_PROTO)
        set_err(s, "invalid HTTP response");
    else if (rc == AD_ERR_BREAK)
        set_err(s, "interrupted");
    else
        set_err(s, "connection lost");
    return rc;
}

void sess_done(Session *s)
{
    if (s->conn && (!s->http.keep_alive || !s->http.body_done))
        sess_close(s);
}

long sess_read_all(Session *s, char *buf, long size)
{
    long total = 0, n;
    int rc = AD_OK;

    while ((n = http_read_body(&s->http, buf + total, size - 1 - total)) > 0) {
        total += n;
        if (total >= size - 1) {
            char extra;                 /* exactly full, or really too big? */
            n = http_read_body(&s->http, &extra, 1);
            if (n > 0)
                rc = http_skip_body(&s->http) < 0 ? AD_ERR_NET : AD_ERR_TOOBIG;
            break;
        }
    }
    if (n < 0)
        rc = (int)n;
    buf[total] = '\0';
    if (rc == AD_ERR_NET)
        set_err(s, "connection lost while reading the response");
    sess_done(s);
    return rc != AD_OK ? rc : total;
}
