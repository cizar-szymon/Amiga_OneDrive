/* plat.h for AmigaOS (dos.library, locale.library). */
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <libraries/locale.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/locale.h>

#include "amidrive.h"
#include "plat_amiga.h"

extern struct LocaleBase *LocaleBase;   /* main.c, may be NULL */

static Tz zone;

void plat_init_timezone(char *source, int size)
{
    char buf[64];

    if (GetVar("TZ", buf, sizeof(buf), 0) > 0 && tz_parse(&zone, buf) == AD_OK) {
        strncpy(source, "TZ=", (size_t)size);
        strncat(source, buf, (size_t)size - 4);
        return;
    }
    tz_fixed(&zone, 0);
    strncpy(source, "UTC (no TZ, no locale)", (size_t)size);
    if (LocaleBase) {
        struct Locale *loc = OpenLocale(NULL);
        if (loc) {
            tz_fixed(&zone, -loc->loc_GMTOffset);   /* locale counts minutes west */
            strncpy(source, "locale prefs (fixed offset, no DST)", (size_t)size);
            CloseLocale(loc);
        }
    }
}

const Tz *plat_timezone(void)
{
    return &zone;
}

unsigned long plat_local_now(void)
{
    struct DateStamp ds;

    DateStamp(&ds);
    return (unsigned long)ds.ds_Days * 86400UL + (unsigned long)ds.ds_Minute * 60UL +
           (unsigned long)(ds.ds_Tick / TICKS_PER_SECOND);
}

unsigned long plat_now(void)
{
    return tz_to_utc(&zone, plat_local_now());
}

int plat_break(void)
{
    return (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) != 0;
}

int plat_sleep(long secs)
{
    /* in steps of 1/5 s so Ctrl-C answers quickly */
    long steps = secs * 5;

    while (steps-- > 0) {
        if (plat_break())
            return AD_ERR_BREAK;
        Delay(TICKS_PER_SECOND / 5);
    }
    return plat_break() ? AD_ERR_BREAK : AD_OK;
}

long plat_read_file(const char *path, char *buf, long size)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    long n;

    if (!fh && strlen(path) + 5 <= 300) {
        char tmp[300];                  /* stopped inside plat_write_file: take the new one */
        strcpy(tmp, path);
        strcat(tmp, ".new");
        fh = Open(tmp, MODE_OLDFILE);
    }
    if (!fh)
        return AD_ERR;
    n = Read(fh, buf, size - 1);
    if (n >= 0 && n == size - 1) {
        char extra;
        if (Read(fh, &extra, 1) > 0)
            n = AD_ERR_TOOBIG;
    }
    Close(fh);
    if (n < 0)
        return n == AD_ERR_TOOBIG ? AD_ERR_TOOBIG : AD_ERR;
    buf[n] = '\0';
    return n;
}

int plat_write_file(const char *path, const char *data, long len)
{
    char tmp[300];
    BPTR fh;
    long n;

    if (strlen(path) + 5 > sizeof(tmp))
        return AD_ERR;
    strcpy(tmp, path);
    strcat(tmp, ".new");

    if (!(fh = Open(tmp, MODE_NEWFILE)))
        return AD_ERR;
    n = Write(fh, (APTR)data, len);
    if (!Close(fh) || n != len) {
        DeleteFile(tmp);
        return AD_ERR;
    }
    /* AmigaDOS cannot rename over an existing file: remove the old one first.
     * If we are stopped in between, "<path>.new" holds the complete new version. */
    DeleteFile((STRPTR)path);
    return Rename(tmp, (STRPTR)path) ? AD_OK : AD_ERR;
}

struct PlatFile {
    BPTR fh;
    int  write, failed;
    char path[300];                     /* the final name; the open file is "<path>.new" */
};

static PlatFile *new_file(const char *path, int write)
{
    PlatFile *f;

    if (strlen(path) + 5 > sizeof(f->path))
        return NULL;
    if (!(f = AllocVec(sizeof(*f), MEMF_ANY | MEMF_CLEAR)))
        return NULL;
    strcpy(f->path, path);
    f->write = write;
    return f;
}

PlatFile *plat_open_read(const char *path)
{
    char tmp[300];
    PlatFile *f = new_file(path, 0);

    if (!f)
        return NULL;
    if (!(f->fh = Open((STRPTR)path, MODE_OLDFILE))) {
        strcpy(tmp, path);
        strcat(tmp, ".new");
        f->fh = Open(tmp, MODE_OLDFILE);
    }
    if (!f->fh) {
        FreeVec(f);
        return NULL;
    }
    return f;
}

PlatFile *plat_open_write(const char *path)
{
    char tmp[300];
    PlatFile *f = new_file(path, 1);

    if (!f)
        return NULL;
    strcpy(tmp, path);
    strcat(tmp, ".new");
    if (!(f->fh = Open(tmp, MODE_NEWFILE))) {
        FreeVec(f);
        return NULL;
    }
    return f;
}

long plat_read(PlatFile *f, void *buf, long len)
{
    long n = Read(f->fh, buf, len);
    return n < 0 ? AD_ERR : n;
}

int plat_write(PlatFile *f, const void *buf, long len)
{
    if (f->failed || Write(f->fh, (APTR)buf, len) != len) {
        f->failed = 1;
        return AD_ERR;
    }
    return AD_OK;
}

int plat_close(PlatFile *f, int commit)
{
    char tmp[300];
    int rc = AD_OK;

    if (!Close(f->fh))
        f->failed = 1;
    if (f->write) {
        strcpy(tmp, f->path);
        strcat(tmp, ".new");
        if (!commit || f->failed) {
            DeleteFile(tmp);
            rc = commit ? AD_ERR : AD_OK;
        } else {
            DeleteFile(f->path);        /* see plat_write_file */
            rc = Rename(tmp, f->path) ? AD_OK : AD_ERR;
        }
    }
    FreeVec(f);
    return rc;
}

int plat_delete_file(const char *path)
{
    return DeleteFile((STRPTR)path) ? AD_OK : AD_ERR;
}

int plat_make_dir(const char *path)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);

    if (!lock)
        lock = CreateDir((STRPTR)path);
    if (!lock)
        return AD_ERR;
    UnLock(lock);
    return AD_OK;
}
