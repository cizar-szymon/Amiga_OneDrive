#include "amidrive.h"
#include "session.h"
#include "tests.h"

static Session s;

void test_session(void)
{
    char buf[64];
    const char *streams[4];
    struct NetConn *first;

    /* two requests on one kept-alive connection */
    streams[0] = cat2(http_resp(200, NULL, "{\"a\":1}"), http_resp(201, NULL, "{}"));
    streams[1] = NULL;
    fake_server(streams, 5);
    sess_init(&s, "graph.microsoft.com");
    CHECK_INT(sess_request(&s, "GET", "/one", NULL, NULL, 0), AD_OK);
    CHECK_INT(s.http.status, 200);
    CHECK_INT(sess_read_all(&s, buf, sizeof(buf)), 7);
    CHECK_STR(buf, "{\"a\":1}");
    CHECK_INT(sess_request(&s, "GET", "/two", NULL, NULL, 0), AD_OK);
    CHECK_INT(s.http.status, 201);
    CHECK_INT(sess_read_all(&s, buf, sizeof(buf)), 2);
    CHECK_INT(fake_connects, 1);
    CHECK(strstr(fake_sent(s.conn), "GET /one HTTP/1.1\r\nHost: graph.microsoft.com\r\n") != NULL);
    CHECK(strstr(fake_sent(s.conn), "GET /two ") != NULL);
    sess_close(&s);

    /* the server closed the idle connection: the request is sent again on a new one */
    streams[0] = http_resp(200, NULL, "{}");
    streams[1] = http_resp(202, NULL, "{\"b\":2}");
    streams[2] = NULL;
    fake_server(streams, 64);
    sess_init(&s, "h");
    CHECK_INT(sess_request(&s, "GET", "/a", NULL, NULL, 0), AD_OK);
    CHECK_INT(sess_read_all(&s, buf, sizeof(buf)), 2);
    first = s.conn;
    CHECK_INT(sess_request(&s, "POST", "/b", NULL, "x=1", 3), AD_OK);
    CHECK_INT(s.http.status, 202);
    CHECK_INT(fake_connects, 2);
    CHECK(!fake_is_open(first));
    CHECK(strstr(fake_sent(s.conn), "POST /b ") != NULL);
    CHECK_INT(sess_read_all(&s, buf, sizeof(buf)), 7);
    sess_close(&s);

    /* a fresh connection is not retried */
    streams[0] = "garbage\r\n\r\n";
    streams[1] = http_resp(200, NULL, "{}");
    streams[2] = NULL;
    fake_server(streams, 64);
    sess_init(&s, "h");
    CHECK_INT(sess_request(&s, "GET", "/", NULL, NULL, 0), AD_ERR_PROTO);
    CHECK_INT(fake_connects, 1);
    CHECK(s.conn == NULL);

    /* no server */
    fake_server(NULL, 64);
    sess_init(&s, "h");
    CHECK_INT(sess_request(&s, "GET", "/", NULL, NULL, 0), AD_ERR_NET);
    CHECK_STR(s.err, "connection refused");

    /* Connection: close drops the connection after the body */
    streams[0] = http_resp(200, "Connection: close\r\n", "{}");
    streams[1] = http_resp(200, NULL, "{}");
    streams[2] = NULL;
    fake_server(streams, 64);
    sess_init(&s, "h");
    CHECK_INT(sess_request(&s, "GET", "/", NULL, NULL, 0), AD_OK);
    CHECK_INT(sess_read_all(&s, buf, sizeof(buf)), 2);
    CHECK(s.conn == NULL);
    CHECK_INT(sess_request(&s, "GET", "/", NULL, NULL, 0), AD_OK);
    CHECK_INT(fake_connects, 2);
    sess_read_all(&s, buf, sizeof(buf));
    sess_close(&s);

    /* body bigger than the buffer; body exactly filling it */
    streams[0] = cat2(http_resp(200, NULL, "0123456789"), http_resp(200, NULL, "0123456789"));
    streams[1] = NULL;
    fake_server(streams, 3);
    sess_init(&s, "h");
    CHECK_INT(sess_request(&s, "GET", "/", NULL, NULL, 0), AD_OK);
    CHECK_INT(sess_read_all(&s, buf, 6), AD_ERR_TOOBIG);
    CHECK_STR(buf, "01234");
    CHECK(s.conn != NULL);                      /* rest skipped, connection still usable */
    CHECK_INT(sess_request(&s, "GET", "/", NULL, NULL, 0), AD_OK);
    CHECK_INT(sess_read_all(&s, buf, 11), 10);
    CHECK_INT(fake_connects, 1);
    sess_close(&s);
}
