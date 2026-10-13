/* AmiDrive - OneDrive client for AmigaOS: command line, settings, sign-in, self test.
 *
 *   AmiDrive [LOCAL=Work:OneDrive] [REMOTE=/Amiga] [ONCE] [CHARSET=LATIN1|LATIN2]
 *            [INTERVAL=3] [STATE=dir]       synchronise (settings are remembered)
 *   AmiDrive LOGIN          sign in with a device code
 *   AmiDrive LOGOUT         forget the sign-in (deletes the token file)
 *   AmiDrive INFO           settings, sign-in state, OneDrive owner and space
 *   AmiDrive TEST           check clock, time zone, DNS, TLS and HTTP against Graph
 *   AmiDrive TEST HOST=x    TLS + "GET /" to another host (e.g. expired.badssl.com)
 *
 * Settings: Shell arguments > the prefs file (ENVARC:AmiDrive/AmiDrive.prefs).
 */
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <libraries/locale.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "amidrive.h"
#include "auth.h"
#include "config.h"
#include "fsmap.h"
#include "graph.h"
#include "net_amiga.h"
#include "plat_amiga.h"
#include "session.h"
#include "state.h"
#include "sync.h"
#include "tz.h"

size_t __stack = 65536;                 /* OpenSSL needs more than the Shell default */

const char amidrive_version[] = "$VER: " AMIDRIVE_NAME " " AMIDRIVE_VERSION " (" AMIDRIVE_DATE ")";

#define TEMPLATE "LOCAL/K,REMOTE/K,ONCE/S,CHARSET/K,INTERVAL/K,STATE/K,CLIENTID/K,TENANT/K," \
                 "LOGIN/S,LOGOUT/S,INFO/S,TEST/S,HOST/K"
enum { ARG_LOCAL, ARG_REMOTE, ARG_ONCE, ARG_CHARSET, ARG_INTERVAL, ARG_STATE, ARG_CLIENTID,
       ARG_TENANT, ARG_LOGIN, ARG_LOGOUT, ARG_INFO, ARG_TEST, ARG_HOST, ARG_COUNT };

/* arguments that are settings, in template order, with their prefs keys */
static const struct { int arg; const char *key; } setting_args[] = {
    { ARG_LOCAL, "LOCAL" }, { ARG_REMOTE, "REMOTE" }, { ARG_CHARSET, "CHARSET" },
    { ARG_INTERVAL, "INTERVAL" }, { ARG_STATE, "STATE" }, { ARG_CLIENTID, "CLIENTID" },
    { ARG_TENANT, "TENANT" }
};

struct LocaleBase *LocaleBase;
extern struct Library *SocketBase;      /* net_amiga.c */

/* large structures: static, not on the stack */
static Config cfg;
static Session login_sess, graph_sess, dl_sess;
static State state;
static AuthTokens tokens;
static AuthDevice device;
static char text[2048];

static AuthApp app(void)
{
    AuthApp a;
    a.client_id = cfg.client_id;
    a.tenant = cfg.tenant;
    return a;
}

/* UTF-8 text from the cloud -> the Amiga code page, for printing */
static const char *local_text(const char *utf8)
{
    static char out[256];
    int changed;

    fs_from_utf8(cfg.charset[5] == '2' ? FS_LATIN2 : FS_LATIN1, utf8, out, sizeof(out), &changed);
    return out;
}

static void print_rc(const char *what, int rc, const char *err)
{
    if (rc == AD_ERR_BREAK)
        Printf("%s: interrupted\n", (LONG)what);
    else
        Printf("%s: %s\n", (LONG)what, (LONG)(err && *err ? err : "failed"));
}

/* ---- settings ------------------------------------------------------------------- */

static int load_settings(LONG *args, int *changed)
{
    unsigned int i;

    config_defaults(&cfg);
    if (plat_read_file(CFG_PREFS_FILE, text, sizeof(text)) >= 0 &&
        config_parse(&cfg, text) != AD_OK)
        Printf("Warning: invalid values in %s were ignored\n", (LONG)CFG_PREFS_FILE);

    *changed = 0;
    for (i = 0; i < sizeof(setting_args) / sizeof(setting_args[0]); i++) {
        const char *v = (const char *)args[setting_args[i].arg];
        if (!v)
            continue;
        if (config_set(&cfg, setting_args[i].key, v) != AD_OK) {
            Printf("Invalid %s=%s\n", (LONG)setting_args[i].key, (LONG)v);
            return RETURN_ERROR;
        }
        *changed = 1;
    }
    return RETURN_OK;
}

static int save_settings(void)
{
    int n = config_format(&cfg, text, sizeof(text));

    if (n < 0 || plat_make_dir(CFG_DIR) != AD_OK ||
        plat_write_file(CFG_PREFS_FILE, text, n) != AD_OK) {
        Printf("Cannot save the settings to %s\n", (LONG)CFG_PREFS_FILE);
        return RETURN_ERROR;
    }
    Printf("Settings saved to %s\n", (LONG)CFG_PREFS_FILE);
    return RETURN_OK;
}

static int start_network(void)
{
    char err[200];

    if (net_init(err, sizeof(err)) != AD_OK) {
        Printf("Network: %s\n", (LONG)err);
        return RETURN_FAIL;
    }
    return RETURN_OK;
}

/* ---- OneDrive info -------------------------------------------------------------- */

/* Valid token (refreshed if needed) + GET /me/drive, printed. */
static int show_drive(void)
{
    char err[200];
    AuthApp a = app();
    DriveInfo di;
    int rc, attempt;

    for (attempt = 0; attempt < 2; attempt++) {
        rc = auth_ensure(&login_sess, &a, &tokens, CFG_TOKEN_FILE, attempt > 0, err, sizeof(err));
        if (rc != AD_OK) {
            print_rc("Sign-in", rc, err);
            return rc == AD_ERR_AUTH ? RETURN_WARN : RETURN_ERROR;
        }
        rc = graph_drive_info(&graph_sess, tokens.access, &di, err, sizeof(err));
        if (rc != AD_ERR_AUTH)          /* 401 once: refresh and try again */
            break;
    }
    if (rc != AD_OK) {
        print_rc("OneDrive", rc, err);
        return RETURN_ERROR;
    }
    Printf("OneDrive:  %s (%s), ", (LONG)local_text(di.owner), (LONG)di.drive_type);
    Printf("%lu.%lu of %lu.%lu GB used\n",
           di.used_mb / 1000, (di.used_mb % 1000) / 100, di.total_mb / 1000, (di.total_mb % 1000) / 100);
    return RETURN_OK;
}

/* ---- commands ------------------------------------------------------------------- */

static int cmd_login(void)
{
    char err[200];
    AuthApp a = app();
    int rc;

    if ((rc = start_network()) != RETURN_OK)
        return rc;
    Printf("Signing in to Microsoft (%s) ...\n", (LONG)cfg.tenant);
    if ((rc = auth_device_start(&login_sess, &a, &device, err, sizeof(err))) != AD_OK) {
        print_rc("Sign-in", rc, err);
        return RETURN_ERROR;
    }
    Printf("\nOn a phone or PC open   %s\n", (LONG)device.uri);
    Printf("and enter the code      %s\n\n", (LONG)device.user_code);
    Printf("Waiting (up to %ld minutes, Ctrl-C cancels) ...\n", device.expires_in / 60);

    rc = auth_device_wait(&login_sess, &a, &device, &tokens, err, sizeof(err));
    memset(&device, 0, sizeof(device));
    if (rc != AD_OK) {
        print_rc("Sign-in", rc, err);
        return rc == AD_ERR_BREAK ? RETURN_WARN : RETURN_ERROR;
    }
    if (plat_make_dir(CFG_DIR) != AD_OK || auth_save(&tokens, CFG_TOKEN_FILE) != AD_OK) {
        Printf("Signed in, but cannot save %s\n", (LONG)CFG_TOKEN_FILE);
        return RETURN_ERROR;
    }
    Printf("Signed in. Token saved to %s\n", (LONG)CFG_TOKEN_FILE);
    return show_drive();
}

static int cmd_logout(void)
{
    plat_delete_file(CFG_TOKEN_FILE);
    Printf("Signed out (%s deleted). Run AmiDrive LOGIN to sign in again.\n", (LONG)CFG_TOKEN_FILE);
    return RETURN_OK;
}

static int cmd_info(void)
{
    int rc;

    Printf("%s %s\n", (LONG)AMIDRIVE_NAME, (LONG)AMIDRIVE_VERSION);
    Printf("Local:     %s\n", (LONG)(cfg.local[0] ? cfg.local : "(not set, use LOCAL=)"));
    Printf("Remote:    %s\n", (LONG)(cfg.remote[0] ? cfg.remote : "(not set, use REMOTE=)"));
    Printf("Charset:   %s, check every %ld min\n", (LONG)cfg.charset, cfg.interval);
    if (cfg.state[0])
        Printf("State:     %s\n", (LONG)cfg.state);
    else if (cfg.local[0])
        Printf("State:     %s/.AmiDrive\n", (LONG)cfg.local);
    Printf("App ID:    %s (%s)\n", (LONG)(cfg.client_id[0] ? cfg.client_id : "not set, use CLIENTID="),
           (LONG)cfg.tenant);

    if (auth_load(&tokens, CFG_TOKEN_FILE) != AD_OK) {
        Printf("Sign-in:   not signed in, run AmiDrive LOGIN\n");
        return RETURN_WARN;
    }
    if ((rc = start_network()) != RETURN_OK)
        return rc;
    return show_drive();
}

/* One GET: status, framing, the start of the body. */
static int test_get(Session *s, const char *path)
{
    static char body[512];
    long n, total = 0;
    int rc;

    Printf("GET %s\n", (LONG)path);
    if ((rc = sess_request(s, "GET", path, "Accept: application/json\r\n", NULL, 0)) != AD_OK) {
        Printf("  failed: %s\n", (LONG)s->err);
        return rc;
    }
    Printf("  status %ld, %s, keep-alive %s\n", (LONG)s->http.status,
           (LONG)(s->http.chunked ? "chunked" : s->http.until_close ? "until close" : "Content-Length"),
           (LONG)(s->http.keep_alive ? "yes" : "no"));
    while ((n = http_read_body(&s->http, body + total, (long)sizeof(body) - 1 - total)) > 0) {
        total += n;
        if (total >= (long)sizeof(body) - 1)
            break;
    }
    if (n < 0)
        return (int)n;
    body[total] = '\0';
    Printf("  body: %s\n", (LONG)body);
    rc = http_skip_body(&s->http);
    sess_done(s);
    return rc;
}

static long ticks_since(const struct DateStamp *t0)
{
    struct DateStamp t1;

    DateStamp(&t1);
    return (t1.ds_Days - t0->ds_Days) * 24L * 60 * 60 * TICKS_PER_SECOND +
           (t1.ds_Minute - t0->ds_Minute) * 60L * TICKS_PER_SECOND + (t1.ds_Tick - t0->ds_Tick);
}

/* ---- synchronisation ------------------------------------------------------------ */

static const char *sync_token(void *ud, int force, int *rc, char *err, int errsize)
{
    AuthApp a = app();

    (void)ud;
    *rc = auth_ensure(&login_sess, &a, &tokens, CFG_TOKEN_FILE, force, err, errsize);
    return *rc == AD_OK ? tokens.access : NULL;
}

static void sync_log(void *ud, const char *msg)
{
    (void)ud;
    Printf("  %s\n", (LONG)msg);
}

/* path + "/" + name ("/" left out after ':' or '/'); path has room for it */
static void add_name(char *path, const char *name)
{
    size_t n = strlen(path);

    if (n > 0 && path[n - 1] != ':' && path[n - 1] != '/')
        strcat(path, "/");
    strcat(path, name);
}

static void print_time(void)
{
    char iso[24];

    tz_format_iso8601(plat_local_now(), iso);
    iso[10] = ' ';
    iso[16] = '\0';                     /* "YYYY-MM-DD HH:MM" */
    Printf("%s ", (LONG)iso);
}

/* Synchronise now (once) or every INTERVAL minutes until Ctrl-C. */
static int cmd_sync(int once)
{
    static char remote[CFG_PATH_SIZE * 2], state_file[CFG_PATH_SIZE + 16];
    FsCharset cs = cfg.charset[5] == '2' ? FS_LATIN2 : FS_LATIN1;
    SyncConfig sc;
    SyncStats ss;
    char err[200];
    int rc;

    if (auth_load(&tokens, CFG_TOKEN_FILE) != AD_OK) {
        Printf("Not signed in: run %s LOGIN first\n", (LONG)AMIDRIVE_NAME);
        return RETURN_WARN;
    }
    if (fs_to_utf8(cs, cfg.remote, remote, sizeof(remote)) < 0) {
        Printf("REMOTE too long\n");
        return RETURN_ERROR;
    }
    /* STATE=dir, else <LOCAL>/.AmiDrive; the file in it is "state" */
    strcpy(state_file, cfg.state[0] ? cfg.state : cfg.local);
    if (!cfg.state[0])
        add_name(state_file, SYNC_STATE_DIR);
    add_name(state_file, "state");
    if ((rc = start_network()) != RETURN_OK)
        return rc;

    memset(&sc, 0, sizeof(sc));
    sc.graph = &graph_sess;
    sc.dl = &dl_sess;
    sc.local = cfg.local;
    sc.remote = remote;
    sc.state_file = state_file;
    sc.charset = cs;
    sc.tz = plat_timezone();
    sc.token = sync_token;
    sc.log = sync_log;

    state_init(&state);
    if (state_load(&state, state_file) == AD_ERR_PROTO)
        Printf("Warning: %s is damaged, the whole folder will be read again\n", (LONG)state_file);

    for (;;) {
        plat_make_dir(cfg.local);
        sc.maxname = plat_name_limit(cfg.local);
        print_time();
        Printf("Checking OneDrive %s -> %s\n", (LONG)cfg.remote, (LONG)cfg.local);
        rc = sync_run(&sc, &state, &ss, err, sizeof(err));
        /* the connections are not kept for minutes of waiting */
        sess_close(&graph_sess);
        sess_close(&dl_sess);
        sess_close(&login_sess);

        if (rc == AD_OK) {
            if (ss.downloaded + ss.moved + ss.deleted + ss.folders + ss.kept + ss.failed == 0)
                Printf("  no changes\n");
            else
                Printf("  %ld downloaded (%lu KB), %ld moved, %ld deleted, %ld new folders, "
                       "%ld kept, %ld failed\n", ss.downloaded, (ss.bytes + 1023) / 1024, ss.moved,
                       ss.deleted, ss.folders, ss.kept, ss.failed);
        } else {
            print_rc("  Stopped", rc, err);
            if (rc == AD_ERR_BREAK || rc == AD_ERR_AUTH || rc == AD_ERR_NOTFOUND)
                break;
        }
        if (once || plat_sleep(cfg.interval * 60) != AD_OK)
            break;
    }
    if (rc == AD_ERR_AUTH)
        Printf("Sign in again: %s LOGIN\n", (LONG)AMIDRIVE_NAME);
    if (rc == AD_ERR_BREAK)
        return RETURN_WARN;
    if (rc != AD_OK)
        return RETURN_ERROR;
    return ss.failed ? RETURN_WARN : RETURN_OK;
}

/* Write a small sync state to T:, read it back, delete it: the streamed, safely
 * replaced files of plat_amiga.c on a real file system. */
static int test_state_file(void)
{
    static State st;
    static const char path[] = "T:AmiDrive-test.state";
    StEntry e, *f;
    int rc, ok;

    state_init(&st);
    strcpy(st.root_id, "ROOT!1");
    memset(&e, 0, sizeof(e));
    e.id = "FILE!2";
    e.parent = "ROOT!1";
    e.name = "tab\tname";
    e.lname = "name";
    e.size = 123456;
    ok = state_put(&st, &e) != NULL && state_save(&st, path) == AD_OK &&
         state_save(&st, path) == AD_OK;                /* the second one replaces */
    state_free(&st);
    rc = state_load(&st, path);
    f = state_find(&st, "FILE!2");
    ok = ok && rc == AD_OK && st.count == 1 && f && f->size == 123456 &&
         strcmp(f->name, "tab\tname") == 0 && strcmp(st.root_id, "ROOT!1") == 0;
    state_free(&st);
    plat_delete_file(path);
    Printf("State file (%s): %s\n", (LONG)path, (LONG)(ok ? "OK" : "FAILED"));
    if (!ok)
        return AD_ERR;

    /* a download: written in place, dated, renamed over nothing, examined */
    {
        static const char part[] = "T:" SYNC_PART_NAME, done[] = "T:AmiDrive-test.txt";
        unsigned long date = 1500000000UL;          /* 2025-07-14 02:40:00 local */
        PlatFile *pf = plat_create(part);
        PlatInfo info;

        ok = pf && plat_write(pf, "12345", 5) == AD_OK && plat_close(pf, 1) == AD_OK &&
             plat_set_date(part, date) == AD_OK && plat_rename(part, done) == AD_OK &&
             plat_file_info(done, &info) == AD_OK && info.size == 5 && info.date == date &&
             !info.dir && plat_file_info(part, &info) != AD_OK && plat_file_info("T:", &info) == AD_OK &&
             info.dir;
        plat_delete_file(done);
        Printf("Files (create, date, rename, examine): %s\n", (LONG)(ok ? "OK" : "FAILED"));
        Printf("Name limit: SYS: %ld, RAM: %ld characters\n\n",
               (LONG)plat_name_limit("SYS:"), (LONG)plat_name_limit("RAM:"));
    }
    return ok ? AD_OK : AD_ERR;
}

static int cmd_test(const char *host, const char *tzsrc)
{
    char iso[24];
    struct DateStamp t0;
    unsigned long local = plat_local_now(), utc = plat_now();
    long off = tz_offset_utc(plat_timezone(), utc) / 60;
    Session *s = &graph_sess;
    int rc;

    Printf("%s %s self test\n\n", (LONG)AMIDRIVE_NAME, (LONG)AMIDRIVE_VERSION);
    Printf("Time zone: %s\n", (LONG)tzsrc);
    tz_format_iso8601(local, iso);
    iso[19] = '\0';
    Printf("Local:     %s (UTC%s%ld:%02ld)\n", (LONG)iso, (LONG)(off < 0 ? "-" : "+"),  /* no %+ld */
           (LONG)((off < 0 ? -off : off) / 60), (LONG)((off < 0 ? -off : off) % 60));
    tz_format_iso8601(utc, iso);
    Printf("UTC:       %s\n\n", (LONG)iso);
    if (test_state_file() != AD_OK)
        return RETURN_ERROR;

    if ((rc = start_network()) != RETURN_OK)
        return rc;
    Printf("bsdsocket.library %ld.%ld, AmiSSL ready\n",
           (LONG)SocketBase->lib_Version, (LONG)SocketBase->lib_Revision);

    sess_init(s, host);
    DateStamp(&t0);
    Printf("Connecting to %s:443 ...\n", (LONG)host);
    if (!(s->conn = net_connect(host, 443, SESS_TIMEOUT, s->err, sizeof(s->err)))) {
        Printf("  failed: %s\n", (LONG)s->err);
        return RETURN_ERROR;
    }
    http_init(&s->http, s->conn);
    s->fresh = 1;
    Printf("  %s, certificate OK, %ld ms\n\n", (LONG)net_tls_info(s->conn), ticks_since(&t0) * 20);

    if (strcmp(host, AD_GRAPH_HOST) != 0)                 /* other host: one request */
        return test_get(s, "/") == AD_OK ? RETURN_OK : RETURN_ERROR;

    rc = test_get(s, "/v1.0/");
    if (rc == AD_OK && !plat_break()) {
        int reused = s->conn != NULL;
        DateStamp(&t0);
        rc = test_get(s, "/v1.0/me");
        Printf("  second request (%s connection): %ld ms\n",
               (LONG)(reused ? "same" : "new"), ticks_since(&t0) * 20);
    }
    if (rc != AD_OK) {
        Printf("\nTest FAILED (%ld)\n", (LONG)rc);
        return RETURN_ERROR;
    }
    Printf("\nTest OK (/me answers 401 without a token)\n");
    return RETURN_OK;
}

int main(void)
{
    LONG args[ARG_COUNT];
    struct RDArgs *rda;
    char tzsrc[80];
    int rc, changed;

    memset(args, 0, sizeof(args));
    if (!(rda = ReadArgs(TEMPLATE, args, NULL))) {
        PrintFault(IoErr(), (STRPTR)AMIDRIVE_NAME);
        return RETURN_FAIL;
    }
    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38);
    plat_init_timezone(tzsrc, sizeof(tzsrc));
    sess_init(&login_sess, AD_LOGIN_HOST);
    sess_init(&graph_sess, AD_GRAPH_HOST);
    sess_init(&dl_sess, NULL);             /* host from each download redirect */

    rc = load_settings(args, &changed);
    if (rc == RETURN_OK && changed)
        rc = save_settings();

    if (rc != RETURN_OK) {
        /* invalid argument or settings not saved */
    } else if (args[ARG_TEST]) {
        rc = cmd_test(args[ARG_HOST] ? (const char *)args[ARG_HOST] : AD_GRAPH_HOST, tzsrc);
    } else if (args[ARG_LOGIN]) {
        rc = cmd_login();
    } else if (args[ARG_LOGOUT]) {
        rc = cmd_logout();
    } else if (args[ARG_INFO]) {
        rc = cmd_info();
    } else if (!cfg.local[0] || !cfg.remote[0]) {
        Printf("Set the folders first: %s LOCAL=<Amiga directory> REMOTE=<OneDrive folder>\n",
               (LONG)AMIDRIVE_NAME);
        rc = RETURN_WARN;
    } else {
        rc = cmd_sync(args[ARG_ONCE] != 0);
    }

    if (plat_break())
        Printf("*** Break\n");
    sess_close(&login_sess);
    sess_close(&graph_sess);
    sess_close(&dl_sess);
    state_free(&state);
    memset(&tokens, 0, sizeof(tokens));
    net_cleanup();
    if (LocaleBase)
        CloseLibrary((struct Library *)LocaleBase);
    FreeArgs(rda);
    return rc;
}
