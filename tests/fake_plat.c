/* Fake plat.h for the tests: a clock that only moves in plat_sleep(), and a few
 * files kept in memory. */
#include <string.h>

#include "amidrive.h"
#include "plat.h"
#include "tests.h"

#define MAX_FILES 6
#define FILE_SIZE 40000

static struct {
    char path[64];
    char data[FILE_SIZE];
    long len;
    int  used;
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

static int find(const char *path)
{
    int i;

    for (i = 0; i < MAX_FILES; i++)
        if (files[i].used && strcmp(files[i].path, path) == 0)
            return i;
    return -1;
}

long plat_read_file(const char *path, char *buf, long size)
{
    int i = find(path);

    if (i < 0)
        return AD_ERR;
    if (files[i].len >= size)
        return AD_ERR_TOOBIG;
    memcpy(buf, files[i].data, (size_t)files[i].len);
    buf[files[i].len] = '\0';
    return files[i].len;
}

int plat_write_file(const char *path, const char *data, long len)
{
    int i = find(path);

    if (i < 0)
        for (i = 0; i < MAX_FILES && files[i].used; i++)
            ;
    if (i >= MAX_FILES || len >= (long)sizeof(files[i].data) || strlen(path) >= sizeof(files[i].path))
        return AD_ERR;
    strcpy(files[i].path, path);
    memcpy(files[i].data, data, (size_t)len);
    files[i].len = len;
    files[i].used = 1;
    return AD_OK;
}

/* streamed files: one file open for writing at a time, kept in "staging" */
struct PlatFile {
    int  write, idx;
    long pos;
    char path[64];
};

static struct PlatFile handles[2];
static char staging[FILE_SIZE];
static long staging_len;
static int staging_failed;

PlatFile *plat_open_read(const char *path)
{
    int i = find(path);

    if (i < 0) {
        char tmp[80];
        sprintf(tmp, "%.70s.new", path);
        i = find(tmp);
    }
    if (i < 0)
        return NULL;
    handles[0].write = 0;
    handles[0].idx = i;
    handles[0].pos = 0;
    return &handles[0];
}

PlatFile *plat_open_write(const char *path)
{
    if (strlen(path) >= sizeof(handles[1].path))
        return NULL;
    strcpy(handles[1].path, path);
    handles[1].write = 1;
    staging_len = 0;
    staging_failed = 0;
    return &handles[1];
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
    if (staging_failed || staging_len + len > FILE_SIZE) {
        staging_failed = 1;
        return AD_ERR;
    }
    memcpy(staging + staging_len, buf, (size_t)len);
    staging_len += len;
    return AD_OK;
}

int plat_close(PlatFile *f, int commit)
{
    if (!f->write)
        return AD_OK;
    if (!commit)
        return AD_OK;
    if (staging_failed)
        return AD_ERR;
    return plat_write_file(f->path, staging, staging_len);
}

int plat_delete_file(const char *path)
{
    int i = find(path);

    if (i < 0)
        return AD_ERR;
    files[i].used = 0;
    return AD_OK;
}

int plat_make_dir(const char *path)
{
    (void)path;
    return AD_OK;
}
