#include "amidrive.h"
#include "http.h"
#include "tests.h"

static Http h;

/* Read the whole body into out; returns the length or the error code. */
static long body(char *out, long size, long piece)
{
    long total = 0, n;

    while ((n = http_read_body(&h, out + total, piece < size - 1 - total ? piece : size - 1 - total)) > 0)
        total += n;
    out[total] = '\0';
    return n < 0 ? n : total;
}

static void two_responses(const char *data, const char *first, const char *second)
{
    static const long chunks[] = { 1, 2, 3, 7, 64, 100000 };
    unsigned int i;
    char buf[256];

    for (i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++) {
        http_init(&h, fake_conn(data, chunks[i]));
        CHECK_INT(http_read_response(&h), AD_OK);
        CHECK_INT(h.status, 200);
        CHECK_INT(body(buf, sizeof(buf), 5), (long)strlen(first));
        CHECK_STR(buf, first);
        CHECK(h.keep_alive);
        CHECK_INT(http_read_response(&h), AD_OK);
        CHECK_INT(h.status, 201);
        CHECK_INT(body(buf, sizeof(buf), 1000), (long)strlen(second));
        CHECK_STR(buf, second);
    }
}

void test_http(void)
{
    char buf[256];

    /* request formatting */
    http_init(&h, fake_conn("", 10));
    CHECK_INT(http_request(&h, "GET", "graph.microsoft.com", "/v1.0/me/drive",
                           "Authorization: Bearer x\r\n", NULL, 0), AD_OK);
    CHECK_STR(fake_sent(h.conn),
              "GET /v1.0/me/drive HTTP/1.1\r\nHost: graph.microsoft.com\r\n"
              "User-Agent: " AMIDRIVE_NAME "/" AMIDRIVE_VERSION " (AmigaOS)\r\n"
              "Authorization: Bearer x\r\n\r\n");

    http_init(&h, fake_conn("", 10));
    CHECK_INT(http_request(&h, "POST", "login.microsoftonline.com", "/consumers/oauth2/v2.0/token",
                           "Content-Type: application/x-www-form-urlencoded\r\n", "a=1&b=2", 7), AD_OK);
    CHECK(strstr(fake_sent(h.conn), "\r\nContent-Length: 7\r\n\r\na=1&b=2") != NULL);

    http_init(&h, fake_conn("", 10));
    CHECK_INT(http_request(&h, "POST", "h", "/createUploadSession", NULL, NULL, 0), AD_OK);
    CHECK(strstr(fake_sent(h.conn), "Content-Length: 0\r\n\r\n") != NULL);

    /* Content-Length bodies, keep-alive, buffer boundaries */
    two_responses("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 13\r\n\r\n"
                  "{\"a\":\"hello\"}"
                  "HTTP/1.1 201 Created\r\ncontent-length: 2\r\n\r\nok",
                  "{\"a\":\"hello\"}", "ok");

    /* chunked: extensions, upper-case hex, trailers */
    two_responses("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                  "5\r\nhello\r\n1;ext=1\r\n \r\nA\r\n0123456789\r\n0\r\nX-Trailer: 1\r\n\r\n"
                  "HTTP/1.1 201 Created\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
                  "hello 0123456789", "");

    /* headers of interest */
    http_init(&h, fake_conn("HTTP/1.1 302 Found\r\nLocation: https://x.sharepoint.com/download?t=abc\r\n"
                            "Content-Length: 0\r\n\r\n", 3));
    CHECK_INT(http_read_response(&h), AD_OK);
    CHECK_INT(h.status, 302);
    CHECK_STR(h.location, "https://x.sharepoint.com/download?t=abc");
    CHECK_INT(http_read_body(&h, buf, sizeof(buf)), 0);

    http_init(&h, fake_conn("HTTP/1.1 429 Too Many Requests\r\nRetry-After: 30\r\n"
                            "Content-Length: 2\r\n\r\n{}", 100));
    CHECK_INT(http_read_response(&h), AD_OK);
    CHECK_INT(h.status, 429);
    CHECK_INT(h.retry_after, 30);
    CHECK_INT(http_skip_body(&h), AD_OK);

    /* 100 Continue is skipped */
    http_init(&h, fake_conn("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx", 4));
    CHECK_INT(http_read_response(&h), AD_OK);
    CHECK_INT(h.status, 200);
    CHECK_INT(body(buf, sizeof(buf), 10), 1);

    /* no length: body until the connection closes */
    http_init(&h, fake_conn("HTTP/1.0 200 OK\r\n\r\nuntil the end", 4));
    CHECK_INT(http_read_response(&h), AD_OK);
    CHECK(!h.keep_alive);
    CHECK_INT(body(buf, sizeof(buf), 3), 13);
    CHECK_STR(buf, "until the end");

    /* Connection: close; 204 and HEAD have no body */
    http_init(&h, fake_conn("HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n", 50));
    CHECK_INT(http_read_response(&h), AD_OK);
    CHECK(!h.keep_alive);
    CHECK_INT(http_read_body(&h, buf, sizeof(buf)), 0);

    http_init(&h, fake_conn("HTTP/1.1 200 OK\r\nContent-Length: 99\r\n\r\n", 50));
    CHECK_INT(http_request(&h, "HEAD", "h", "/", NULL, NULL, 0), AD_OK);
    CHECK_INT(http_read_response(&h), AD_OK);
    CHECK_INT(http_read_body(&h, buf, sizeof(buf)), 0);

    /* errors */
    http_init(&h, fake_conn("SSH-2.0-OpenSSH\r\n\r\n", 50));
    CHECK_INT(http_read_response(&h), AD_ERR_PROTO);

    http_init(&h, fake_conn("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort", 50));
    CHECK_INT(http_read_response(&h), AD_OK);
    CHECK_INT(body(buf, sizeof(buf), 100), AD_ERR_NET);

    http_init(&h, fake_conn("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n", 50));
    CHECK_INT(http_read_response(&h), AD_OK);
    CHECK_INT(body(buf, sizeof(buf), 100), AD_ERR_PROTO);

    http_init(&h, fake_conn("HTTP/1.1 200 OK\r\nContent-", 50));
    CHECK_INT(http_read_response(&h), AD_ERR_NET);
}
