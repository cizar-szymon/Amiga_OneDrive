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
#include "fsmap.h"
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

void (*plat_idle_hook)(void);

int plat_sleep(long secs)
{
    /* in steps of 1/5 s so Ctrl-C answers quickly */
    long steps = secs * 5;

    while (steps-- > 0) {
        if (plat_idle_hook)
            plat_idle_hook();           /* the window and the icon answer meanwhile */
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

PlatFile *plat_create(const char *path)
{
    PlatFile *f = new_file(path, 2);

    if (!f)
        return NULL;
    if (!(f->fh = Open((STRPTR)path, MODE_NEWFILE))) {
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
    if (f->write == 2) {                /* plat_create: written in place */
        if (!commit || f->failed) {
            DeleteFile(f->path);
            rc = commit ? AD_ERR : AD_OK;
        }
    } else if (f->write) {
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

int plat_file_info(const char *path, PlatInfo *info)
{
    struct FileInfoBlock *fib;
    BPTR lock;
    int rc = AD_ERR;

    if (!(lock = Lock((STRPTR)path, ACCESS_READ)))
        return AD_ERR;
    if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib)) {
            info->size = (unsigned long)fib->fib_Size;
            info->date = (unsigned long)fib->fib_Date.ds_Days * 86400UL +
                         (unsigned long)fib->fib_Date.ds_Minute * 60UL +
                         (unsigned long)(fib->fib_Date.ds_Tick / TICKS_PER_SECOND);
            info->dir = fib->fib_DirEntryType > 0;
            rc = AD_OK;
        }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return rc;
}

struct PlatDir {
    BPTR lock;
    struct FileInfoBlock *fib;
};

PlatDir *plat_dir_open(const char *path)
{
    PlatDir *d = AllocVec(sizeof(*d), MEMF_ANY | MEMF_CLEAR);

    if (!d)
        return NULL;
    if ((d->lock = Lock((STRPTR)path, ACCESS_READ)) != 0 &&
        (d->fib = AllocDosObject(DOS_FIB, NULL)) != NULL &&
        Examine(d->lock, d->fib) && d->fib->fib_DirEntryType > 0)
        return d;
    plat_dir_close(d);
    return NULL;
}

int plat_dir_next(PlatDir *d, char *name, int size, PlatInfo *info)
{
    for (;;) {
        LONG type;

        if (!ExNext(d->lock, d->fib))
            return IoErr() == ERROR_NO_MORE_ENTRIES ? 0 : AD_ERR;
        type = d->fib->fib_DirEntryType;
        if (type == ST_SOFTLINK)
            continue;
        if ((int)strlen(d->fib->fib_FileName) >= size)
            return AD_ERR;
        strcpy(name, d->fib->fib_FileName);
        info->size = (unsigned long)d->fib->fib_Size;
        info->date = (unsigned long)d->fib->fib_Date.ds_Days * 86400UL +
                     (unsigned long)d->fib->fib_Date.ds_Minute * 60UL +
                     (unsigned long)(d->fib->fib_Date.ds_Tick / TICKS_PER_SECOND);
        info->dir = type > 0;
        return 1;
    }
}

void plat_dir_close(PlatDir *d)
{
    if (!d)
        return;
    if (d->fib)
        FreeDosObject(DOS_FIB, d->fib);
    if (d->lock)
        UnLock(d->lock);
    FreeVec(d);
}

int plat_set_date(const char *path, unsigned long local_secs)
{
    struct DateStamp ds;

    ds.ds_Days = (LONG)(local_secs / 86400UL);
    ds.ds_Minute = (LONG)((local_secs % 86400UL) / 60UL);
    ds.ds_Tick = (LONG)(local_secs % 60UL) * TICKS_PER_SECOND;
    return SetFileDate((STRPTR)path, &ds) ? AD_OK : AD_ERR;
}

int plat_rename(const char *from, const char *to)
{
    return Rename((STRPTR)from, (STRPTR)to) ? AD_OK : AD_ERR;
}

int plat_name_limit(const char *path)
{
    struct InfoData *id;
    BPTR lock;
    int limit = FS_MAXNAME_LONG;

    if (!(lock = Lock((STRPTR)path, ACCESS_READ)))
        return FS_MAXNAME_OFS;
    if ((id = AllocVec(sizeof(*id), MEMF_ANY)) != NULL) {
        if (Info(lock, id)) {
            ULONG t = (ULONG)id->id_DiskType;
            /* OFS / FFS ("DOS\0".."DOS\5"): 30 characters; FFS2 ("DOS\6", "DOS\7"),
             * SFS, PFS and the rest: long names */
            if ((t & 0xFFFFFF00UL) == 0x444F5300UL && (t & 0xFF) < 6)
                limit = FS_MAXNAME_OFS;
        }
        FreeVec(id);
    }
    UnLock(lock);
    return limit;
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
