#include "amidrive.h"
#include "graph.h"
#include "tz.h"
#include "tests.h"

static Session g, dl;

/* what the item callback saw */
static GraphItem seen[8];
static int nseen, stop_at = -1;

static int collect(void *ud, const GraphItem *it)
{
    (void)ud;
    if (nseen == stop_at)
        return AD_ERR_BREAK;
    if (nseen < 8)
        seen[nseen] = *it;
    nseen++;
    return AD_OK;
}

/* downloaded data */
static char got_data[64];
static long got_len;
static int sink_fail;

static int sink(void *ud, const void *data, long len)
{
    (void)ud;
    if (sink_fail)
        return AD_ERR;
    if (got_len + len < (long)sizeof(got_data)) {
        memcpy(got_data + got_len, data, (size_t)len);
        got_len += len;
        got_data[got_len] = '\0';
    }
    return AD_OK;
}

static const char page1[] =
    "{\"@odata.context\":\"https://graph.microsoft.com/v1.0/$metadata#Collection(driveItem)\","
    "\"@odata.nextLink\":\"https://graph.microsoft.com/v1.0/me/drive/root/delta?token=AAA%3d\","
    "\"value\":["
    "{\"id\":\"F00D!101\",\"name\":\"Amiga\",\"size\":2048,\"eTag\":\"aRjAwRCExMDEuMA\","
    "\"cTag\":\"adDpGMDBEITEwMS42Mzg\",\"lastModifiedDateTime\":\"2026-10-01T10:00:00Z\","
    "\"parentReference\":{\"driveId\":\"f00d\",\"driveType\":\"personal\",\"id\":\"F00D!100\"},"
    "\"folder\":{\"childCount\":3}},"
    "{\"id\":\"F00D!102\",\"name\":\"Gry \\u017c\\u00f3\\u0142te.lha\",\"size\":123456,"
    "\"eTag\":\"e2\",\"cTag\":\"c2\",\"lastModifiedDateTime\":\"2026-10-12T08:00:00Z\","
    "\"fileSystemInfo\":{\"createdDateTime\":\"1994-05-01T12:00:00Z\","
    "\"lastModifiedDateTime\":\"1994-05-02T13:14:15Z\"},"
    "\"parentReference\":{\"id\":\"F00D!101\"},"
    "\"file\":{\"mimeType\":\"application/octet-stream\",\"hashes\":{\"quickXorHash\":\"AAAA\"}}},"
    "{\"id\":\"F00D!103\",\"parentReference\":{\"id\":\"F00D!101\"},\"deleted\":{\"state\":\"deleted\"}},"
    "7,"
    "{\"name\":\"no id\"},"
    "{\"id\":\"F00D!104\",\"name\":\"Big.iso\",\"size\":5000000000,\"parentReference\":{\"id\":\"F00D!101\"},"
    "\"file\":{}},"
    "{\"id\":\"F00D!105\",\"name\":\"Notatki\",\"parentReference\":{\"id\":\"F00D!101\"},"
    "\"package\":{\"type\":\"oneNote\"},\"folder\":null}"
    "]}";

static const char page2[] =
    "{\"@odata.deltaLink\":\"https://graph.microsoft.com/v1.0/me/drive/root/delta?token=BBB\","
    "\"value\":[{\"id\":\"F00D!106\",\"name\":\"a\\/b\",\"parentReference\":{\"id\":\"F00D!101\"},"
    "\"file\":{}}]}";

static const char folder_json[] =
    "{\"id\":\"F00D!101\",\"name\":\"Amiga\",\"parentReference\":{\"id\":\"F00D!100\"},"
    "\"folder\":{\"childCount\":3},\"lastModifiedDateTime\":\"2026-10-01T10:00:00Z\"}";

static void test_links(void)
{
    char link[GRAPH_LINK_SIZE], small[40];

    CHECK_INT(graph_delta_link("", link, sizeof(link)), AD_OK);
    CHECK(strncmp(link, "/v1.0/me/drive/root/delta?$select=id,name,size,", 47) == 0);
    CHECK(strstr(link, "&$top=20") != NULL);
    CHECK_INT(graph_delta_link("/", link, sizeof(link)), AD_OK);
    CHECK(strncmp(link, "/v1.0/me/drive/root/delta?", 26) == 0);
    CHECK_INT(graph_delta_link("/Amiga/Gry i programy/", link, sizeof(link)), AD_OK);
    CHECK(strncmp(link, "/v1.0/me/drive/root:/Amiga/Gry%20i%20programy:/delta?$select=", 61) == 0);
    CHECK_INT(graph_children_link("Pliki/\xC5\xBC\xC3\xB3\xC5\x82w#1", link, sizeof(link)), AD_OK);
    CHECK(strncmp(link, "/v1.0/me/drive/root:/Pliki/%C5%BC%C3%B3%C5%82w%231:/children?$select=", 69) == 0);
    CHECK_INT(graph_delta_link("/Amiga", small, sizeof(small)), AD_ERR_TOOBIG);

    CHECK_INT(graph_retry_wait(7, 0), 7);
    CHECK_INT(graph_retry_wait(0, 3), 1);
    CHECK_INT(graph_retry_wait(86400, 0), 600);
    CHECK_INT(graph_retry_wait(-1, 0), 2);
    CHECK_INT(graph_retry_wait(-1, 2), 8);
    CHECK_INT(graph_retry_wait(-1, 30), 512);
}

static void test_item(void)
{
    const char *streams[2];
    char err[200];
    GraphItem it;

    streams[0] = cat2(http_resp(200, NULL, folder_json),
                      http_resp(404, NULL, "{\"error\":{\"code\":\"itemNotFound\",\"message\":\"Item does not exist\"}}"));
    streams[1] = NULL;
    fake_server(streams, 13);
    sess_init(&g, AD_GRAPH_HOST);
    CHECK_INT(graph_item(&g, "TOK", "/Amiga", &it, err, sizeof(err)), AD_OK);
    CHECK_STR(it.id, "F00D!101");
    CHECK_STR(it.parent_id, "F00D!100");
    CHECK_STR(it.name, "Amiga");
    CHECK_INT(it.flags, GI_FOLDER);
    CHECK(strstr(fake_sent(g.conn), "GET /v1.0/me/drive/root:/Amiga?$select=id,name,") != NULL);
    CHECK_INT(graph_item(&g, "TOK", "/Brak", &it, err, sizeof(err)), AD_ERR_NOTFOUND);
    CHECK_STR(err, "folder not found on OneDrive: Item does not exist");
    CHECK_INT(fake_connects, 1);
    sess_close(&g);
}

static void test_pages(void)
{
    const char *streams[3];
    char err[200], link[GRAPH_LINK_SIZE];
    int done;

    /* page 1 after a 429 (Retry-After: 7), page 2 with the deltaLink */
    streams[0] = cat2(cat2(http_resp(429, "Retry-After: 7\r\n", "{\"error\":{\"code\":\"activityLimitReached\"}}"),
                           http_resp(200, NULL, page1)),
                      http_resp(200, NULL, page2));
    streams[1] = NULL;
    fake_server(streams, 61);
    sess_init(&g, AD_GRAPH_HOST);
    fake_slept = 0;
    nseen = 0;
    CHECK_INT(graph_delta_link("/Amiga", link, sizeof(link)), AD_OK);
    CHECK_INT(graph_page(&g, "TOK", link, sizeof(link), &done, collect, NULL, err, sizeof(err)), AD_OK);
    CHECK_INT(fake_slept, 7);
    CHECK_INT(done, 0);
    CHECK_STR(link, "https://graph.microsoft.com/v1.0/me/drive/root/delta?token=AAA%3d");
    CHECK_INT(nseen, 5);
    CHECK_STR(seen[0].id, "F00D!101");
    CHECK_STR(seen[0].parent_id, "F00D!100");
    CHECK_INT(seen[0].flags, GI_FOLDER);
    CHECK_STR(seen[0].etag, "aRjAwRCExMDEuMA");
    CHECK_STR(seen[0].ctag, "adDpGMDBEITEwMS42Mzg");
    CHECK_STR(seen[1].name, "Gry \xC5\xBC\xC3\xB3\xC5\x82te.lha");
    CHECK(seen[1].size == 123456UL);
    CHECK_INT(seen[1].flags, GI_FILE);
    {   /* the time of the file itself, not of the upload */
        unsigned long t;
        CHECK_INT(tz_parse_iso8601("1994-05-02T13:14:15Z", &t), AD_OK);
        CHECK(seen[1].mtime == t);
    }
    CHECK_STR(seen[2].id, "F00D!103");
    CHECK_STR(seen[2].name, "");
    CHECK_INT(seen[2].flags, GI_DELETED);
    CHECK(seen[2].mtime == 0);
    CHECK_INT(seen[3].flags, GI_FILE | GI_TOOBIG);
    CHECK_INT(seen[4].flags, GI_PACKAGE);

    CHECK_INT(graph_page(&g, "TOK", link, sizeof(link), &done, collect, NULL, err, sizeof(err)), AD_OK);
    CHECK_INT(done, 1);
    CHECK_STR(link, "https://graph.microsoft.com/v1.0/me/drive/root/delta?token=BBB");
    CHECK_INT(nseen, 6);
    CHECK_STR(seen[5].name, "a/b");
    CHECK(strstr(fake_sent(g.conn), "GET /v1.0/me/drive/root:/Amiga:/delta?$select=") != NULL);
    CHECK(strstr(fake_sent(g.conn), "GET /v1.0/me/drive/root/delta?token=AAA%3d HTTP/1.1\r\n") != NULL);
    CHECK_INT(fake_connects, 1);
    sess_close(&g);

    /* expired delta link, rejected token, a stop from the callback: link unchanged */
    streams[0] = cat2(cat2(http_resp(410, NULL, "{\"error\":{\"code\":\"resyncRequired\",\"message\":\"Resync required\"}}"),
                           http_resp(401, NULL, "{\"error\":{\"code\":\"InvalidAuthenticationToken\"}}")),
                      http_resp(200, NULL, page1));
    fake_server(streams, 64);
    sess_init(&g, AD_GRAPH_HOST);
    strcpy(link, "https://graph.microsoft.com/v1.0/me/drive/root/delta?token=OLD");
    CHECK_INT(graph_page(&g, "TOK", link, sizeof(link), &done, collect, NULL, err, sizeof(err)), AD_ERR_GONE);
    CHECK_STR(link, "https://graph.microsoft.com/v1.0/me/drive/root/delta?token=OLD");
    CHECK_INT(graph_page(&g, "TOK", link, sizeof(link), &done, collect, NULL, err, sizeof(err)), AD_ERR_AUTH);
    nseen = 0;
    stop_at = 2;
    CHECK_INT(graph_page(&g, "TOK", link, sizeof(link), &done, collect, NULL, err, sizeof(err)), AD_ERR_BREAK);
    stop_at = -1;
    CHECK_INT(done, 0);
    CHECK_STR(link, "https://graph.microsoft.com/v1.0/me/drive/root/delta?token=OLD");
    sess_close(&g);

    /* busy all the time: gives up after 5 attempts; links to other hosts refused */
    streams[0] = cat2(cat2(http_resp(503, NULL, ""), http_resp(503, NULL, "")),
                      cat2(cat2(http_resp(503, NULL, ""), http_resp(503, NULL, "")),
                           http_resp(503, "Retry-After: 30\r\n", "{\"error\":{\"message\":\"Service unavailable\"}}")));
    fake_server(streams, 64);
    sess_init(&g, AD_GRAPH_HOST);
    fake_slept = 0;
    CHECK_INT(graph_page(&g, "TOK", link, sizeof(link), &done, collect, NULL, err, sizeof(err)), AD_ERR_HTTP);
    CHECK_INT(fake_slept, 2 + 4 + 8 + 16);
    CHECK_STR(err, "OneDrive busy, try again later: Service unavailable");
    strcpy(link, "https://evil.example.com/v1.0/me/drive/root/delta");
    CHECK_INT(graph_page(&g, "TOK", link, sizeof(link), &done, collect, NULL, err, sizeof(err)), AD_ERR_PROTO);
    sess_close(&g);
}

static void test_download(void)
{
    const char *streams[4];
    char err[200];
    unsigned long got;

    /* redirect to the download server; the second file reuses both connections */
    streams[0] = cat2(cat2(http_resp(302, "Location: https://public.dm.files.1drv.com/y4mAbC?x=1&y=2\r\n", ""),
                           http_resp(302, "Location: https://public.dm.files.1drv.com:443/y4mDeF\r\n", "")),
                      http_resp(404, NULL, "{\"error\":{\"code\":\"itemNotFound\"}}"));
    streams[1] = cat2("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                      "5\r\nHello\r\n7\r\n, Amiga\r\n0\r\n\r\n",
                      "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nxyz");
    streams[2] = NULL;
    fake_server(streams, 9);
    sess_init(&g, AD_GRAPH_HOST);
    sess_init(&dl, NULL);
    got_len = 0;
    CHECK_INT(graph_download(&g, &dl, "TOK", "F00D!102", sink, NULL, &got, err, sizeof(err)), AD_OK);
    CHECK_STR(got_data, "Hello, Amiga");
    CHECK(got == 12UL);
    CHECK_STR(fake_host, "public.dm.files.1drv.com");
    CHECK(strstr(fake_sent(g.conn), "GET /v1.0/me/drive/items/F00D%21102/content HTTP/1.1\r\n") != NULL);
    CHECK(strstr(fake_sent(dl.conn), "GET /y4mAbC?x=1&y=2 HTTP/1.1\r\nHost: public.dm.files.1drv.com\r\n") != NULL);
    CHECK(strstr(fake_sent(dl.conn), "Authorization") == NULL);     /* the token stays at Graph */

    got_len = 0;
    CHECK_INT(graph_download(&g, &dl, "TOK", "F00D!107", sink, NULL, &got, err, sizeof(err)), AD_OK);
    CHECK_STR(got_data, "xyz");
    CHECK(strstr(fake_sent(dl.conn), "GET /y4mDeF HTTP/1.1\r\n") != NULL);
    CHECK_INT(fake_connects, 2);
    CHECK_INT(graph_download(&g, &dl, "TOK", "F00D!999", sink, NULL, &got, err, sizeof(err)), AD_ERR_NOTFOUND);
    CHECK_STR(err, "file not found on OneDrive");
    sess_close(&g);
    sess_close(&dl);

    /* data straight from Graph; then a plain-http redirect is refused */
    streams[0] = cat2("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nabcd",
                      http_resp(302, "Location: http://insecure.example.com/f\r\n", ""));
    streams[1] = NULL;
    fake_server(streams, 64);
    sess_init(&g, AD_GRAPH_HOST);
    got_len = 0;
    CHECK_INT(graph_download(&g, &dl, "TOK", "A", sink, NULL, &got, err, sizeof(err)), AD_OK);
    CHECK_STR(got_data, "abcd");
    CHECK_INT(graph_download(&g, &dl, "TOK", "B", sink, NULL, &got, err, sizeof(err)), AD_ERR_PROTO);
    CHECK_INT(fake_connects, 1);
    sess_close(&g);

    /* Ctrl-C in the middle, a failing sink, a connection lost mid-body */
    streams[0] = "HTTP/1.1 200 OK\r\nContent-Length: 30\r\n\r\n0123456789012345678901234567890";
    streams[1] = "HTTP/1.1 200 OK\r\nContent-Length: 30\r\n\r\n0123456789";
    streams[2] = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789";
    streams[3] = NULL;
    fake_server(streams, 8);
    sess_init(&g, AD_GRAPH_HOST);
    got_len = 0;
    fake_break_in = 3;
    CHECK_INT(graph_download(&g, &dl, "TOK", "A", sink, NULL, &got, err, sizeof(err)), AD_ERR_BREAK);
    CHECK(got > 0 && got < 30UL);       /* stopped in the middle */
    CHECK(g.conn == NULL);
    fake_break_in = 0;
    CHECK_INT(graph_download(&g, &dl, "TOK", "A", sink, NULL, &got, err, sizeof(err)), AD_ERR_NET);
    CHECK(got == 10UL);
    sink_fail = 1;
    CHECK_INT(graph_download(&g, &dl, "TOK", "A", sink, NULL, &got, err, sizeof(err)), AD_ERR);
    CHECK_STR(err, "cannot save the downloaded data");
    sink_fail = 0;
    sess_close(&g);
}

void test_graph(void)
{
    test_links();
    test_item();
    test_pages();
    test_download();
}
