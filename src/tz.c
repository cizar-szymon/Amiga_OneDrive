/* Dates and time zones (see tz.h). Only 32-bit arithmetic. */
#include <string.h>

#include "amidrive.h"
#include "tz.h"

#define DAY 86400L
#define MAX_DAYS 49710L                 /* last day representable in 32-bit seconds */
#define DAYS_1970_TO_1978 2922L

/* Howard Hinnant's civil-from-days algorithms, shifted to the 1978 epoch. */
long tz_days_from_civil(long y, int m, int d)
{
    long era, yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + doe - 719468L - DAYS_1970_TO_1978;
}

void tz_civil_from_days(long z, long *y, int *m, int *d)
{
    long era, doe, yoe, doy, mp;

    z += 719468L + DAYS_1970_TO_1978;
    era = (z >= 0 ? z : z - 146096L) / 146097L;
    doe = z - era * 146097L;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y += *m <= 2;
}

static int days_in_month(long y, int m)
{
    static const unsigned char dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
        return 29;
    return dm[m - 1];
}

/* ---- ISO 8601 ------------------------------------------------------------------- */

static int digits(const char *s, int n, long *out)
{
    long v = 0;
    int i;

    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return 1;
}

int tz_parse_iso8601(const char *s, unsigned long *utc)
{
    long y, mo, d, h, mi, sec, days, tod, off = 0;
    const char *p;

    if (!digits(s, 4, &y) || s[4] != '-' || !digits(s + 5, 2, &mo) || s[7] != '-' ||
        !digits(s + 8, 2, &d) || (s[10] != 'T' && s[10] != 't') ||
        !digits(s + 11, 2, &h) || s[13] != ':' || !digits(s + 14, 2, &mi) ||
        s[16] != ':' || !digits(s + 17, 2, &sec))
        return AD_ERR_PROTO;
    if (mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, (int)mo) ||
        h > 23 || mi > 59 || sec > 60)
        return AD_ERR_PROTO;
    if (sec == 60)
        sec = 59;                       /* leap second */

    p = s + 19;
    if (*p == '.')                      /* fraction: ignored */
        for (p++; *p >= '0' && *p <= '9'; p++)
            ;
    if (*p == 'Z' || *p == 'z') {
        p++;
    } else if (*p == '+' || *p == '-') {
        long oh, om;
        if (!digits(p + 1, 2, &oh) || p[3] != ':' || !digits(p + 4, 2, &om) || oh > 23 || om > 59)
            return AD_ERR_PROTO;
        off = (oh * 3600 + om * 60) * (*p == '-' ? -1 : 1);
        p += 6;
    } else {
        return AD_ERR_PROTO;            /* Graph always gives the zone */
    }
    if (*p != '\0')
        return AD_ERR_PROTO;

    /* UTC = local - offset, kept as (days, seconds of day) to stay in 32 bits */
    days = tz_days_from_civil(y, (int)mo, (int)d);
    tod = h * 3600 + mi * 60 + sec - off;
    while (tod < 0)    { tod += DAY; days--; }
    while (tod >= DAY) { tod -= DAY; days++; }
    if (days < 0 || days > MAX_DAYS ||
        (days == MAX_DAYS && (unsigned long)tod > 0xFFFFFFFFUL - (unsigned long)MAX_DAYS * DAY))
        return AD_ERR_TOOBIG;
    *utc = (unsigned long)days * (unsigned long)DAY + (unsigned long)tod;
    return AD_OK;
}

static void two(char *o, long v)
{
    o[0] = (char)('0' + v / 10);
    o[1] = (char)('0' + v % 10);
}

int tz_parse_http_date(const char *s, unsigned long *utc)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char iso[24];
    int m;

    /* "Wed, 14 Oct 2026 10:15:39 GMT" (IMF-fixdate, the only form servers send now) */
    if (strlen(s) < 29 || s[3] != ',' || s[4] != ' ' || s[7] != ' ' || s[11] != ' ' ||
        s[16] != ' ' || strncmp(s + 25, " GMT", 4) != 0)
        return AD_ERR_PROTO;
    for (m = 0; m < 12; m++)
        if (strncmp(s + 8, months + m * 3, 3) == 0)
            break;
    if (m == 12)
        return AD_ERR_PROTO;
    memcpy(iso, s + 12, 4);                             /* year */
    iso[4] = '-';
    iso[5] = (char)('0' + (m + 1) / 10);
    iso[6] = (char)('0' + (m + 1) % 10);
    iso[7] = '-';
    memcpy(iso + 8, s + 5, 2);                          /* day */
    iso[10] = 'T';
    memcpy(iso + 11, s + 17, 8);                        /* hh:mm:ss */
    iso[19] = 'Z';
    iso[20] = '\0';
    return tz_parse_iso8601(iso, utc);
}

void tz_format_iso8601(unsigned long utc, char *out)
{
    long days = (long)(utc / DAY), tod = (long)(utc % DAY), y;
    int m, d;

    tz_civil_from_days(days, &y, &m, &d);
    two(out, y / 100);
    two(out + 2, y % 100);
    out[4] = '-';  two(out + 5, m);
    out[7] = '-';  two(out + 8, d);
    out[10] = 'T'; two(out + 11, tod / 3600);
    out[13] = ':'; two(out + 14, tod / 60 % 60);
    out[16] = ':'; two(out + 17, tod % 60);
    out[19] = 'Z';
    out[20] = '\0';
}

/* ---- POSIX TZ ------------------------------------------------------------------- */

void tz_fixed(Tz *tz, long minutes_east)
{
    memset(tz, 0, sizeof(*tz));
    tz->std_off = tz->dst_off = minutes_east * 60;
}

static const char *parse_name(const char *s)
{
    const char *start = s;

    if (*s == '<') {
        while (*s && *s != '>')
            s++;
        return *s == '>' && s - start > 1 ? s + 1 : NULL;
    }
    while ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'))
        s++;
    return s - start >= 3 ? s : NULL;
}

static const char *parse_num(const char *s, long *v)
{
    long n = 0;
    int any = 0;

    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s++ - '0');
        if (n > 1000)
            return NULL;
        any = 1;
    }
    *v = n;
    return any ? s : NULL;
}

/* [+|-]hh[:mm[:ss]] -> seconds */
static const char *parse_time(const char *s, long *secs)
{
    long h, m = 0, sec = 0, sign = 1;

    if (*s == '+' || *s == '-')
        sign = *s++ == '-' ? -1 : 1;
    if (!(s = parse_num(s, &h)) || h > 167)
        return NULL;
    if (*s == ':') {
        if (!(s = parse_num(s + 1, &m)) || m > 59)
            return NULL;
        if (*s == ':' && (!(s = parse_num(s + 1, &sec)) || sec > 59))
            return NULL;
    }
    *secs = sign * (h * 3600 + m * 60 + sec);
    return s;
}

/* ",Mm.w.d[/time]" */
static const char *parse_rule(const char *s, TzRule *r)
{
    long m, w, d;

    if (*s++ != ',' || *s++ != 'M')
        return NULL;
    if (!(s = parse_num(s, &m)) || m < 1 || m > 12 || *s++ != '.' ||
        !(s = parse_num(s, &w)) || w < 1 || w > 5 || *s++ != '.' ||
        !(s = parse_num(s, &d)) || d > 6)
        return NULL;
    r->month = (int)m;
    r->week = (int)w;
    r->wday = (int)d;
    r->time = 7200;
    if (*s == '/' && !(s = parse_time(s + 1, &r->time)))
        return NULL;
    return s;
}

int tz_parse(Tz *tz, const char *s)
{
    long off;
    Tz t;

    memset(&t, 0, sizeof(t));
    if (!s || !(s = parse_name(s)) || !(s = parse_time(s, &off)))
        return AD_ERR_PROTO;
    t.std_off = t.dst_off = -off;               /* POSIX counts west of UTC */
    if (*s == '\0' || *s == '\n' || *s == ' ') {
        *tz = t;
        return AD_OK;
    }
    if (!(s = parse_name(s)))
        return AD_ERR_PROTO;
    t.dst_off = t.std_off + 3600;
    if (*s != ',' && *s) {
        if (!(s = parse_time(s, &off)))
            return AD_ERR_PROTO;
        t.dst_off = -off;
    }
    if (!(s = parse_rule(s, &t.start)) || !(s = parse_rule(s, &t.end)))
        return AD_ERR_PROTO;
    if (*s != '\0' && *s != '\n' && *s != ' ')
        return AD_ERR_PROTO;
    t.has_dst = 1;
    *tz = t;
    return AD_OK;
}

/* Day (since 1978) on which the rule falls in year y. */
static long rule_day(long y, const TzRule *r)
{
    long first = tz_days_from_civil(y, r->month, 1);
    int wd_first = (int)(((first % 7) + 7) % 7);    /* 1978-01-01 was a Sunday */
    int day = 1 + (r->wday - wd_first + 7) % 7 + (r->week - 1) * 7;

    if (day > days_in_month(y, r->month))
        day -= 7;
    return first + day - 1;
}

static int is_dst(const Tz *tz, unsigned long utc)
{
    long days = (long)(utc / DAY), sod = (long)(utc % DAY) + tz->std_off;
    long y, y0, pos, start, end;
    int m, d;

    if (!tz->has_dst)
        return 0;
    while (sod < 0)    { sod += DAY; days--; }
    while (sod >= DAY) { sod -= DAY; days++; }
    tz_civil_from_days(days, &y, &m, &d);
    y0 = tz_days_from_civil(y, 1, 1);
    /* everything in standard local time, seconds since the start of the year */
    pos = (days - y0) * DAY + sod;
    start = (rule_day(y, &tz->start) - y0) * DAY + tz->start.time;
    end = (rule_day(y, &tz->end) - y0) * DAY + tz->end.time - (tz->dst_off - tz->std_off);
    if (start < end)
        return pos >= start && pos < end;
    return pos >= start || pos < end;               /* southern hemisphere */
}

long tz_offset_utc(const Tz *tz, unsigned long utc)
{
    return is_dst(tz, utc) ? tz->dst_off : tz->std_off;
}

static unsigned long add_off(unsigned long t, long off)
{
    if (off < 0)
        return t < (unsigned long)-off ? 0 : t - (unsigned long)-off;
    return t > 0xFFFFFFFFUL - (unsigned long)off ? 0xFFFFFFFFUL : t + (unsigned long)off;
}

unsigned long tz_to_local(const Tz *tz, unsigned long utc)
{
    return add_off(utc, tz_offset_utc(tz, utc));
}

unsigned long tz_to_utc(const Tz *tz, unsigned long local)
{
    unsigned long u = add_off(local, -tz->dst_off);

    if (tz->has_dst && is_dst(tz, u))
        return u;
    return add_off(local, -tz->std_off);
}
