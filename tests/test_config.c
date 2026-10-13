#include "amidrive.h"
#include "config.h"
#include "tests.h"

void test_config(void)
{
    char buf[1024], v[32];
    Config c, d;

    config_defaults(&c);
    CHECK_STR(c.charset, "LATIN1");
    CHECK_INT(c.interval, 3);
    CHECK_STR(c.tenant, "common");
    CHECK_STR(c.remote, "");

    /* kv_get: case, spaces, comments, '=' in the value */
    CHECK_INT(kv_get("# c\n  Key = x \nkey=a=b \r\n", "KEY", v, sizeof(v)), 1);
    CHECK_STR(v, "x");
    CHECK_INT(kv_get("# c\nkey=a=b \r\n", "KEY", v, sizeof(v)), 3);
    CHECK_STR(v, "a=b");
    CHECK_INT(kv_get("#key=1\n", "key", v, sizeof(v)), AD_ERR);
    CHECK_INT(kv_get("keys=1\nkey=2", "key", v, sizeof(v)), 1);
    CHECK_STR(v, "2");
    CHECK_INT(kv_get("key=123456", "key", v, 4), AD_ERR_TOOBIG);
    CHECK_INT(kv_get("key=", "key", v, sizeof(v)), 0);

    /* parse */
    CHECK_INT(config_parse(&c, "LOCAL=Work:OneDrive\nremote=Amiga/\ncharset=latin2\nINTERVAL=10\n"), AD_OK);
    CHECK_STR(c.local, "Work:OneDrive");
    CHECK_STR(c.remote, "/Amiga");
    CHECK_STR(c.charset, "LATIN2");
    CHECK_INT(c.interval, 10);

    CHECK_INT(config_parse(&c, "INTERVAL=0\nCHARSET=UTF8\n"), AD_ERR_PROTO);
    CHECK_INT(c.interval, 10);                 /* invalid values leave the old ones */
    CHECK_STR(c.charset, "LATIN2");

    /* REMOTE forms; empty never means the whole drive */
    CHECK_INT(config_set(&c, "REMOTE", "/"), AD_OK);
    CHECK_STR(c.remote, "/");
    CHECK_INT(config_set(&c, "REMOTE", "/Docs/Amiga//"), AD_OK);
    CHECK_STR(c.remote, "/Docs/Amiga");
    CHECK_INT(config_set(&c, "REMOTE", ""), AD_OK);
    CHECK_STR(c.remote, "");
    CHECK_INT(config_set(&c, "NOPE", "1"), AD_ERR);

    /* empty CLIENTID / TENANT keep the defaults */
    CHECK_INT(config_set(&c, "CLIENTID", "abc-123"), AD_OK);
    CHECK_INT(config_set(&c, "CLIENTID", ""), AD_OK);
    CHECK_STR(c.client_id, "abc-123");

    /* format -> parse round trip */
    config_set(&c, "REMOTE", "/Amiga");
    config_set(&c, "STATE", "Work:State");
    CHECK(config_format(&c, buf, sizeof(buf)) > 0);
    config_defaults(&d);
    CHECK_INT(config_parse(&d, buf), AD_OK);
    CHECK_STR(d.local, c.local);
    CHECK_STR(d.remote, c.remote);
    CHECK_STR(d.state, c.state);
    CHECK_STR(d.charset, c.charset);
    CHECK_INT(d.interval, c.interval);
    CHECK_STR(d.client_id, c.client_id);
    CHECK_STR(d.tenant, c.tenant);
    CHECK_INT(config_format(&c, buf, 40), AD_ERR_TOOBIG);
}
