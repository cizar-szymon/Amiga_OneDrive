/* Services the portable modules need from the system: time, waiting, files.
 * Amiga: plat_amiga.c. PC tests: tests/fake_plat.c. */
#ifndef PLAT_H
#define PLAT_H

/* Current time, UTC seconds since 1978-01-01. */
unsigned long plat_now(void);

/* Wait secs seconds. Returns AD_OK, or AD_ERR_BREAK when Ctrl-C was pressed. */
int plat_sleep(long secs);

/* Ctrl-C pressed since the last check? */
int plat_break(void);

/* Read a whole file into buf (zero terminated). When path is missing, "<path>.new"
 * (left by an interrupted plat_write_file) is read instead. Returns the length,
 * AD_ERR (missing or unreadable) or AD_ERR_TOOBIG. */
long plat_read_file(const char *path, char *buf, long size);

/* Replace a file safely: write "<path>.new", then swap it in. Returns AD_OK or AD_ERR. */
int plat_write_file(const char *path, const char *data, long len);

/* Streamed files, for data too large for one buffer (the sync state).
 * plat_open_read() falls back to "<path>.new" like plat_read_file(); NULL when missing.
 * plat_open_write() writes "<path>.new"; plat_close(f, 1) swaps it in like
 * plat_write_file(), plat_close(f, 0) throws it away. */
typedef struct PlatFile PlatFile;

PlatFile *plat_open_read(const char *path);
PlatFile *plat_open_write(const char *path);
long plat_read(PlatFile *f, void *buf, long len);       /* bytes, 0 at the end, AD_ERR */
int  plat_write(PlatFile *f, const void *buf, long len); /* AD_OK or AD_ERR */
int  plat_close(PlatFile *f, int commit);                /* AD_OK or AD_ERR */

/* Create (replace) path and write it directly, without "<path>.new" - for a
 * temporary file. plat_close(f, 0) deletes it. */
PlatFile *plat_create(const char *path);

typedef struct {
    unsigned long size;
    unsigned long date;          /* local seconds since 1978-01-01 */
    int dir;
} PlatInfo;

/* Size, date and kind of a file or directory. Returns AD_OK or AD_ERR (missing). */
int plat_file_info(const char *path, PlatInfo *info);

/* Set the date of a file (local seconds since 1978). Returns AD_OK or AD_ERR. */
int plat_set_date(const char *path, unsigned long local_secs);

/* Rename / move on the same volume; fails when to exists. Returns AD_OK or AD_ERR. */
int plat_rename(const char *from, const char *to);

/* Deletes a file or an empty directory. Returns AD_OK or AD_ERR. */
int plat_delete_file(const char *path);

/* Create a directory if it does not exist yet. Returns AD_OK or AD_ERR. */
int plat_make_dir(const char *path);

#endif
