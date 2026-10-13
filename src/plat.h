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

int plat_delete_file(const char *path);

/* Create a directory if it does not exist yet. Returns AD_OK or AD_ERR. */
int plat_make_dir(const char *path);

#endif
