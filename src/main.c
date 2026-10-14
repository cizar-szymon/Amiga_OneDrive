/* AmiDrive - OneDrive client for AmigaOS: command line, settings, sign-in, self test.
 *
 *   AmiDrive [LOCAL=Work:OneDrive] [REMOTE=/Amiga] [ONCE] [CHARSET=LATIN1|LATIN2]
 *            [UPLOAD=YES|NO]                 YES: also send the changes made on the Amiga
 *            [INTERVAL=3] [STATE=dir]       synchronise (settings are remembered)
 *            [APPICON=YES|NO]               YES: "OneDrive" icon on the Workbench in the background
 *   AmiDrive LOGIN          sign in with a device code
 *   AmiDrive LOGOUT         forget the sign-in (deletes the token file)
 *   AmiDrive INFO           settings, sign-in state, OneDrive owner and space
 *   AmiDrive TEST           check clock, time zone, DNS, TLS and HTTP against Graph
 *   AmiDrive TEST HOST=x    TLS + "GET /" to another host (e.g. expired.badssl.com)
 *   AmiDrive TEST URL=u [TO=file] [RCVBUF=n]   download speed of this client: http://
 *                           (plain TCP) against https:// (TLS), same code and buffers
 *   AmiDrive STOP           stop the AmiDrive running in the background (waits for it)
 *   AmiDrive SETCLOCK       set the Amiga clock (and battery clock) from the Internet
 *            [CLOCK=ASK|AUTO|NO]            wrong clock: window / set it / nothing
 *
 * Settings: Shell arguments > Tool Types of the icon > the prefs file
 * (ENVARC:AmiDrive/AmiDrive.prefs). Only Shell arguments are saved.
 *
 * From Workbench: the Tool Types are the arguments (others, like TOOLPRI, are ignored;
 * "(...)" is switched off). Output goes to a window that opens only when there is
 * something to show (errors, sign-in); the progress of the synchronisation only with
 * WINDOW. Not signed in: the sign-in runs in that window first. Started again while
 * running: asks whether to stop it.
 *
 * Only one synchronisation runs at a time (public port "AmiDrive"). Each pass is also
 * written to <state>/AmiDrive.log (AmiDrive.log.old after 64 KB).
 *
 * In the background (not ONCE) the "OneDrive" AppIcon (appicon_amiga.c) opens the
 * directory, takes files dropped on it and adds "synchronise now" / "stop" to Tools.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <libraries/locale.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/intuition.h>

#include "amidrive.h"
#include "appicon_amiga.h"
#include "clock_amiga.h"
#include "status_amiga.h"
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

/* in the data hunk: in the code hunk (const) Version showed "Drive 0.3.2" */
char amidrive_version[] = "$VER: " AMIDRIVE_NAME " " AMIDRIVE_VERSION " (" AMIDRIVE_DATE ")";

#define TEMPLATE "LOCAL/K,REMOTE/K,ONCE/S,CHARSET/K,UPLOAD/K,INTERVAL/K,STATE/K,CLIENTID/K,TENANT/K," \
                 "LOGIN/S,LOGOUT/S,INFO/S,TEST/S,HOST/K,WINDOW/S,STOP/S,APPICON/K,CLOCK/K,SETCLOCK/S," \
                 "URL/K,TO/K,RCVBUF/K/N"
enum { ARG_LOCAL, ARG_REMOTE, ARG_ONCE, ARG_CHARSET, ARG_UPLOAD, ARG_INTERVAL, ARG_STATE, ARG_CLIENTID,
       ARG_TENANT, ARG_LOGIN, ARG_LOGOUT, ARG_INFO, ARG_TEST, ARG_HOST, ARG_WINDOW, ARG_STOP, ARG_APPICON,
       ARG_CLOCK, ARG_SETCLOCK, ARG_URL, ARG_TO, ARG_RCVBUF, ARG_COUNT };

/* the output window when started from Workbench: opens on the first output */
#define WB_WINDOW "CON:40/30/600/180/" AMIDRIVE_NAME "/AUTO/CLOSE/WAIT"
#define LOG_NAME  "AmiDrive.log"
#define LOG_MAX   65536L                /* then renamed to AmiDrive.log.old */
#define PORT_NAME AMIDRIVE_NAME         /* public port of the running synchronisation */

/* arguments that are settings, in template order, with their prefs keys */
static const struct { int arg; const char *key; } setting_args[] = {
    { ARG_LOCAL, "LOCAL" }, { ARG_REMOTE, "REMOTE" }, { ARG_CHARSET, "CHARSET" }, { ARG_UPLOAD, "UPLOAD" },
    { ARG_INTERVAL, "INTERVAL" }, { ARG_STATE, "STATE" }, { ARG_CLIENTID, "CLIENTID" },
    { ARG_TENANT, "TENANT" }, { ARG_APPICON, "APPICON" }, { ARG_CLOCK, "CLOCK" }
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
static struct WBStartup *wbmsg;         /* started from Workbench */
static int quiet;                       /* from Workbench without WINDOW: progress only to the log */
static BPTR logfh;                      /* the log, open during a pass */
static char log_path[CFG_PATH_SIZE + 32];
static struct MsgPort *instance;        /* our public port while synchronising */
static char progname[108];              /* our program file (image of the AppIcon) */

/* ---- output: the Shell or the window, and the log ------------------------------- */

/* The arguments are LONGs on the stack (vbcc: va_list points at them), as VFPrintf wants. */
static void out_v(int progress, const char *fmt, va_list ap)
{
    if (Output() && !(progress && quiet))
        VFPrintf(Output(), (CONST_STRPTR)fmt, (CONST_APTR)ap);
    if (logfh)
        VFPrintf(logfh, (CONST_STRPTR)fmt, (CONST_APTR)ap);
}

/* messages and errors: always shown */
static void say(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    out_v(0, fmt, ap);
    va_end(ap);
}

/* progress of the synchronisation: from Workbench only with WINDOW */
static void note(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    out_v(1, fmt, ap);
    va_end(ap);
}

static void log_open(void)
{
    static char old[sizeof(log_path) + 4];
    PlatInfo info;

    if (logfh || !log_path[0])
        return;
    if (plat_file_info(log_path, &info) == AD_OK && info.size > LOG_MAX) {
        strcpy(old, log_path);
        strcat(old, ".old");
        plat_delete_file(old);
        plat_rename(log_path, old);
    }
    if ((logfh = Open((STRPTR)log_path, MODE_READWRITE)) != 0)
        Seek(logfh, 0, OFFSET_END);
}

static void log_close(void)
{
    if (logfh) {
        Close(logfh);
        logfh = 0;
    }
}

/* ---- one synchronisation at a time ---------------------------------------------- */

/* 1 if an AmiDrive is synchronising; with stop, Ctrl-C is sent to it */
static int signal_running(int stop)
{
    struct MsgPort *p;

    Forbid();
    if ((p = FindPort((STRPTR)PORT_NAME)) != NULL && stop)
        Signal(p->mp_SigTask, SIGBREAKF_CTRL_C);
    Permit();
    return p != NULL;
}

/* 0 if another AmiDrive is synchronising, else our port is added (if memory allows) */
static int instance_claim(void)
{
    Forbid();
    if (FindPort((STRPTR)PORT_NAME)) {
        Permit();
        return 0;
    }
    if ((instance = CreateMsgPort()) != NULL) {
        instance->mp_Node.ln_Name = (char *)PORT_NAME;
        instance->mp_Node.ln_Pri = -128;
        AddPort(instance);
    }
    Permit();
    return 1;
}

static void instance_release(void)
{
    if (instance) {
        RemPort(instance);
        DeleteMsgPort(instance);
        instance = NULL;
    }
}

/* Workbench: "already running, stop it?" */
static int ask_stop(void)
{
    struct IntuitionBase *IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 36);
    struct EasyStruct es;
    LONG r = 0;

    if (!IntuitionBase)
        return 0;
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (STRPTR)AMIDRIVE_NAME;
    es.es_TextFormat = (STRPTR)AMIDRIVE_NAME " is already running.\nStop it?";
    es.es_GadgetFormat = (STRPTR)"Stop|Cancel";
    r = EasyRequestArgs(NULL, &es, NULL, NULL);
    CloseLibrary((struct Library *)IntuitionBase);
    return r == 1;
}

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
        say("%s: interrupted\n", (LONG)what);
    else
        say("%s: %s\n", (LONG)what, (LONG)(err && *err ? err : "failed"));
}

/* ---- settings ------------------------------------------------------------------- */

static int load_settings(LONG *args, int *changed)
{
    unsigned int i;

    config_defaults(&cfg);
    if (plat_read_file(CFG_PREFS_FILE, text, sizeof(text)) >= 0 &&
        config_parse(&cfg, text) != AD_OK)
        say("Warning: invalid values in %s were ignored\n", (LONG)CFG_PREFS_FILE);

    *changed = 0;
    for (i = 0; i < sizeof(setting_args) / sizeof(setting_args[0]); i++) {
        const char *v = (const char *)args[setting_args[i].arg];
        if (!v)
            continue;
        if (config_set(&cfg, setting_args[i].key, v) != AD_OK) {
            say("Invalid %s=%s\n", (LONG)setting_args[i].key, (LONG)v);
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
        say("Cannot save the settings to %s\n", (LONG)CFG_PREFS_FILE);
        return RETURN_ERROR;
    }
    say("Settings saved to %s\n", (LONG)CFG_PREFS_FILE);
    return RETURN_OK;
}

static int start_network(void)
{
    char err[200];

    if (net_init(err, sizeof(err)) != AD_OK) {
        say("Network: %s\n", (LONG)err);
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
    say("OneDrive:  %s (%s), ", (LONG)local_text(di.owner), (LONG)di.drive_type);
    say("%lu.%lu of %lu.%lu GB used\n",
           di.used_mb / 1000, (di.used_mb % 1000) / 100, di.total_mb / 1000, (di.total_mb % 1000) / 100);
    return RETURN_OK;
}

/* ---- commands ------------------------------------------------------------------- */

/* ---- the clock (CLOCK=ASK|AUTO|NO, SETCLOCK) ------------------------------------ */

static unsigned long last_clock_check;  /* UTC of the last check (AUTO: every 24 h) */

/* local seconds -> "Wed 14-Oct-2026 12:20:05" (out: 32 bytes) */
static void format_local(unsigned long t, char *out)
{
    static const char wd[] = "SunMonTueWedThuFriSat", mo[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    unsigned long days = t / 86400UL, tod = t % 86400UL;
    long y;
    int m, d;

    tz_civil_from_days((long)days, &y, &m, &d);
    sprintf(out, "%.3s %02d-%.3s-%ld %02lu:%02lu:%02lu", wd + (days % 7) * 3, d, mo + (m - 1) * 3,
            y, tod / 3600, tod / 60 % 60, tod % 60);        /* 1978-01-01 was a Sunday */
}

/* Change one key of the prefs file only (not the Tool Types or arguments of this run). */
static void save_one_setting(const char *key, const char *value)
{
    static Config c;
    int n;

    config_defaults(&c);
    if (plat_read_file(CFG_PREFS_FILE, text, sizeof(text)) >= 0)
        config_parse(&c, text);
    if (config_set(&c, key, value) == AD_OK && (n = config_format(&c, text, sizeof(text))) > 0 &&
        plat_make_dir(CFG_DIR) == AD_OK)
        plat_write_file(CFG_PREFS_FILE, text, n);
}

/* diff (> 0: the Amiga is behind) -> "365 days behind", "1 h 0 min ahead", "3 min 5 s behind" */
static void format_offset(long diff, char *out)
{
    long a = diff < 0 ? -diff : diff;
    const char *dir = diff > 0 ? "behind" : "ahead";

    if (a >= 2 * 86400L)
        sprintf(out, "%ld days %s", a / 86400L, dir);
    else if (a >= 3600)
        sprintf(out, "%ld h %ld min %s", a / 3600, a / 60 % 60, dir);
    else
        sprintf(out, "%ld min %ld s %s", a / 60, a % 60, dir);
}

/* The window: 1 = set the clock, 2 = set it and from now on automatically, 0 = no */
static int ask_set_clock(const char *amiga, const char *net, long diff)
{
    struct IntuitionBase *IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 36);
    struct EasyStruct es;
    static char off[48];
    ULONG args[4];
    LONG r;

    if (!IntuitionBase)
        return 0;
    format_offset(diff, off);
    args[0] = (ULONG)amiga;
    args[1] = (ULONG)net;
    args[2] = (ULONG)off;
    args[3] = (ULONG)AD_GRAPH_HOST;
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (STRPTR)AMIDRIVE_NAME ": the clock is wrong";
    es.es_TextFormat = (STRPTR)
        "The Amiga clock is wrong, so secure connections to\n"
        "OneDrive fail (certificates seem not valid yet / expired)\n"
        "and file dates are wrong.\n\n"
        "Amiga clock:     %s\n"
        "Internet time:   %s\n"
        "The Amiga clock is %s.\n"
        "(time of %s, verified server)\n\n"
        "Set the Amiga clock (also the battery clock) now?";
    es.es_GadgetFormat = (STRPTR)"Set clock|Always set automatically|Cancel";
    r = EasyRequestArgs(NULL, &es, NULL, args);
    CloseLibrary((struct Library *)IntuitionBase);
    return (int)r;
}

/* Compare the clock with the Internet time and fix it. ask: show the window first (only
 * when more than 2 minutes off, or always with force: a certificate was rejected for its
 * dates); else set it without asking when 2 s or more off. Returns 1 if it was set. */
static int check_clock(int ask, int force)
{
    char err[200], a[32], n[32];
    unsigned long utc, before;
    long diff;
    int r, battery;

    if (start_network() != RETURN_OK)
        return 0;
    last_clock_check = plat_now();
    if (clock_internet(AD_GRAPH_HOST, &utc, err, sizeof(err)) != AD_OK) {
        (force ? say : note)("Clock: no time from the Internet (%s)\n", (LONG)err);
        return 0;
    }
    before = plat_now();
    diff = (long)(utc - before);            /* > 0: the Amiga is behind */
    if (diff > -2 && diff < 2)
        return 0;
    if (ask) {
        if (!force && diff > -120 && diff < 120)
            return 0;                       /* a little drift: not worth a window */
        format_local(plat_local_now(), a);
        format_local(tz_to_local(plat_timezone(), utc), n);
        r = ask_set_clock(a, n, diff);
        if (r == 0) {
            note("Clock: not changed (Amiga %s, Internet %s)\n", (LONG)a, (LONG)n);
            return 0;
        }
        if (r == 2) {
            strcpy(cfg.clock, "AUTO");
            save_one_setting("CLOCK", "AUTO");
        }
        utc += plat_now() - before;         /* the time the window was open */
    }
    if (clock_set_local(tz_to_local(plat_timezone(), utc), &battery) != AD_OK) {
        say("Clock: cannot set the time (timer.device)\n");
        return 0;
    }
    format_local(plat_local_now(), n);
    format_offset(diff, a);
    note("Clock set from the Internet to %s (it was %s)%s\n", (LONG)n, (LONG)a,
         (LONG)(battery ? "" : ", no battery clock"));
    return 1;
}

/* At the start: AUTO checks always, ASK only when the clock is before this release
 * (certainly wrong; other errors show up as rejected certificates, see clock_retry). */
static void clock_at_start(void)
{
    unsigned long release = (unsigned long)tz_days_from_civil(AMIDRIVE_YEAR, AMIDRIVE_MONTH, AMIDRIVE_DAY) * 86400UL;

    if (last_clock_check)               /* once per run (sign-in, then synchronisation) */
        return;
    if (cfg.clock[0] == 'A' && cfg.clock[1] == 'U')
        check_clock(0, 0);
    else if (cfg.clock[0] == 'A' && plat_now() < release)
        check_clock(1, 1);
}

/* After a failed request: if a certificate was rejected for its dates, offer / do the
 * clock fix (not with CLOCK=NO). Returns 1 if the clock was set: try again. */
static int clock_retry(void)
{
    if (!net_cert_time_error || cfg.clock[0] == 'N')
        return 0;
    net_cert_time_error = 0;
    return check_clock(cfg.clock[1] != 'U', 1);
}

/* AmiDrive SETCLOCK: set the clock from the Internet now (no window). */
static int cmd_setclock(void)
{
    char err[200], a[32], n[32];
    unsigned long utc;
    int rc, battery;

    if ((rc = start_network()) != RETURN_OK)
        return rc;
    if (clock_internet(AD_GRAPH_HOST, &utc, err, sizeof(err)) != AD_OK) {
        say("No time from the Internet: %s\n", (LONG)err);
        return RETURN_ERROR;
    }
    format_local(plat_local_now(), a);
    format_local(tz_to_local(plat_timezone(), utc), n);
    say("Amiga clock:    %s\nInternet time:  %s (%s)\n", (LONG)a, (LONG)n, (LONG)AD_GRAPH_HOST);
    if (clock_set_local(tz_to_local(plat_timezone(), utc), &battery) != AD_OK) {
        say("Cannot set the time (timer.device)\n");
        return RETURN_ERROR;
    }
    say("Clock set%s\n", (LONG)(battery ? " (also the battery clock)" : " (no battery clock found)"));
    return RETURN_OK;
}

static int cmd_login(void)
{
    char err[200];
    AuthApp a = app();
    int rc;

    if ((rc = start_network()) != RETURN_OK)
        return rc;
    clock_at_start();
    say("Signing in to Microsoft (%s) ...\n", (LONG)cfg.tenant);
    if ((rc = auth_device_start(&login_sess, &a, &device, err, sizeof(err))) != AD_OK && clock_retry()) {
        sess_close(&login_sess);
        rc = auth_device_start(&login_sess, &a, &device, err, sizeof(err));
    }
    if (rc != AD_OK) {
        print_rc("Sign-in", rc, err);
        return RETURN_ERROR;
    }
    say("\nOn a phone or PC open   %s\n", (LONG)device.uri);
    say("and enter the code      %s\n\n", (LONG)device.user_code);
    say("Waiting (up to %ld minutes, Ctrl-C cancels) ...\n", device.expires_in / 60);

    rc = auth_device_wait(&login_sess, &a, &device, &tokens, err, sizeof(err));
    memset(&device, 0, sizeof(device));
    if (rc != AD_OK) {
        print_rc("Sign-in", rc, err);
        return rc == AD_ERR_BREAK ? RETURN_WARN : RETURN_ERROR;
    }
    if (plat_make_dir(CFG_DIR) != AD_OK || auth_save(&tokens, CFG_TOKEN_FILE) != AD_OK) {
        say("Signed in, but cannot save %s\n", (LONG)CFG_TOKEN_FILE);
        return RETURN_ERROR;
    }
    say("Signed in. Token saved to %s\n", (LONG)CFG_TOKEN_FILE);
    return show_drive();
}

static int cmd_logout(void)
{
    plat_delete_file(CFG_TOKEN_FILE);
    say("Signed out (%s deleted). Run AmiDrive LOGIN to sign in again.\n", (LONG)CFG_TOKEN_FILE);
    return RETURN_OK;
}

static int cmd_info(void)
{
    int rc;

    say("%s %s\n", (LONG)AMIDRIVE_NAME, (LONG)AMIDRIVE_VERSION);
    say("Local:     %s\n", (LONG)(cfg.local[0] ? cfg.local : "(not set, use LOCAL=)"));
    say("Remote:    %s\n", (LONG)(cfg.remote[0] ? cfg.remote : "(not set, use REMOTE=)"));
    say("Charset:   %s, check every %ld min, %s\n", (LONG)cfg.charset, cfg.interval,
           (LONG)(cfg.upload[0] == 'Y' ? "both ways" : "download only (UPLOAD=NO)"));
    say("Clock:     %s\n", (LONG)(cfg.clock[0] == 'N' ? "not checked (CLOCK=NO)" :
                                   cfg.clock[1] == 'U' ? "set automatically from the Internet (CLOCK=AUTO)" :
                                   "window when it is wrong (CLOCK=ASK)"));
    say("AppIcon:   %s\n", (LONG)(cfg.appicon[0] == 'Y' ? "\"OneDrive\" on the Workbench" : "no (APPICON=NO)"));
    if (cfg.state[0])
        say("State:     %s\n", (LONG)cfg.state);
    else if (cfg.local[0])
        say("State:     %s/.AmiDrive\n", (LONG)cfg.local);
    say("App ID:    %s (%s)\n", (LONG)(cfg.client_id[0] ? cfg.client_id : "not set, use CLIENTID="),
           (LONG)cfg.tenant);

    if (auth_load(&tokens, CFG_TOKEN_FILE) != AD_OK) {
        say("Sign-in:   not signed in, run AmiDrive LOGIN\n");
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

    say("GET %s\n", (LONG)path);
    if ((rc = sess_request(s, "GET", path, "Accept: application/json\r\n", NULL, 0)) != AD_OK) {
        say("  failed: %s\n", (LONG)s->err);
        return rc;
    }
    say("  status %ld, %s, keep-alive %s\n", (LONG)s->http.status,
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
    say("  body: %s\n", (LONG)body);
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
    note("  %s\n", (LONG)msg);
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
    note("%s ", (LONG)iso);
}

/* ---- the status window: progress of a pass, or the state between passes -------------- */

static struct {
    struct DateStamp pass_start, phase_start;
    int phase, auto_shown;
    unsigned long xfer;                 /* bytes really transferred in this phase */
    const char *last_file;
    unsigned long last_done;            /* file_done at the last report */
    long last_draw;                     /* ticks since phase_start */
} pg;
static char last_result[120];           /* "12:31 no changes", for the idle window */
static unsigned long next_check;        /* local time of the next pass, 0 = none */

/* 1234 -> "1234 bytes", "880 KB", "12.3 MB" */
static void fmt_bytes(unsigned long b, char *out)
{
    if (b < 10000UL)
        sprintf(out, "%lu bytes", b);
    else if (b < 1000000UL)
        sprintf(out, "%lu KB", (b + 512) / 1024);
    else
        sprintf(out, "%lu.%lu MB", b / 1048576UL, (b % 1048576UL) * 10 / 1048576UL);
}

/* seconds left -> "~40 s", "~3 min", "~1 h 20 min" (the window is narrow) */
static void fmt_left(unsigned long s, char *out)
{
    if (s < 60)
        sprintf(out, "~%lu s", s < 5 ? 5UL : (s + 4) / 5 * 5);
    else if (s < 3600)
        sprintf(out, "~%lu min", (s + 30) / 60);
    else
        sprintf(out, "~%lu h %lu min", s / 3600, s / 60 % 60);
}

/* the path below LOCAL ("Gry/a.adf") */
static const char *below_local(const char *path)
{
    size_t n = strlen(cfg.local);

    if (strncmp(path, cfg.local, n) == 0) {
        path += n;
        if (*path == '/')
            path++;
    }
    return path;
}

/* done of total in percent, without overflowing 32 bits */
static int percent_of(unsigned long done, unsigned long total)
{
    unsigned long p;

    if (!total)
        return -1;
    p = total >= 0x01000000UL ? done / (total / 100) : done * 100 / total;
    return p > 100 ? 100 : (int)p;
}

static void show_idle(void)
{
    static char l2[160], l3[80], t[24];

    if (!status_is_open())
        return;
    sprintf(l2, "Last check %s", last_result[0] ? last_result : "-");
    if (next_check) {
        tz_format_iso8601(next_check, t);
        t[16] = '\0';
        sprintf(l3, "Next at %s", t + 11);
    } else {
        l3[0] = '\0';
    }
    status_show("Up to date", l2, l3, -1);
}

static void show_progress(const SyncProgress *p)
{
    static char l1[100], l2[200], l3[160], a[24], b[24], c[40];
    long t = ticks_since(&pg.phase_start);
    unsigned long rate = 0;
    int pct = percent_of(p->done, p->total);
    const char *what = p->phase == SYNC_SEND ? "Uploading" : "Downloading";

    if (t >= 50)                        /* bytes per second, after a second */
        rate = pg.xfer > 40000000UL ? pg.xfer / (unsigned long)t * 50 : pg.xfer * 50 / (unsigned long)t;

    if (p->phase == SYNC_SCAN) {
        status_show("Checking the Amiga", cfg.local, "", -1);
        return;
    }
    if (p->phase == SYNC_CLOUD && pct < 0 && !p->file)
        what = "Checking OneDrive";     /* reading the changes, nothing to download yet */
    if (pct >= 0)
        sprintf(l1, "%s: %d %%", what, pct);
    else
        strcpy(l1, what);

    if (p->file) {
        int fp = percent_of(p->file_done, p->file_size);

        /* the percentage first: long names are cut at the window edge */
        sprintf(l2, "%d%% %.120s", fp < 0 ? 0 : fp, below_local(p->file));
        if (pct < 0)                    /* the whole is unknown: the bar shows the file */
            pct = percent_of(p->file_done, p->file_size);
    } else if (p->phase == SYNC_CLOUD) {
        sprintf(l2, "%ld items read", p->items);
    } else {
        l2[0] = '\0';
    }

    if (p->total) {
        fmt_bytes(p->done, a);
        fmt_bytes(p->total, b);
        sprintf(l3, "%s/%s", a, b);
    } else {
        fmt_bytes(p->done, a);
        sprintf(l3, "%ld files, %s", p->files, a);
    }
    if (rate) {
        fmt_bytes(rate, b);
        sprintf(l3 + strlen(l3), "  %s/s", b);
        if (p->total && t >= 250 && p->total > p->done) {      /* an estimate after 5 s */
            fmt_left((p->total - p->done) / rate, c);
            sprintf(l3 + strlen(l3), "  %s", c);
        }
    }
    status_show(l1, l2, l3, pct);
}

/* sync_run progress: the icon and the window answer during the pass too */
static void sync_progress(void *ud, const SyncProgress *p)
{
    long t;
    int r, force = 0;

    (void)ud;
    if (p->phase != pg.phase) {         /* a new phase: speed counted again */
        pg.phase = p->phase;
        pg.xfer = 0;
        pg.last_draw = -1000;
        DateStamp(&pg.phase_start);
        force = 1;
    }
    if (p->file != pg.last_file || p->file_done < pg.last_done) {
        pg.last_file = p->file;
        pg.last_done = 0;
        force = 1;
    }
    if (p->file) {
        pg.xfer += p->file_done - pg.last_done;
        pg.last_done = p->file_done;
    }

    r = appicon_poll();
    if (r & APPICON_STOP)
        Signal(FindTask(NULL), SIGBREAKF_CTRL_C);       /* the pass ends at the next check */
    if (r & APPICON_STATUS)
        force |= status_open();
    if (status_handle() & STATUS_STOP)                  /* "Synchronise now": running already */
        Signal(FindTask(NULL), SIGBREAKF_CTRL_C);

    /* a long first listing: show the window once by itself (closed: stays closed) */
    if (!pg.auto_shown && p->total && cfg.appicon[0] == 'Y' && ticks_since(&pg.pass_start) >= 500) {
        pg.auto_shown = 1;
        force |= status_open();
    }
    if (!status_is_open())
        return;
    t = ticks_since(&pg.phase_start);
    if (!force && t - pg.last_draw < 25)                /* at most twice a second */
        return;
    pg.last_draw = t;
    show_progress(p);
}

static void pass_ends(int rc, const SyncStats *ss, const char *err, int once)
{
    char t[24];
    long changes = ss->downloaded + ss->moved + ss->deleted + ss->folders + ss->uploaded +
                   ss->remote_moved + ss->remote_deleted + ss->remote_folders;

    tz_format_iso8601(plat_local_now(), t);
    t[16] = '\0';
    if (rc != AD_OK)
        sprintf(last_result, "%s stopped: %.80s", t + 11, rc == AD_ERR_BREAK ? "interrupted" : err);
    else if (!changes)
        sprintf(last_result, "%s no changes%s", t + 11, ss->failed ? " (some failed, tried again)" : "");
    else
        sprintf(last_result, "%s %ld downloaded, %ld sent, %ld other changes%s", t + 11,
                ss->downloaded, ss->uploaded, changes - ss->downloaded - ss->uploaded,
                ss->failed ? ", some failed" : "");
    next_check = once ? 0 : plat_local_now() + (unsigned long)cfg.interval * 60;
    status_busy(0);
    show_idle();
}

static void pass_begins(void)
{
    memset(&pg, 0, sizeof(pg));
    DateStamp(&pg.pass_start);
    pg.phase_start = pg.pass_start;
    next_check = 0;
    status_busy(1);
    if (status_is_open())
        status_show("Checking OneDrive", "Connecting ...", "", -1);
}

/* Wait secs between passes. AD_OK: time is up or "synchronise now" (AppIcon menu or
 * files dropped on it); AD_ERR_BREAK: Ctrl-C or "stop". */
static int wait_next(long secs)
{
    ULONG sig = appicon_signal(), wsig;
    long steps = secs * 5;              /* 1/5 s steps, like plat_sleep */
    int r;

    for (;;) {
        if (plat_break())
            return AD_ERR_BREAK;
        if (sig && (SetSignal(0, sig) & sig)) {
            r = appicon_handle();
            if (r & APPICON_STOP)
                return AD_ERR_BREAK;
            if (r & APPICON_SYNC)
                return AD_OK;
            if ((r & APPICON_STATUS) && status_open())
                show_idle();
        }
        if ((wsig = status_signal()) != 0 && (SetSignal(0, wsig) & wsig)) {
            r = status_handle();
            if (r & STATUS_STOP)
                return AD_ERR_BREAK;
            if (r & STATUS_SYNC)
                return AD_OK;
        }
        if (steps-- <= 0)
            return AD_OK;
        Delay(TICKS_PER_SECOND / 5);
    }
}

static void icon_log(const char *msg)
{
    int was_open = logfh != 0;

    log_open();
    print_time();
    note("%s\n", (LONG)msg);
    if (!was_open)
        log_close();
}

/* Synchronise now (once) or every INTERVAL minutes until Ctrl-C. */
static int cmd_sync(int once)
{
    static char remote[CFG_PATH_SIZE * 2], state_dir[CFG_PATH_SIZE + 16], state_file[CFG_PATH_SIZE + 24];
    FsCharset cs = cfg.charset[5] == '2' ? FS_LATIN2 : FS_LATIN1;
    SyncConfig sc;
    SyncStats ss;
    char err[200];
    int rc, fatal;

    if (!instance_claim()) {
        if (!wbmsg)
            say("%s is already running (%s STOP stops it)\n", (LONG)AMIDRIVE_NAME, (LONG)AMIDRIVE_NAME);
        else if (ask_stop())
            signal_running(1);
        return RETURN_WARN;
    }
    if (fs_to_utf8(cs, cfg.remote, remote, sizeof(remote)) < 0) {
        say("REMOTE too long\n");
        return RETURN_ERROR;
    }
    /* STATE=dir, else <LOCAL>/.AmiDrive; in it "state" and the log */
    strcpy(state_dir, cfg.state[0] ? cfg.state : cfg.local);
    if (!cfg.state[0])
        add_name(state_dir, SYNC_STATE_DIR);
    strcpy(state_file, state_dir);
    add_name(state_file, "state");
    strcpy(log_path, state_dir);
    add_name(log_path, LOG_NAME);
    plat_make_dir(cfg.local);
    plat_make_dir(state_dir);
    log_open();
    if ((rc = start_network()) != RETURN_OK)
        return rc;
    clock_at_start();

    if (auth_load(&tokens, CFG_TOKEN_FILE) != AD_OK) {
        if (!wbmsg) {
            say("Not signed in: run %s LOGIN first\n", (LONG)AMIDRIVE_NAME);
            return RETURN_WARN;
        }
        log_close();                    /* started from the icon: sign in now, in the window */
        if ((rc = cmd_login()) != RETURN_OK)
            return rc;
        log_open();
    }
    if ((rc = start_network()) != RETURN_OK)
        return rc;
    if (!once && cfg.appicon[0] == 'Y')
        icon_log(appicon_open("OneDrive", cfg.local, progname, icon_log) ?
                 "Icon \"OneDrive\" added to the Workbench" : "No Workbench icon (Workbench not running?)");

    memset(&sc, 0, sizeof(sc));
    sc.graph = &graph_sess;
    sc.dl = &dl_sess;
    sc.local = cfg.local;
    sc.remote = remote;
    sc.state_file = state_file;
    sc.charset = cs;
    sc.tz = plat_timezone();
    sc.upload = cfg.upload[0] == 'Y';
    sc.token = sync_token;
    sc.log = sync_log;
    sc.progress = sync_progress;

    state_init(&state);
    if (state_load(&state, state_file) == AD_ERR_PROTO)
        say("Warning: %s is damaged, the whole folder will be read again\n", (LONG)state_file);

    for (;;) {
        plat_make_dir(cfg.local);
        log_open();
        if (cfg.clock[0] == 'A' && cfg.clock[1] == 'U' && plat_now() - last_clock_check >= 86400UL)
            check_clock(0, 0);              /* CLOCK=AUTO: once a day */
        sc.maxname = plat_name_limit(cfg.local);
        print_time();
        note("Checking OneDrive %s -> %s\n", (LONG)cfg.remote, (LONG)cfg.local);
        pass_begins();
        rc = sync_run(&sc, &state, &ss, err, sizeof(err));
        pass_ends(rc, &ss, err, once);
        /* the connections are not kept for minutes of waiting */
        sess_close(&graph_sess);
        sess_close(&dl_sess);
        sess_close(&login_sess);

        fatal = rc == AD_ERR_BREAK || rc == AD_ERR_AUTH || rc == AD_ERR_NOTFOUND;
        if (rc == AD_OK) {
            if (ss.downloaded + ss.moved + ss.deleted + ss.folders + ss.kept + ss.failed +
                ss.uploaded + ss.remote_moved + ss.remote_deleted + ss.remote_folders == 0) {
                note("  no changes\n");
            } else {
                note("  Amiga:    %ld downloaded (%lu KB), %ld moved, %ld deleted, %ld new folders\n",
                     ss.downloaded, (ss.bytes + 1023) / 1024, ss.moved, ss.deleted, ss.folders);
                if (sc.upload)
                    note("  OneDrive: %ld uploaded (%lu KB), %ld moved, %ld deleted, %ld new folders\n",
                         ss.uploaded, (ss.up_bytes + 1023) / 1024, ss.remote_moved,
                         ss.remote_deleted, ss.remote_folders);
                if (ss.kept + ss.failed)
                    note("  %ld kept (changed on the Amiga), %ld failed (tried again next time)\n",
                         ss.kept, ss.failed);
            }
        } else if (fatal) {             /* these end the program: always shown (Ctrl-C below) */
            if (rc != AD_ERR_BREAK)
                print_rc("  Stopped", rc, err);
            break;
        } else {                        /* tried again after INTERVAL */
            note("  Stopped: %s\n", (LONG)(*err ? err : "failed"));
            if (clock_retry())              /* the clock was wrong and is set now */
                continue;
        }
        if (once)
            break;
        log_close();                    /* readable while waiting */
        if (wait_next(cfg.interval * 60) != AD_OK) {
            rc = AD_ERR_BREAK;
            break;
        }
    }
    appicon_close();
    status_close();
    if (rc == AD_ERR_AUTH)
        say("Sign in again: %s LOGIN\n", (LONG)AMIDRIVE_NAME);
    if (rc == AD_ERR_BREAK) {
        log_open();
        print_time();
        note("Stopped\n");
        return RETURN_WARN;
    }
    if (rc != AD_OK)
        return RETURN_ERROR;
    return ss.failed ? RETURN_WARN : RETURN_OK;
}

/* AmiDrive STOP: Ctrl-C to the running synchronisation, wait until it has ended */
static int cmd_stop(void)
{
    int i;

    if (!signal_running(1)) {
        say("%s is not running\n", (LONG)AMIDRIVE_NAME);
        return RETURN_WARN;
    }
    say("Stopping %s ...\n", (LONG)AMIDRIVE_NAME);
    for (i = 0; i < 120 && signal_running(0); i++)        /* up to 60 s */
        Delay(TICKS_PER_SECOND / 2);
    if (signal_running(0)) {
        say("Still running (busy with a request); it stops after it\n");
        return RETURN_WARN;
    }
    say("Stopped\n");
    return RETURN_OK;
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
    say("State file (%s): %s\n", (LONG)path, (LONG)(ok ? "OK" : "FAILED"));
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
        say("Files (create, date, rename, examine): %s\n", (LONG)(ok ? "OK" : "FAILED"));
        say("Name limit: SYS: %ld, RAM: %ld characters\n\n",
               (LONG)plat_name_limit("SYS:"), (LONG)plat_name_limit("RAM:"));
    }
    return ok ? AD_OK : AD_ERR;
}

/* AmiDrive TEST URL=...: how fast this client downloads, to compare plain TCP with TLS */
static int cmd_speed(const char *url, const char *to)
{
    static char host[128], body[16384];
    static Http h;
    const char *p, *path;
    char err[200];
    struct DateStamp t0;
    unsigned long total = 0;
    NetConn *c;
    BPTR fh = 0;
    long n, ticks;
    int port, rc;

    if (strncmp(url, "https://", 8) == 0) {
        port = 443;
        p = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        port = 80;
        p = url + 7;
    } else {
        say("URL must start with http:// or https://\n");
        return RETURN_ERROR;
    }
    if (!(path = strchr(p, '/')) || (size_t)(path - p) >= sizeof(host)) {
        say("Invalid URL\n");
        return RETURN_ERROR;
    }
    memcpy(host, p, (size_t)(path - p));
    host[path - p] = '\0';
    if ((rc = start_network()) != RETURN_OK)
        return rc;

    DateStamp(&t0);
    if (!(c = net_connect(host, port, SESS_TIMEOUT, err, sizeof(err)))) {
        say("Connect: %s\n", (LONG)err);
        return RETURN_ERROR;
    }
    say("%s:%ld  %s, %ld ms\n", (LONG)host, (LONG)port, (LONG)net_tls_info(c), ticks_since(&t0) * 20);
    http_init(&h, c);
    if ((rc = http_request(&h, "GET", host, path, NULL, NULL, 0)) != AD_OK ||
        (rc = http_read_response(&h)) != AD_OK || h.status != 200) {
        say("Request failed (%ld, HTTP %ld)\n", (LONG)rc, (LONG)h.status);
        net_close(c);
        return RETURN_ERROR;
    }
    if (to && !(fh = Open((STRPTR)to, MODE_NEWFILE))) {
        say("Cannot write %s\n", (LONG)to);
        net_close(c);
        return RETURN_ERROR;
    }
    DateStamp(&t0);
    while ((n = http_read_body(&h, body, (long)sizeof(body))) > 0) {
        total += (unsigned long)n;
        if (fh && Write(fh, body, n) != n)
            break;
        if (plat_break())
            break;
    }
    ticks = ticks_since(&t0);
    if (fh)
        Close(fh);
    net_close(c);
    if (ticks < 1)
        ticks = 1;
    say("%lu bytes in %ld.%02ld s = %lu KB/s%s%s\n", total, ticks / 50, (ticks % 50) * 2,
        total / 1024 * 50 / (unsigned long)ticks, (LONG)(to ? " to " : " (not saved)"),
        (LONG)(to ? to : ""));
    return n < 0 ? RETURN_ERROR : RETURN_OK;
}

static int cmd_test(const char *host, const char *tzsrc)
{
    char iso[24];
    struct DateStamp t0;
    unsigned long local = plat_local_now(), utc = plat_now();
    long off = tz_offset_utc(plat_timezone(), utc) / 60;
    Session *s = &graph_sess;
    int rc;

    say("%s %s self test\n\n", (LONG)AMIDRIVE_NAME, (LONG)AMIDRIVE_VERSION);
    say("Time zone: %s\n", (LONG)tzsrc);
    tz_format_iso8601(local, iso);
    iso[19] = '\0';
    say("Local:     %s (UTC%s%ld:%02ld)\n", (LONG)iso, (LONG)(off < 0 ? "-" : "+"),  /* no %+ld */
           (LONG)((off < 0 ? -off : off) / 60), (LONG)((off < 0 ? -off : off) % 60));
    tz_format_iso8601(utc, iso);
    say("UTC:       %s\n\n", (LONG)iso);
    if (test_state_file() != AD_OK)
        return RETURN_ERROR;

    if ((rc = start_network()) != RETURN_OK)
        return rc;
    say("bsdsocket.library %ld.%ld, AmiSSL ready\n",
           (LONG)SocketBase->lib_Version, (LONG)SocketBase->lib_Revision);

    sess_init(s, host);
    DateStamp(&t0);
    say("Connecting to %s:443 ...\n", (LONG)host);
    if (!(s->conn = net_connect(host, 443, SESS_TIMEOUT, s->err, sizeof(s->err)))) {
        say("  failed: %s\n", (LONG)s->err);
        return RETURN_ERROR;
    }
    http_init(&s->http, s->conn);
    s->fresh = 1;
    say("  %s, certificate OK, %ld ms\n\n", (LONG)net_tls_info(s->conn), ticks_since(&t0) * 20);

    if (strcmp(host, AD_GRAPH_HOST) != 0)                 /* other host: one request */
        return test_get(s, "/") == AD_OK ? RETURN_OK : RETURN_ERROR;

    rc = test_get(s, "/v1.0/");
    if (rc == AD_OK && !plat_break()) {
        int reused = s->conn != NULL;
        DateStamp(&t0);
        rc = test_get(s, "/v1.0/me");
        say("  second request (%s connection): %ld ms\n",
               (LONG)(reused ? "same" : "new"), ticks_since(&t0) * 20);
    }
    if (rc != AD_OK) {
        say("\nTest FAILED (%ld)\n", (LONG)rc);
        return RETURN_ERROR;
    }
    say("\nTest OK (/me answers 401 without a token)\n");
    return RETURN_OK;
}

/* ---- Workbench: the Tool Types as arguments ------------------------------------- */

/* 1 if the Tool Type ("KEY" or "KEY=value") names an argument of TEMPLATE */
static int template_key(const char *tt)
{
    const char *t = TEMPLATE;
    size_t n = 0;

    while (tt[n] && tt[n] != '=')
        n++;
    while (*t) {
        size_t k = 0;
        while (t[k] && t[k] != '/' && t[k] != ',')
            k++;
        if (k == n && n > 0) {
            size_t i;
            for (i = 0; i < n; i++)
                if ((tt[i] & ~0x20) != t[i])            /* keys are upper case letters */
                    break;
            if (i == n)
                return 1;
        }
        while (*t && *t != ',')
            t++;
        if (*t)
            t++;
    }
    return 0;
}

/* The Tool Types of our icon as a command line for ReadArgs: KEY="value" SWITCH ...
 * Tool Types that are not ours (TOOLPRI, DONOTWAIT, "(LOCAL=...)") are left out. */
static struct RDArgs *wb_args(LONG *args)
{
    static char line[1024];
    struct Library *IconBase = OpenLibrary((STRPTR)"icon.library", 36);
    struct DiskObject *dob;
    struct RDArgs *rda;
    size_t len = 0;

    line[0] = '\0';
    if (IconBase) {
        BPTR old = CurrentDir(wbmsg->sm_ArgList[0].wa_Lock);
        if ((dob = GetDiskObject(wbmsg->sm_ArgList[0].wa_Name)) != NULL) {
            STRPTR *tt;
            for (tt = (STRPTR *)dob->do_ToolTypes; tt && *tt; tt++) {
                const char *s = (const char *)*tt, *eq = strchr(s, '=');
                if (!template_key(s) || len + strlen(s) + 4 >= sizeof(line))
                    continue;
                if (eq) {               /* KEY="value": values may contain spaces */
                    memcpy(line + len, s, (size_t)(eq - s) + 1);
                    len += (size_t)(eq - s) + 1;
                    line[len++] = '"';
                    for (s = eq + 1; *s; s++)
                        if (*s != '"')
                            line[len++] = *s;
                    line[len++] = '"';
                } else {
                    strcpy(line + len, s);
                    len += strlen(s);
                }
                line[len++] = ' ';
                line[len] = '\0';
            }
            FreeDiskObject(dob);
        }
        CurrentDir(old);
        CloseLibrary(IconBase);
    }
    line[len++] = '\n';                 /* ReadArgs wants the line end */
    line[len] = '\0';

    if (!(rda = (struct RDArgs *)AllocDosObject(DOS_RDARGS, NULL)))
        return NULL;
    rda->RDA_Source.CS_Buffer = (STRPTR)line;
    rda->RDA_Source.CS_Length = (LONG)len;
    rda->RDA_Source.CS_CurChr = 0;
    rda->RDA_Flags |= RDAF_NOPROMPT;
    if (!ReadArgs((STRPTR)TEMPLATE, args, rda)) {
        PrintFault(IoErr(), (STRPTR)AMIDRIVE_NAME " Tool Types");
        FreeDosObject(DOS_RDARGS, rda);
        return NULL;
    }
    return rda;
}

int main(int argc, char **argv)
{
    LONG args[ARG_COUNT];
    struct RDArgs *rda;
    BPTR win = 0, oldout = 0;
    char tzsrc[80];
    int rc, changed;

    memset(args, 0, sizeof(args));
    if (argc == 0) {                    /* Workbench: output to a window, Tool Types */
        wbmsg = (struct WBStartup *)argv;
        strncpy(progname, (const char *)wbmsg->sm_ArgList[0].wa_Name, sizeof(progname) - 1);
        if ((win = Open((STRPTR)WB_WINDOW, MODE_NEWFILE)) != 0)
            oldout = SelectOutput(win);
        rda = wb_args(args);
        quiet = !args[ARG_WINDOW];
    } else if (!(rda = ReadArgs((STRPTR)TEMPLATE, args, NULL))) {
        PrintFault(IoErr(), (STRPTR)AMIDRIVE_NAME);
        return RETURN_FAIL;
    } else {
        GetProgramName((STRPTR)progname, sizeof(progname));
    }
    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38);
    plat_init_timezone(tzsrc, sizeof(tzsrc));
    sess_init(&login_sess, AD_LOGIN_HOST);
    sess_init(&graph_sess, AD_GRAPH_HOST);
    sess_init(&dl_sess, NULL);             /* host from each download redirect */

    if (!rda) {
        rc = RETURN_FAIL;                  /* invalid Tool Types (reported) */
    } else {
        rc = load_settings(args, &changed);
        if (rc == RETURN_OK && changed && !wbmsg)      /* Tool Types are not saved */
            rc = save_settings();
    }

    if (rc != RETURN_OK) {
        /* invalid argument or settings not saved */
    } else if (args[ARG_STOP]) {
        rc = cmd_stop();
    } else if (args[ARG_SETCLOCK]) {
        rc = cmd_setclock();
    } else if (args[ARG_TEST] && args[ARG_URL]) {
        if (args[ARG_RCVBUF])
            net_rcvbuf = *(LONG *)args[ARG_RCVBUF];
        rc = cmd_speed((const char *)args[ARG_URL], (const char *)args[ARG_TO]);
    } else if (args[ARG_TEST]) {
        rc = cmd_test(args[ARG_HOST] ? (const char *)args[ARG_HOST] : AD_GRAPH_HOST, tzsrc);
    } else if (args[ARG_LOGIN]) {
        rc = cmd_login();
    } else if (args[ARG_LOGOUT]) {
        rc = cmd_logout();
    } else if (args[ARG_INFO]) {
        rc = cmd_info();
    } else if (!cfg.local[0] || !cfg.remote[0]) {
        if (wbmsg)
            say("Set the folders first: Tool Types LOCAL=<Amiga directory> and\n"
                "REMOTE=<OneDrive folder> in the icon (Icons > Information...)\n");
        else
            say("Set the folders first: %s LOCAL=<Amiga directory> REMOTE=<OneDrive folder>\n",
                (LONG)AMIDRIVE_NAME);
        rc = RETURN_WARN;
    } else {
        rc = cmd_sync(args[ARG_ONCE] != 0);
    }

    if (plat_break())
        note("*** Break\n");
    appicon_close();
    status_close();
    log_close();
    instance_release();
    sess_close(&login_sess);
    sess_close(&graph_sess);
    sess_close(&dl_sess);
    state_free(&state);
    memset(&tokens, 0, sizeof(tokens));
    net_cleanup();
    if (LocaleBase)
        CloseLibrary((struct Library *)LocaleBase);
    if (rda) {
        FreeArgs(rda);
        if (wbmsg)
            FreeDosObject(DOS_RDARGS, rda);
    }
    if (win) {                          /* WAIT: an opened window stays until closed */
        SelectOutput(oldout);
        Close(win);
    }
    return rc;
}
