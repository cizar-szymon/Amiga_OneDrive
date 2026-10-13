/* Time: Graph ISO 8601 dates <-> seconds since the Amiga epoch (1978-01-01 00:00 UTC),
 * and time zones with daylight saving from a POSIX TZ string.
 *
 * AmigaOS 3.x locale prefs only hold a fixed GMT offset (no DST), so the TZ variable
 * ("CET-1CEST,M3.5.0,M10.5.0/3") is preferred; the locale offset is the fallback.
 * Unsigned 32-bit seconds cover 1978..2114; no 64-bit arithmetic is needed.
 */
#ifndef TZ_H
#define TZ_H

typedef struct {
    int  month;                 /* 1..12 */
    int  week;                  /* 1..5, 5 = last */
    int  wday;                  /* 0 = Sunday */
    long time;                  /* seconds after local midnight */
} TzRule;

typedef struct {
    long   std_off;             /* seconds to add to UTC for standard time */
    long   dst_off;             /* seconds to add to UTC for daylight time */
    int    has_dst;
    TzRule start, end;          /* DST start (in standard time) and end (in DST) */
} Tz;

/* Fixed offset, e.g. from locale prefs (minutes east of UTC). */
void tz_fixed(Tz *tz, long minutes_east);

/* Parse a POSIX TZ string ("CET-1CEST,M3.5.0,M10.5.0/3", "UTC0", "<+03>-3").
 * Only the M.w.d rule form is supported. Returns AD_OK or AD_ERR_PROTO. */
int tz_parse(Tz *tz, const char *s);

/* Offset (seconds) to add to the UTC time to get local time at that moment. */
long tz_offset_utc(const Tz *tz, unsigned long utc);

/* Convert both ways (seconds since 1978-01-01). In the repeated hour at the end
 * of DST the daylight interpretation wins. */
unsigned long tz_to_local(const Tz *tz, unsigned long utc);
unsigned long tz_to_utc(const Tz *tz, unsigned long local);

/* "2026-10-13T20:07:32Z", "...T20:07:32.123Z", "...T22:07:32+02:00" -> UTC seconds.
 * Returns AD_OK, AD_ERR_PROTO (bad syntax) or AD_ERR_TOOBIG (outside 1978..2114). */
int tz_parse_iso8601(const char *s, unsigned long *utc);

/* UTC seconds -> "YYYY-MM-DDTHH:MM:SSZ" (out needs 21 bytes). */
void tz_format_iso8601(unsigned long utc, char *out);

/* Days since 1978-01-01 <-> civil date. */
long tz_days_from_civil(long y, int m, int d);
void tz_civil_from_days(long days, long *y, int *m, int *d);

#endif
