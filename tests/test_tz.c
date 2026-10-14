#include "amidrive.h"
#include "tz.h"
#include "tests.h"

static unsigned long iso(const char *s)
{
    unsigned long t = 0;
    CHECK_INT(tz_parse_iso8601(s, &t), AD_OK);
    return t;
}

void test_tz(void)
{
    char out[24];
    unsigned long t;
    long y;
    int m, d;
    Tz tz;

    /* epoch and calendar */
    CHECK_INT(tz_days_from_civil(1978, 1, 1), 0);
    CHECK_INT(tz_days_from_civil(2000, 3, 1), 8095);
    tz_civil_from_days(17635, &y, &m, &d);          /* 2026-04-14 */
    CHECK(y == 2026 && m == 4 && d == 14);
    for (t = 0; t < 49710; t += 97) {               /* round trip over the whole range */
        tz_civil_from_days((long)t, &y, &m, &d);
        if (tz_days_from_civil(y, m, d) != (long)t) {
            CHECK_INT(tz_days_from_civil(y, m, d), (long)t);
            break;
        }
    }

    /* ISO 8601 */
    CHECK_INT(iso("1978-01-01T00:00:00Z"), 0);
    t = iso("2026-10-13T20:07:32Z");
    tz_format_iso8601(t, out);
    CHECK_STR(out, "2026-10-13T20:07:32Z");
    CHECK_INT(iso("2026-10-13T20:07:32.1234567Z"), (long)t);
    CHECK_INT(iso("2026-10-13T22:07:32+02:00"), (long)t);
    CHECK_INT(iso("2026-10-13T15:07:32-05:00"), (long)t);
    CHECK_INT(iso("2024-02-29T12:00:00Z") % 86400, 43200);
    tz_format_iso8601(0xFFFFFFFFUL, out);
    CHECK_STR(out, "2114-02-07T06:28:15Z");
    CHECK_INT(iso("2114-02-07T06:28:15Z"), (long)0xFFFFFFFFUL);
    CHECK_INT(tz_parse_iso8601("2114-02-07T06:28:16Z", &t), AD_ERR_TOOBIG);
    CHECK_INT(tz_parse_iso8601("1977-12-31T23:59:59Z", &t), AD_ERR_TOOBIG);
    CHECK_INT(tz_parse_iso8601("1978-01-01T00:30:00+01:00", &t), AD_ERR_TOOBIG);
    CHECK_INT(tz_parse_iso8601("2023-02-29T12:00:00Z", &t), AD_ERR_PROTO);

    /* HTTP Date header (clock from the Internet) */
    {
        unsigned long h;
        CHECK_INT(tz_parse_http_date("Wed, 14 Oct 2026 10:15:39 GMT", &h), AD_OK);
        CHECK_INT((long)h, iso("2026-10-14T10:15:39Z"));
        CHECK_INT(tz_parse_http_date("Sun, 01 Jan 1978 00:00:00 GMT", &h), AD_OK);
        CHECK_INT((long)h, 0);
        CHECK_INT(tz_parse_http_date("Thu, 29 Feb 2024 23:59:59 GMT", &h), AD_OK);
        CHECK_INT((long)h, iso("2024-02-29T23:59:59Z"));
        CHECK_INT(tz_parse_http_date("Wed, 14 Oct 2026 10:15:39 UTC", &h), AD_ERR_PROTO);
        CHECK_INT(tz_parse_http_date("Wed, 14 Okt 2026 10:15:39 GMT", &h), AD_ERR_PROTO);
        CHECK_INT(tz_parse_http_date("Wednesday, 14-Oct-26 10:15:39 GMT", &h), AD_ERR_PROTO);
        CHECK_INT(tz_parse_http_date("Fri, 30 Feb 2026 10:15:39 GMT", &h), AD_ERR_PROTO);
        CHECK_INT(tz_parse_http_date("Wed, 14 Oct 1977 10:15:39 GMT", &h), AD_ERR_TOOBIG);
    }
    CHECK_INT(tz_parse_iso8601("2026-10-13 20:07:32Z", &t), AD_ERR_PROTO);
    CHECK_INT(tz_parse_iso8601("2026-10-13T20:07:32", &t), AD_ERR_PROTO);
    CHECK_INT(tz_parse_iso8601("2026-10-13T20:07:32Zjunk", &t), AD_ERR_PROTO);

    /* Central Europe with DST */
    CHECK_INT(tz_parse(&tz, "CET-1CEST,M3.5.0,M10.5.0/3"), AD_OK);
    CHECK_INT(tz_offset_utc(&tz, iso("2026-10-13T20:07:32Z")), 7200);
    CHECK_INT(tz_offset_utc(&tz, iso("2026-12-24T12:00:00Z")), 3600);
    /* 2026: DST from 29 March 01:00 UTC to 25 October 01:00 UTC */
    CHECK_INT(tz_offset_utc(&tz, iso("2026-03-29T00:59:59Z")), 3600);
    CHECK_INT(tz_offset_utc(&tz, iso("2026-03-29T01:00:00Z")), 7200);
    CHECK_INT(tz_offset_utc(&tz, iso("2026-10-25T00:59:59Z")), 7200);
    CHECK_INT(tz_offset_utc(&tz, iso("2026-10-25T01:00:00Z")), 3600);
    t = iso("2026-10-13T20:07:32Z");
    CHECK_INT(tz_to_local(&tz, t), (long)iso("2026-10-13T22:07:32Z"));
    CHECK_INT(tz_to_utc(&tz, tz_to_local(&tz, t)), (long)t);
    CHECK_INT(tz_to_utc(&tz, iso("2026-01-10T10:00:00Z")), (long)iso("2026-01-10T09:00:00Z"));
    /* repeated hour 02:30 on 25 October: daylight interpretation */
    CHECK_INT(tz_to_utc(&tz, iso("2026-10-25T02:30:00Z")), (long)iso("2026-10-25T00:30:00Z"));

    /* other forms */
    CHECK_INT(tz_parse(&tz, "UTC0"), AD_OK);
    CHECK(!tz.has_dst && tz.std_off == 0);
    CHECK_INT(tz_parse(&tz, "<+03>-3"), AD_OK);
    CHECK_INT(tz.std_off, 10800);
    CHECK_INT(tz_parse(&tz, "EST5EDT,M3.2.0,M11.1.0"), AD_OK);
    CHECK_INT(tz_offset_utc(&tz, iso("2026-07-01T12:00:00Z")), -4 * 3600);
    CHECK_INT(tz_offset_utc(&tz, iso("2026-01-01T12:00:00Z")), -5 * 3600);
    CHECK_INT(tz_parse(&tz, "AEST-10AEDT,M10.1.0,M4.1.0/3"), AD_OK);   /* southern */
    CHECK_INT(tz_offset_utc(&tz, iso("2026-01-15T00:00:00Z")), 11 * 3600);
    CHECK_INT(tz_offset_utc(&tz, iso("2026-07-15T00:00:00Z")), 10 * 3600);
    CHECK_INT(tz_parse(&tz, "CET-2BST"), AD_ERR_PROTO);  /* the old Amiga setting: no rules */
    CHECK_INT(tz_parse(&tz, "CET-1CEST,J60,J300"), AD_ERR_PROTO);
    CHECK_INT(tz_parse(&tz, ""), AD_ERR_PROTO);

    tz_fixed(&tz, 60);                              /* locale prefs fallback */
    CHECK_INT(tz_offset_utc(&tz, iso("2026-07-01T12:00:00Z")), 3600);
}
