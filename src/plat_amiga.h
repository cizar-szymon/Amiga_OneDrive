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

#endif
