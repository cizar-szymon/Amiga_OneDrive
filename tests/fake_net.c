/* Fake network for the tests: every net_connect() gets the next scripted server
 * stream, replayed in small pieces (to exercise buffer boundaries); when a stream
 * runs out the "server" has closed the connection. What the client sends is
 * recorded per connection. */
#include <string.h>

#include "amidrive.h"
#include "net.h"
#include "tests.h"

#define MAX_CONNS 16

struct NetConn {
    const char *data;
    long pos, len, chunk;
    int  open;
    char sent[16384];
    long sent_len;
};

static struct NetConn conns[MAX_CONNS];
static const char *scripts[MAX_CONNS];
static int nscripts, nconns;
static long script_chunk = 7;

int fake_connects;
char fake_host[128];             /* host of the last net_connect() */

static struct NetConn *new_conn(const char *data, long chunk)
{
    struct NetConn *c;

    if (nconns >= MAX_CONNS)
        nconns = 0;
    c = &conns[nconns++];
    memset(c, 0, sizeof(*c));
    c->data = data;
    c->len = (long)strlen(data);
    c->chunk = chunk;
    c->open = 1;
    return c;
}

struct NetConn *fake_conn(const char *server_data, long chunk)
{
    return new_conn(server_data, chunk);
}

void fake_server(const char *const *streams, long chunk)
{
    nscripts = 0;
    while (streams && streams[nscripts] && nscripts < MAX_CONNS) {
        scripts[nscripts] = streams[nscripts];
        nscripts++;
    }
    script_chunk = chunk;
    fake_connects = 0;
}

const char *fake_sent(struct NetConn *c)
{
    return c->sent;
}

NetConn *net_connect(const char *host, int port, long timeout_secs, char *err, int errsize)
{
    (void)port; (void)timeout_secs;
    strncpy(fake_host, host, sizeof(fake_host) - 1);
    if (fake_connects >= nscripts) {
        strncpy(err, "connection refused", (size_t)errsize - 1);
        err[errsize - 1] = '\0';
        return NULL;
    }
    return new_conn(scripts[fake_connects++], script_chunk);
}

void net_close(NetConn *c)
{
    c->open = 0;
}

int fake_is_open(struct NetConn *c)
{
    return c->open;
}

long net_read(NetConn *c, void *buf, long len)
{
    long n = c->len - c->pos;

    if (n > len)
        n = len;
    if (n > c->chunk)
        n = c->chunk;
    memcpy(buf, c->data + c->pos, (size_t)n);
    c->pos += n;
    return n;                    /* 0 = peer closed */
}

int net_write(NetConn *c, const void *buf, long len)
{
    if (c->sent_len + len >= (long)sizeof(c->sent))
        return AD_ERR_NET;
    memcpy(c->sent + c->sent_len, buf, (size_t)len);
    c->sent_len += len;
    c->sent[c->sent_len] = '\0';
    return AD_OK;
}
