/* Minimal HTTP/1.1 client (see http.h). Portable: uses only net.h and <string.h>. */
#include <string.h>

#include "amidrive.h"
#include "http.h"
#include "tz.h"

#define LINE_SIZE (HTTP_LOCATION_SIZE + 64)

static char line[LINE_SIZE];     /* one header line; the client is single threaded */

/* ---- small helpers ---------------------------------------------------------- */

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* Case-insensitive "does s start with prefix". */
static int starts_with_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        if (lower((unsigned char)*s) != lower((unsigned char)*prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

/* Decimal number; returns 0 when there is no digit. Stops at overflow. */
static int parse_ulong(const char *s, unsigned long *out)
{
    unsigned long v = 0;
    int digits = 0;

    while (*s >= '0' && *s <= '9') {
        unsigned long d = (unsigned long)(*s - '0');
        if (v > (0xFFFFFFFFUL - d) / 10)
            return 0;
        v = v * 10 + d;
        s++;
        digits++;
    }
    *out = v;
    return digits > 0;
}

static int parse_hex(const char *s, unsigned long *out)
{
    unsigned long v = 0;
    int digits = 0;

    for (;; s++) {
        int c = lower((unsigned char)*s), d;
        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else break;
        if (v > 0x0FFFFFFFUL)
            return 0;
        v = (v << 4) | (unsigned long)d;
        digits++;
    }
    *out = v;
    return digits > 0;
}

static char *ultoa10(unsigned long v, char *end)
{
    *--end = '\0';
    do {
        *--end = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    return end;
}

static void copy_str(char *dst, int size, const char *src)
{
    int n = (int)strlen(src);
    if (n >= size)
        n = size - 1;
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
}

/* ---- buffered reading ----------------------------------------------------------- */

static int fill(Http *h)
{
    long n = net_read(h->conn, h->rbuf, HTTP_RBUF_SIZE);
    if (n <= 0)
        return n == 0 ? AD_ERR_NET : (int)n;
    h->rpos = 0;
    h->rlen = (int)n;
    return AD_OK;
}

/* Read one line without CR/LF into line[]. Too long lines are cut (the rest is
 * skipped); *cut tells about it. Returns the length or AD_ERR_*. */
static int get_line(Http *h, int *cut)
{
    int len = 0;

    *cut = 0;
    for (;;) {
        char c;
        if (h->rpos >= h->rlen) {
            int rc = fill(h);
            if (rc < 0)
                return rc;
        }
        c = h->rbuf[h->rpos++];
        if (c == '\n')
            break;
        if (len < LINE_SIZE - 1)
            line[len++] = c;
        else
            *cut = 1;
    }
    if (len > 0 && line[len - 1] == '\r')
        len--;
    line[len] = '\0';
    return len;
}

/* ---- request -------------------------------------------------------------------- */

typedef struct {
    Http *h;
    char  buf[512];
    int   len;
    int   rc;
} Writer;

static void w_flush(Writer *w)
{
    if (w->len > 0 && w->rc == AD_OK)
        w->rc = net_write(w->h->conn, w->buf, w->len);
    w->len = 0;
}

static void w_put(Writer *w, const char *s)
{
    while (*s) {
        if (w->len == (int)sizeof(w->buf))
            w_flush(w);
        w->buf[w->len++] = *s++;
    }
}

void http_init(Http *h, NetConn *conn)
{
    memset(h, 0, sizeof(*h));
    h->conn = conn;
    h->body_done = 1;
    h->retry_after = -1;
}

int http_request(Http *h, const char *method, const char *host, const char *path,
                 const char *extra_headers, const void *body, unsigned long body_len)
{
    Writer w;
    char num[16];

    w.h = h;
    w.len = 0;
    w.rc = AD_OK;

    w_put(&w, method);
    w_put(&w, " ");
    w_put(&w, path);
    w_put(&w, " HTTP/1.1\r\nHost: ");
    w_put(&w, host);
    w_put(&w, "\r\nUser-Agent: " AMIDRIVE_NAME "/" AMIDRIVE_VERSION " (AmigaOS)\r\n");
    if (extra_headers)
        w_put(&w, extra_headers);
    if (body_len > 0 || strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0) {
        w_put(&w, "Content-Length: ");
        w_put(&w, ultoa10(body_len, num + sizeof(num)));
        w_put(&w, "\r\n");
    }
    w_put(&w, "\r\n");
    w_flush(&w);
    if (w.rc == AD_OK && body_len > 0)
        w.rc = net_write(h->conn, body, (long)body_len);

    h->head_only = strcmp(method, "HEAD") == 0;
    return w.rc;
}

/* ---- response ------------------------------------------------------------------- */

static void header(Http *h, int cut, unsigned long *content_length, int *have_length)
{
    const char *v;

    if (starts_with_ci(line, "Content-Length:")) {
        v = skip_spaces(line + 15);
        *have_length = parse_ulong(v, content_length);
    } else if (starts_with_ci(line, "Transfer-Encoding:")) {
        v = skip_spaces(line + 18);
        if (starts_with_ci(v, "chunked"))
            h->chunked = 1;
    } else if (starts_with_ci(line, "Connection:")) {
        v = skip_spaces(line + 11);
        if (starts_with_ci(v, "close"))
            h->keep_alive = 0;
        else if (starts_with_ci(v, "keep-alive"))
            h->keep_alive = 1;
    } else if (starts_with_ci(line, "Location:")) {
        /* a cut URL is useless: leave it empty so the caller fails cleanly */
        if (!cut)
            copy_str(h->location, HTTP_LOCATION_SIZE, skip_spaces(line + 9));
    } else if (starts_with_ci(line, "Retry-After:")) {
        unsigned long secs;
        if (parse_ulong(skip_spaces(line + 12), &secs))
            h->retry_after = (long)secs;   /* HTTP-date form is not used by Graph */
    } else if (starts_with_ci(line, "Date:")) {
        if (tz_parse_http_date(skip_spaces(line + 5), &h->date) != AD_OK)
            h->date = 0;                    /* the time to set the Amiga clock from */
    } else if (starts_with_ci(line, "Content-Type:")) {
        copy_str(h->content_type, (int)sizeof(h->content_type), skip_spaces(line + 13));
    }
}

int http_read_response(Http *h)
{
    for (;;) {
        unsigned long content_length = 0, status;
        int have_length = 0, len, cut;

        h->status = 0;
        h->retry_after = -1;
        h->date = 0;
        h->location[0] = '\0';
        h->content_type[0] = '\0';
        h->chunked = 0;
        h->until_close = 0;
        h->remaining = 0;
        h->body_done = 0;

        /* status line: "HTTP/1.1 200 OK" */
        len = get_line(h, &cut);
        if (len < 0)
            return len;
        if (strncmp(line, "HTTP/1.", 7) != 0 || line[8] != ' ' ||
            !parse_ulong(line + 9, &status) || status < 100 || status > 999)
            return AD_ERR_PROTO;
        h->status = (int)status;
        h->keep_alive = line[7] == '1';

        for (;;) {
            len = get_line(h, &cut);
            if (len < 0)
                return len;
            if (len == 0)
                break;
            header(h, cut, &content_length, &have_length);
        }

        if (h->status >= 100 && h->status < 200)
            continue;                      /* 100 Continue etc.: real response follows */

        if (h->head_only || h->status == 204 || h->status == 304) {
            h->body_done = 1;
        } else if (h->chunked) {
            h->remaining = 0;              /* next read starts with a chunk size line */
        } else if (have_length) {
            h->remaining = content_length;
            h->body_done = content_length == 0;
        } else {
            h->until_close = 1;
            h->keep_alive = 0;
        }
        return AD_OK;
    }
}

/* Start of a chunk: read "size[;ext]". A zero size ends the body (trailers skipped). */
static int next_chunk(Http *h)
{
    int len, cut;
    unsigned long size;

    len = get_line(h, &cut);
    if (len < 0)
        return len;
    if (!parse_hex(line, &size))
        return AD_ERR_PROTO;
    if (size == 0) {
        do {                               /* trailer lines until the empty one */
            len = get_line(h, &cut);
            if (len < 0)
                return len;
        } while (len > 0);
        h->body_done = 1;
    }
    h->remaining = size;
    return AD_OK;
}

long http_read_body(Http *h, void *buf, long len)
{
    long n;

    if (h->body_done || len <= 0)
        return 0;

    if (h->chunked && h->remaining == 0) {
        int rc = next_chunk(h);
        if (rc < 0)
            return rc;
        if (h->body_done)
            return 0;
    }

    if (h->rpos >= h->rlen) {
        if (h->until_close) {
            n = net_read(h->conn, h->rbuf, HTTP_RBUF_SIZE);
            if (n == 0) {
                h->body_done = 1;
                return 0;
            }
            if (n < 0)
                return n;
            h->rpos = 0;
            h->rlen = (int)n;
        } else {
            int rc = fill(h);
            if (rc < 0)
                return rc;
        }
    }

    n = h->rlen - h->rpos;
    if (n > len)
        n = len;
    if (!h->until_close && (unsigned long)n > h->remaining)
        n = (long)h->remaining;
    memcpy(buf, h->rbuf + h->rpos, (size_t)n);
    h->rpos += (int)n;

    if (!h->until_close) {
        h->remaining -= (unsigned long)n;
        if (h->remaining == 0) {
            if (h->chunked) {
                int cut, rc = get_line(h, &cut);   /* CRLF after the chunk data */
                if (rc < 0)
                    return rc;
                if (rc != 0)
                    return AD_ERR_PROTO;
            } else {
                h->body_done = 1;
            }
        }
    }
    return n;
}

int http_skip_body(Http *h)
{
    char tmp[256];
    long n;

    while ((n = http_read_body(h, tmp, (long)sizeof(tmp))) > 0)
        ;
    return n < 0 ? (int)n : AD_OK;
}
