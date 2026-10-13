#include "amidrive.h"
#include "json.h"
#include "tests.h"

/* A trimmed delta response as returned by Graph. */
static const char delta[] =
    "{\"@odata.context\":\"https://graph.microsoft.com/v1.0/$metadata#Collection(driveItem)\","
    "\"@odata.nextLink\":\"https://graph.microsoft.com/v1.0/me/drive/root/delta?token=abc\","
    "\"value\":["
    "{\"id\":\"01ABC\",\"name\":\"Gra\\u017cyna \\\"x\\\".txt\",\"size\":1234,"
    "\"eTag\":\"\\\"{E1},2\\\"\",\"lastModifiedDateTime\":\"2026-10-13T20:07:32Z\","
    "\"parentReference\":{\"driveId\":\"d1\",\"path\":\"/drive/root:/Amiga\"},"
    "\"file\":{\"mimeType\":\"text/plain\",\"hashes\":{\"sha1Hash\":\"00\"}}},"
    "{\"id\":\"01DEF\",\"name\":\"Docs\",\"size\":0,\"folder\":{\"childCount\":3}},"
    "{\"id\":\"01GHI\",\"name\":\"\\ud83d\\ude00.png\",\"deleted\":{\"state\":\"deleted\"},\"x\":null,"
    "\"big\":5000000000,\"flag\":true}"
    "]}";

void test_json(void)
{
    jsmntok_t tok[128];
    char s[128];
    unsigned long ul;
    int b, val, item, root = 0;
    Json j;

    CHECK_INT(json_parse(&j, delta, strlen(delta), tok, 128), AD_OK);
    CHECK(json_eq(&j, json_get(&j, root, "@odata.nextLink"), "https://graph.microsoft.com/v1.0/me/drive/root/delta?token=abc"));
    CHECK_INT(json_get(&j, root, "@odata.deltaLink"), -1);

    val = json_get(&j, root, "value");
    CHECK(val > 0 && j.tok[val].type == JSMN_ARRAY && j.tok[val].size == 3);

    item = json_elem(&j, val, 0);
    CHECK_INT(json_str(&j, json_get(&j, item, "name"), s, sizeof(s)), 16);
    CHECK_STR(s, "Gra\xC5\xBC" "yna \"x\".txt");                  /* ż as UTF-8 */
    CHECK_INT(json_str(&j, json_get(&j, item, "eTag"), s, sizeof(s)), 8);
    CHECK_STR(s, "\"{E1},2\"");
    CHECK_INT(json_ulong(&j, json_get(&j, item, "size"), &ul), AD_OK);
    CHECK_INT(ul, 1234);
    CHECK(json_eq(&j, json_path(&j, item, "parentReference.path"), "/drive/root:/Amiga"));
    CHECK(json_eq(&j, json_path(&j, item, "file.hashes.sha1Hash"), "00"));
    CHECK_INT(json_path(&j, item, "file.hashes.crc32Hash"), -1);
    CHECK_INT(json_get(&j, item, "folder"), -1);

    /* walking with json_next skips nested objects correctly */
    item = json_next(&j, item);
    CHECK_INT(item, json_elem(&j, val, 1));
    CHECK(json_eq(&j, json_get(&j, item, "name"), "Docs"));
    CHECK(json_get(&j, item, "folder") > 0);

    item = json_next(&j, item);
    CHECK_INT(json_str(&j, json_get(&j, item, "name"), s, sizeof(s)), 8);
    CHECK_STR(s, "\xF0\x9F\x98\x80.png");                         /* surrogate pair */
    CHECK(json_get(&j, item, "deleted") > 0);
    CHECK(json_is_null(&j, json_get(&j, item, "x")));
    CHECK_INT(json_ulong(&j, json_get(&j, item, "big"), &ul), AD_ERR_TOOBIG);
    CHECK_INT(json_bool(&j, json_get(&j, item, "flag"), &b), AD_OK);
    CHECK(b == 1);
    CHECK_INT(json_next(&j, item), j.ntok);
    CHECK_INT(json_elem(&j, val, 3), -1);

    /* small output buffer */
    CHECK_INT(json_str(&j, json_get(&j, root, "@odata.context"), s, 10), AD_ERR_TOOBIG);

    /* errors */
    CHECK_INT(json_parse(&j, delta, strlen(delta), tok, 10), AD_ERR_TOOBIG);
    CHECK_INT(json_parse(&j, "{\"a\":", 5, tok, 128), AD_ERR_PROTO);
    CHECK_INT(json_parse(&j, "{\"a\":\"\\ud83d\"}", 14, tok, 128), AD_OK);
    CHECK_INT(json_str(&j, 2, s, sizeof(s)), AD_ERR_PROTO);      /* lone surrogate */
}
