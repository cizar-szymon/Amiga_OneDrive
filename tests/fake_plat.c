/* Fake plat.h for the tests: a clock that only moves in plat_sleep(), and a few
 * files kept in memory. */
#include <string.h>

#include "amidrive.h"
#include "plat.h"
#include "tests.h"

#define MAX_FILES 4

static struct {
    char path[64];
    char data[12000];
    long len;
    int  used;
} files[MAX_FILES];

unsigned long fake_time = 1500000000UL;   /* 2025 */
long fake_slept;
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
