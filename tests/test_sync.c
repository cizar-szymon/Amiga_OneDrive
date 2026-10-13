#include "amidrive.h"
#include "plat.h"
#include "sync.h"
#include "tests.h"

#define LOCAL  "W:OneDrive"
#define SFILE  "W:OneDrive/.AmiDrive/state"
#define MTIME  "2024-01-02T03:04:05Z"
#define DL(x)  http_resp(302, "Location: https://dl.example.com/" x "\r\n", "")

static Session g, dl;
static State st, st2;
static SyncConfig cfg;
static SyncStats ss;
static Tz tz;
static char gstream[24000], dstream[4000], logbuf[6000], err[200];
static int verbose;

static const char *tok_fn(void *ud, int force, int *rc, char *e, int esize)
{
    (void)ud; (void)force; (void)rc; (void)e; (void)esize;
    return "TOK";
}

static void log_fn(void *ud, const char *m)
{
    (void)ud;
    if (verbose)
        printf("  %s\n", m);
    if (strlen(logbuf) + strlen(m) + 2 < sizeof(logbuf)) {
        strcat(logbuf, m);
        strcat(logbuf, "\n");
    }
}

/* one delta item */
static const char *item(const char *id, const char *parent, const char *name, const char *facet,
                        unsigned long size, const char *ctag)
{
    static char ring[16][400];
    static int n;
    char *b = ring[n++ % 16];

    sprintf(b, "{\"id\":\"%s\",\"name\":\"%s\",\"size\":%lu,\"cTag\":\"%s\",\"eTag\":\"e%s\","
               "\"fileSystemInfo\":{\"lastModifiedDateTime\":\"" MTIME "\"},"
               "\"parentReference\":{\"id\":\"%s\"},%s}", id, name, size, ctag, ctag, parent, facet);
    return b;
}

#define FILE_   "\"file\":{}"
#define FOLDER_ "\"folder\":{}"
#define DELETED "\"deleted\":{}"

/* a delta page: "next" (more pages) or "delta" (the last) link, then the items */
static const char *page(const char *kind, const char *token, const char *const *items)
{
    static char b[5000];
    int i;

    sprintf(b, "{\"@odata.%sLink\":\"https://graph.microsoft.com/v1.0/me/drive/root/delta?token=%s\","
               "\"value\":[", kind, token);
    for (i = 0; items[i]; i++) {
        if (i)
            strcat(b, ",");
        strcat(b, items[i]);
    }
    strcat(b, "]}");
    return http_resp(200, NULL, b);
}

static void start(void)
{
    const char *streams[3];

    streams[0] = gstream;
    streams[1] = dstream;
    streams[2] = NULL;
    fake_server(streams, 97);
    sess_init(&g, AD_GRAPH_HOST);
    sess_init(&dl, NULL);
    logbuf[0] = '\0';
    err[0] = '\0';
}

static void finish(void)
{
    sess_close(&g);
    sess_close(&dl);
    gstream[0] = dstream[0] = '\0';
}

static const char *root_json(const char *id)
{
    static char b[200];
    sprintf(b, "{\"id\":\"%s\",\"name\":\"Amiga\",\"parentReference\":{\"id\":\"TOP\"},\"folder\":{}}", id);
    return http_resp(200, NULL, b);
}

static int file_is(const char *path, const char *text)
{
    long len;
    const char *d = fake_file(path, &len);
    return d && len == (long)strlen(text) && memcmp(d, text, (size_t)len) == 0;
}

static unsigned long date_of(const char *path)
{
    PlatInfo info;
    return plat_file_info(path, &info) == AD_OK ? info.date : 0;
}

void test_sync(void)
{
    const char *items[10];
    char hashed[64], path[128];
    unsigned long lt;
    int changed;

    fake_fs_reset();
    tz_fixed(&tz, 60);
    CHECK_INT(tz_parse_iso8601(MTIME, &lt), AD_OK);
    lt = tz_to_local(&tz, lt);

    cfg.graph = &g;
    cfg.dl = &dl;
    cfg.local = LOCAL;
    cfg.remote = "/Amiga";
    cfg.state_file = SFILE;
    cfg.charset = FS_LATIN1;
    cfg.maxname = FS_MAXNAME_OFS;
    cfg.tz = &tz;
    cfg.token = tok_fn;
    cfg.log = log_fn;
    state_init(&st);

    /* ---- first pass: everything is new ---- */
    CHECK_INT(plat_make_dir(LOCAL), AD_OK);
    CHECK_INT(plat_write_file(LOCAL "/b.txt", "old", 3), AD_OK);          /* differs: kept */
    CHECK_INT(plat_write_file(LOCAL "/same.txt", "same!", 5), AD_OK);     /* equal: taken over */
    CHECK_INT(plat_set_date(LOCAL "/same.txt", lt), AD_OK);

    items[0] = item("ROOT", "TOP", "Amiga", FOLDER_, 0, "r");
    items[1] = item("D1", "ROOT", "Gry", FOLDER_, 0, "d1");
    items[2] = item("F1", "D1", "a.txt", FILE_, 5, "c1");
    items[3] = item("F2", "ROOT", "b.txt", FILE_, 3, "c2");
    items[4] = item("F5", "ROOT", "same.txt", FILE_, 5, "c5");
    items[5] = item("X1", "ROOT", ".AmiDrive", FOLDER_, 0, "x");
    items[6] = NULL;
    strcpy(gstream, root_json("ROOT"));
    strcat(gstream, page("next", "P2", items));
    strcat(gstream, DL("f1"));
    strcat(gstream, DL("f2"));
    items[0] = item("F3", "D2", "c.txt", FILE_, 2, "c3");                  /* before its folder */
    items[1] = item("D2", "D1", "Demo", FOLDER_, 0, "d2");
    items[2] = item("P1", "ROOT", "Notes", "\"package\":{\"type\":\"oneNote\"}", 0, "p");
    items[3] = item("F4", "ROOT", "G\xC4\x99\xC5\x9B.txt", FILE_, 1, "c4");
    items[4] = NULL;
    strcat(gstream, page("delta", "L1", items));
    strcat(gstream, DL("f4"));
    strcat(gstream, DL("f3"));
    strcpy(dstream, http_resp(200, NULL, "hello"));
    strcat(dstream, http_resp(200, NULL, "abc"));
    strcat(dstream, http_resp(200, NULL, "x"));
    strcat(dstream, http_resp(200, NULL, "yz"));
    start();
    CHECK_INT(sync_run(&cfg, &st, &ss, err, sizeof(err)), AD_OK);
    CHECK_STR(err, "");
    CHECK_INT(ss.full, 1);
    CHECK_INT(ss.downloaded, 4);
    CHECK_INT(ss.folders, 2);
    CHECK_INT(ss.kept, 1);
    CHECK_INT(ss.skipped, 2);
    CHECK_INT(ss.failed, 0);
    CHECK(ss.bytes == 11UL);
    CHECK(file_is(LOCAL "/Gry/a.txt", "hello"));
    CHECK(date_of(LOCAL "/Gry/a.txt") == lt);
    CHECK(file_is(LOCAL "/b.txt", "abc"));
    CHECK(file_is(LOCAL "/b-Amiga.txt", "old"));
    CHECK(file_is(LOCAL "/same.txt", "same!"));
    CHECK(file_is(LOCAL "/Gry/Demo/c.txt", "yz"));
    CHECK_INT(fs_local_name(FS_LATIN1, "G\xC4\x99\xC5\x9B.txt", 30, 1, hashed, sizeof(hashed), &changed) > 0, 1);
    sprintf(path, LOCAL "/%s", hashed);
    CHECK(file_is(path, "x"));
    CHECK(fake_file(LOCAL "/" SYNC_PART_NAME, NULL) == NULL);
    CHECK(fake_file(LOCAL "/Gry/" SYNC_PART_NAME, NULL) == NULL);
    CHECK_STR(st.delta, "https://graph.microsoft.com/v1.0/me/drive/root/delta?token=L1");
    CHECK_INT(st.count, 7);
    CHECK(strstr(fake_sent(g.conn), "GET /v1.0/me/drive/root:/Amiga:/delta?$select=") != NULL);
    CHECK(strstr(logbuf, "Skipped Notes: OneNote notebook\n") != NULL);
    CHECK(strstr(logbuf, "Kept (changed on the Amiga) as W:OneDrive/b-Amiga.txt\n") != NULL);
    CHECK(strstr(logbuf, "Downloaded W:OneDrive/Gry/Demo/c.txt: 2 bytes\n") != NULL);
    CHECK_INT(fake_connects, 2);
    finish();

    /* the saved state is the same */
    state_init(&st2);
    CHECK_INT(state_load(&st2, SFILE), AD_OK);
    CHECK_INT(st2.count, 7);
    CHECK_STR(st2.delta, st.delta);
    CHECK_STR(st2.root_id, "ROOT");
    state_free(&st2);

    /* ---- changes: a rename + move, a new version, a deleted folder holding a file
     * changed on the Amiga ---- */
    CHECK_INT(plat_write_file(LOCAL "/Gry/Demo/c.txt", "zz9", 3), AD_OK);
    items[0] = item("F1", "ROOT", "A1.txt", FILE_, 5, "c1");
    items[1] = item("F2", "ROOT", "b.txt", FILE_, 4, "c2b");
    items[2] = item("D1", "ROOT", "Gry", DELETED, 0, "");
    items[3] = NULL;
    strcpy(gstream, root_json("ROOT"));
    strcat(gstream, page("delta", "L2", items));
    strcat(gstream, DL("f2b"));
    strcpy(dstream, http_resp(200, NULL, "abcd"));
    start();
    CHECK_INT(sync_run(&cfg, &st, &ss, err, sizeof(err)), AD_OK);
    CHECK_INT(ss.full, 0);
    CHECK_INT(ss.moved, 1);
    CHECK_INT(ss.downloaded, 1);
    CHECK_INT(ss.kept, 3);                     /* c.txt, Demo, Gry */
    CHECK_INT(ss.deleted, 0);
    CHECK_INT(ss.failed, 0);
    CHECK(file_is(LOCAL "/A1.txt", "hello"));
    CHECK(fake_file(LOCAL "/Gry/a.txt", NULL) == NULL);
    CHECK(file_is(LOCAL "/b.txt", "abcd"));
    CHECK(file_is(LOCAL "/b-Amiga.txt", "old"));
    CHECK(file_is(LOCAL "/Gry/Demo/c.txt", "zz9"));
    CHECK(state_find(&st, "D1") == NULL && state_find(&st, "D2") == NULL && state_find(&st, "F3") == NULL);
    CHECK_INT(st.count, 4);
    CHECK(strstr(fake_sent(g.conn), "GET /v1.0/me/drive/root/delta?token=L1 HTTP/1.1") != NULL);
    CHECK(strstr(logbuf, "Kept W:OneDrive/Gry/Demo/c.txt: changed on the Amiga\n") != NULL);
    finish();

    /* ---- the delta link expired: the whole folder again; what is missing goes ---- */
    items[0] = item("ROOT", "TOP", "Amiga", FOLDER_, 0, "r");
    items[1] = item("F2", "ROOT", "b.txt", FILE_, 4, "c2b");
    items[2] = item("F5", "ROOT", "same.txt", FILE_, 5, "c5");
    items[3] = NULL;
    strcpy(gstream, root_json("ROOT"));
    strcat(gstream, http_resp(410, NULL, "{\"error\":{\"code\":\"resyncRequired\"}}"));
    strcat(gstream, page("delta", "L3", items));
    start();
    CHECK_INT(sync_run(&cfg, &st, &ss, err, sizeof(err)), AD_OK);
    CHECK_INT(ss.full, 1);
    CHECK_INT(ss.downloaded, 0);
    CHECK_INT(ss.deleted, 2);
    CHECK(fake_file(LOCAL "/A1.txt", NULL) == NULL);
    CHECK(fake_file(path, NULL) == NULL);
    CHECK(file_is(LOCAL "/b.txt", "abcd"));
    CHECK_INT(st.count, 2);
    CHECK_STR(st.delta, "https://graph.microsoft.com/v1.0/me/drive/root/delta?token=L3");
    CHECK(strstr(logbuf, "the list of changes expired") != NULL);
    finish();

    /* ---- one file fails (gone at the download server): the delta link stays ---- */
    items[0] = item("F6", "ROOT", "new.txt", FILE_, 3, "c6");
    items[1] = item("F7", "ROOT", "ok.txt", FILE_, 2, "c7");
    items[2] = NULL;
    strcpy(gstream, root_json("ROOT"));
    strcat(gstream, page("delta", "L4", items));
    strcat(gstream, DL("f6"));
    strcat(gstream, DL("f7"));
    strcpy(dstream, http_resp(404, NULL, ""));
    strcat(dstream, http_resp(200, NULL, "ok"));
    start();
    CHECK_INT(sync_run(&cfg, &st, &ss, err, sizeof(err)), AD_OK);
    CHECK_INT(ss.failed, 1);
    CHECK_INT(ss.downloaded, 1);
    CHECK(fake_file(LOCAL "/new.txt", NULL) == NULL);
    CHECK(file_is(LOCAL "/ok.txt", "ok"));
    CHECK(fake_file(LOCAL "/" SYNC_PART_NAME, NULL) == NULL);
    CHECK_STR(st.delta, "https://graph.microsoft.com/v1.0/me/drive/root/delta?token=L3");
    CHECK(strstr(logbuf, "Failed W:OneDrive/new.txt: file not found on OneDrive\n") != NULL);
    finish();

    /* ---- the same changes again: ok.txt is not downloaded twice; then the connection
     * breaks before the next download: the pass stops ---- */
    strcpy(gstream, root_json("ROOT"));
    strcat(gstream, page("delta", "L4", items));
    strcat(gstream, DL("f6"));            /* and then nothing: closed */
    strcpy(dstream, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nne");
    start();
    CHECK_INT(sync_run(&cfg, &st, &ss, err, sizeof(err)), AD_ERR_NET);
    CHECK(strstr(err, "connection lost") != NULL || strstr(err, "download") != NULL);
    CHECK(fake_file(LOCAL "/new.txt", NULL) == NULL);
    CHECK(fake_file(LOCAL "/" SYNC_PART_NAME, NULL) == NULL);
    CHECK_STR(st.delta, "https://graph.microsoft.com/v1.0/me/drive/root/delta?token=L3");
    finish();

    strcpy(gstream, root_json("ROOT"));
    strcat(gstream, page("delta", "L4", items));
    strcat(gstream, DL("f6"));
    strcpy(dstream, http_resp(200, NULL, "new"));
    start();
    CHECK_INT(sync_run(&cfg, &st, &ss, err, sizeof(err)), AD_OK);
    CHECK_INT(ss.downloaded, 1);
    CHECK_INT(ss.failed, 0);
    CHECK(file_is(LOCAL "/new.txt", "new"));
    CHECK_STR(st.delta, "https://graph.microsoft.com/v1.0/me/drive/root/delta?token=L4");
    finish();

    /* ---- Ctrl-C, no such folder, another folder ---- */
    strcpy(gstream, root_json("ROOT"));
    start();
    fake_break_in = 1;
    CHECK_INT(sync_run(&cfg, &st, &ss, err, sizeof(err)), AD_ERR_BREAK);
    CHECK_STR(err, "interrupted");
    fake_break_in = 0;
    finish();

    strcpy(gstream, http_resp(404, NULL, "{\"error\":{\"code\":\"itemNotFound\"}}"));
    start();
    CHECK_INT(sync_run(&cfg, &st, &ss, err, sizeof(err)), AD_ERR_NOTFOUND);
    CHECK_STR(err, "OneDrive folder /Amiga not found");
    CHECK_INT(st.count, 4);
    finish();

    items[0] = item("Q1", "ROOT2", "q.txt", FILE_, 1, "q");
    items[1] = NULL;
    strcpy(gstream, root_json("ROOT2"));
    strcat(gstream, page("delta", "M1", items));
    strcat(gstream, DL("q1"));
    strcpy(dstream, http_resp(200, NULL, "q"));
    start();
    CHECK_INT(sync_run(&cfg, &st, &ss, err, sizeof(err)), AD_OK);
    CHECK_INT(ss.full, 1);
    CHECK_INT(st.count, 1);                   /* the old folder's state is gone */
    CHECK_STR(st.root_id, "ROOT2");
    CHECK(file_is(LOCAL "/b.txt", "abcd"));   /* its files are left alone */
    CHECK(file_is(LOCAL "/q.txt", "q"));
    finish();

    state_free(&st);
    fake_fs_reset();
}
