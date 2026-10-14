/* Amiga side of plat.h, plus the time zone used for local <-> UTC. */
#ifndef PLAT_AMIGA_H
#define PLAT_AMIGA_H

#include "plat.h"
#include "tz.h"

/* Read the time zone: the TZ variable (with DST rules) first, else the fixed
 * locale offset. source gets a description for messages. Call once at start. */
void plat_init_timezone(char *source, int size);

const Tz *plat_timezone(void);

/* Local time now, seconds since 1978-01-01 (DateStamp). */
unsigned long plat_local_now(void);

/* Called about 5 times a second while plat_sleep waits (sign-in, retries): the user
 * interface answers meanwhile. NULL = nothing. */
extern void (*plat_idle_hook)(void);

/* Longest file name on the volume of path: 30 for OFS/FFS, 107 for FFS2, SFS, PFS. */
int plat_name_limit(const char *path);

#endif
