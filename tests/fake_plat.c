/* Fake plat.h for the tests: a clock that only moves in plat_sleep(), and a small
 * file system in memory. Names are compared without case, like AmigaDOS.
 * plat_write_file() accepts any path; plat_create(), plat_make_dir() and
 * plat_rename() need the parent directory to exist (a volume "X:" always does). */
#include <stdlib.h>
#include <string.h>

#include "amidrive.h"
#include "plat.h"
#include "tests.h"

#define MAX_FILES 48
#define PATH_SIZE 128

static struct {
    char path[PATH_SIZE];
    char *data;
    long len;
    unsigned long date;
    int  used, dir;
} files[MAX_FILES];

unsigned long fake_time = 1500000000UL;   /* 2025 */
long fake_slept;
int fake_write_fail;             /* plat_write() fails after this many more bytes (0 = never) */
int fake_break_in;               /* > 0: the n-th plat_break() from now sees Ctrl-C */

unsigned long plat_now(void)
{
    return fake_time;
}

int plat_sleep(long secs)
{
    fake_time += (unsigned long)secs;
    fake_slept += secs;
    return AD_OK;
}

int plat_break(void)
{
    return fake_break_in > 0 && --fake_break_in == 0;
}

/* ---- the file table ------------------------------------------------------------------ */

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int eq_ci(const char *a, const char *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        if (lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
            return 0;
    return 1;
}

static int find(const char *path)
{
    size_t n = strlen(path);
    int i;

    for (i = 0; i < MAX_FILES; i++)
        if (files[i].used && strlen(files[i].path) == n && eq_ci(files[i].path, path, n))
            return i;
    return -1;
}

/* the parent directory exists (or path is directly on a volume) */
static int parent_ok(const char *path)
{
    char parent[PATH_SIZE];
    const char *slash = strrchr(path, '/');
    int i;

    if (!slash)
        return strchr(path, ':') != NULL;
    if ((size_t)(slash - path) >= sizeof(parent))
        return 0;
    memcpy(parent, path, (size_t)(slash - path));
    parent[slash - path] = '\0';
    i = find(parent);
    return i >= 0 && files[i].dir;
}

/* does a directory contain anything */
static int has_children(const char *dir)
{
    size_t n = strlen(dir);
    int i;

    for (i = 0; i < MAX_FILES; i++)
        if (files[i].used && strlen(files[i].path) > n && files[i].path[n] == '/' &&
            eq_ci(files[i].path, dir, n))
            return 1;
    return 0;
}

static int store(const char *path, const char *data, long len, int dir)
{
    int i = find(path);
    char *copy = NULL;

    if (i >= 0 && files[i].dir != dir)
        return AD_ERR;
    if (i < 0)
        for (i = 0; i < MAX_FILES && files[i].used; i++)
            ;
    if (i >= MAX_FILES || strlen(path) >= PATH_SIZE)
        return AD_ERR;
    if (!dir && !(copy = malloc((size_t)len + 1)))
        return AD_ERR;
    if (copy)
        memcpy(copy, data, (size_t)len);
    free(files[i].data);
    strcpy(files[i].path, path);
    files[i].data = copy;
    files[i].len = len;
    files[i].dir = dir;
    files[i].date = fake_time;
    files[i].used = 1;
    return AD_OK;
}

void fake_fs_reset(void)
{
    int i;

    for (i = 0; i < MAX_FILES; i++) {
        free(files[i].data);
        files[i].data = NULL;
        files[i].used = 0;
    }
}

const char *fake_file(const char *path, long *len)
{
    int i = find(path);

    if (i < 0 || files[i].dir)
        return NULL;
    if (len)
        *len = files[i].len;
    return files[i].data ? files[i].data : "";
}

int fake_is_dir(const char *path)
{
    int i = find(path);
    return i >= 0 && files[i].dir;
}

int fake_file_count(void)
{
    int i, n = 0;

    for (i = 0; i < MAX_FILES; i++)
        n += files[i].used;
    return n;
}

/* ---- whole files ---------------------------------------------------------------------- */

long plat_read_file(const char *path, char *buf, long size)
{
    int i = find(path);

    if (i < 0 || files[i].dir)
        return AD_ERR;
    if (files[i].len >= size)
        return AD_ERR_TOOBIG;
    memcpy(buf, files[i].data, (size_t)files[i].len);
    buf[files[i].len] = '\0';
    return files[i].len;
}

int plat_write_file(const char *path, const char *data, long len)
{
    return store(path, data, len, 0);
}

/* ---- streamed files: one open for reading and one for writing at a time ---------------- */

struct PlatFile {
    int  write, idx;
    long pos;
    char path[PATH_SIZE];
};

static struct PlatFile handles[2];
static char *staging;
static long staging_len, staging_size;
static int staging_failed;

PlatFile *plat_open_read(const char *path)
{
    int i = find(path);

    if (i < 0) {
        char tmp[PATH_SIZE + 8];
        sprintf(tmp, "%.120s.new", path);
        i = find(tmp);
    }
    if (i < 0 || files[i].dir)
        return NULL;
    handles[0].write = 0;
    handles[0].idx = i;
    handles[0].pos = 0;
    return &handles[0];
}

static PlatFile *open_write(const char *path, int mode)
{
    if (strlen(path) >= PATH_SIZE)
        return NULL;
    strcpy(handles[1].path, path);
    handles[1].write = mode;
    staging_len = 0;
    staging_failed = 0;
    return &handles[1];
}

PlatFile *plat_open_write(const char *path)
{
    return open_write(path, 1);
}

PlatFile *plat_create(const char *path)
{
    int i = find(path);

    if (!parent_ok(path) || (i >= 0 && files[i].dir))
        return NULL;
    return open_write(path, 2);
}

long plat_read(PlatFile *f, void *buf, long len)
{
    long n = files[f->idx].len - f->pos;

    if (n > len)
        n = len;
    memcpy(buf, files[f->idx].data + f->pos, (size_t)n);
    f->pos += n;
    return n;
}

int plat_write(PlatFile *f, const void *buf, long len)
{
    (void)f;
    if (fake_write_fail > 0) {
        if (len >= fake_write_fail) {
            fake_write_fail = 0;
            staging_failed = 1;
        } else {
            fake_write_fail -= (int)len;
        }
    }
    if (staging_failed)
        return AD_ERR;
    if (staging_len + len > staging_size) {
        long size = (staging_len + len) * 2 + 1024;
        char *p = realloc(staging, (size_t)size);
        if (!p) {
            staging_failed = 1;
            return AD_ERR;
        }
        staging = p;
        staging_size = size;
    }
    memcpy(staging + staging_len, buf, (size_t)len);
    staging_len += len;
    return AD_OK;
}

int plat_close(PlatFile *f, int commit)
{
    if (!f->write || !commit)
        return AD_OK;
    if (staging_failed)
        return AD_ERR;
    return store(f->path, staging ? staging : "", staging_len, 0);
}

/* ---- names and directories ---------------------------------------------------------- */

int plat_file_info(const char *path, PlatInfo *info)
{
    int i = find(path);

    if (i < 0)
        return AD_ERR;
    info->size = (unsigned long)files[i].len;
    info->date = files[i].date;
    info->dir = files[i].dir;
    return AD_OK;
}

int plat_set_date(const char *path, unsigned long local_secs)
{
    int i = find(path);

    if (i < 0)
        return AD_ERR;
    files[i].date = local_secs;
    return AD_OK;
}

int plat_rename(const char *from, const char *to)
{
    size_t n = strlen(from);
    int i = find(from), j = find(to), k;

    /* a different name of the same object (a change of case) is allowed */
    if (i < 0 || (j >= 0 && j != i) || !parent_ok(to))
        return AD_ERR;
    for (k = 0; k < MAX_FILES; k++) {          /* the directory's contents move along */
        if (k != i && files[k].used && strlen(files[k].path) > n && files[k].path[n] == '/' &&
            eq_ci(files[k].path, from, n)) {
            char moved[PATH_SIZE * 2];
            sprintf(moved, "%s%s", to, files[k].path + n);
            if (strlen(moved) >= PATH_SIZE)
                return AD_ERR;
            strcpy(files[k].path, moved);
        }
    }
    if (strlen(to) >= PATH_SIZE)
        return AD_ERR;
    strcpy(files[i].path, to);
    return AD_OK;
}

int plat_delete_file(const char *path)
{
    int i = find(path);

    if (i < 0 || (files[i].dir && has_children(path)))
        return AD_ERR;
    free(files[i].data);
    files[i].data = NULL;
    files[i].used = 0;
    return AD_OK;
}

int plat_make_dir(const char *path)
{
    int i = find(path);

    if (i >= 0)
        return files[i].dir ? AD_OK : AD_ERR;
    if (!parent_ok(path))
        return AD_ERR;
    return store(path, NULL, 0, 1);
}
