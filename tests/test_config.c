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
    CHECK_STR(c.upload, "YES");
    CHECK_INT(config_set(&c, "upload", "no"), AD_OK);
    CHECK_STR(c.upload, "NO");
    CHECK_INT(config_set(&c, "UPLOAD", "maybe"), AD_ERR_PROTO);
    CHECK_STR(c.upload, "NO");
    {
        char t[1024];
        Config u;
        CHECK(config_format(&c, t, sizeof(t)) > 0);
        CHECK(strstr(t, "UPLOAD=NO\n") != NULL);
        config_defaults(&u);
        CHECK_INT(config_parse(&u, t), AD_OK);
        CHECK_STR(u.upload, "NO");
    }
    CHECK_INT(config_set(&c, "UPLOAD", "YES"), AD_OK);

    /* PAUSE: NO by default, YES/NO only */
    CHECK_STR(c.pause, "NO");
    CHECK_INT(config_set(&c, "pause", "yes"), AD_OK);
    CHECK_STR(c.pause, "YES");
    CHECK_INT(config_set(&c, "PAUSE", "1"), AD_ERR_PROTO);
    CHECK_STR(c.pause, "YES");
    {
        Config u;
        char t[1024];
        CHECK(config_format(&c, t, sizeof(t)) > 0);
        CHECK(strstr(t, "PAUSE=YES\n") != NULL);
        config_defaults(&u);
        CHECK_INT(config_parse(&u, t), AD_OK);
        CHECK_STR(u.pause, "YES");
    }
    CHECK_INT(config_set(&c, "PAUSE", "NO"), AD_OK);

    /* APPICON: YES by default, YES/NO only, survives format -> parse */
    CHECK_STR(c.appicon, "YES");
    CHECK_INT(config_set(&c, "appicon", "No"), AD_OK);
    CHECK_STR(c.appicon, "NO");
    CHECK_STR(c.upload, "YES");
    CHECK_INT(config_set(&c, "APPICON", "1"), AD_ERR_PROTO);
    {
        char t[1024];
        Config u;
        CHECK(config_format(&c, t, sizeof(t)) > 0);
        CHECK(strstr(t, "APPICON=NO\n") != NULL);
        config_defaults(&u);
        CHECK_INT(config_parse(&u, t), AD_OK);
        CHECK_STR(u.appicon, "NO");
    }
    CHECK_INT(config_set(&c, "APPICON", "YES"), AD_OK);

    /* CLOCK: ASK by default, ASK/AUTO/NO, survives format -> parse */
    CHECK_STR(c.clock, "ASK");
    CHECK_INT(config_set(&c, "clock", "auto"), AD_OK);
    CHECK_STR(c.clock, "AUTO");
    CHECK_INT(config_set(&c, "CLOCK", "YES"), AD_ERR_PROTO);
    CHECK_STR(c.clock, "AUTO");
    {
        char t[1024];
        Config u;
        CHECK(config_format(&c, t, sizeof(t)) > 0);
        config_defaults(&u);
        CHECK_INT(config_parse(&u, t), AD_OK);
        CHECK_STR(u.clock, "AUTO");
    }
    CHECK_INT(config_set(&c, "CLOCK", "NO"), AD_OK);
    CHECK_STR(c.clock, "NO");
    CHECK_INT(config_set(&c, "CLOCK", "ASK"), AD_OK);

    /* accounts: ACCOUNT 1..3, NAME; an account file holds only its own keys */
    CHECK_INT(c.account, 1);
    CHECK_INT(config_set(&c, "ACCOUNT", "2"), AD_OK);
    CHECK_INT(c.account, 2);
    CHECK_INT(config_set(&c, "ACCOUNT", "4"), AD_ERR_PROTO);
    CHECK_INT(config_set(&c, "ACCOUNT", "0"), AD_ERR_PROTO);
    CHECK_INT(config_set(&c, "ACCOUNT", "12"), AD_ERR_PROTO);
    CHECK_INT(c.account, 2);
    CHECK_INT(config_set(&c, "NAME", "Jan Kowalski (personal)"), AD_OK);
    {
        char t[1024];
        Config u;
        CHECK(config_format(&c, t, sizeof(t)) > 0);
        CHECK(strstr(t, "ACCOUNT=2\n") != NULL);
        config_defaults(&u);
        CHECK_INT(config_parse(&u, t), AD_OK);
        CHECK_INT(u.account, 2);
        CHECK_STR(u.name, "Jan Kowalski (personal)");

        CHECK_INT(config_set(&c, "LOCAL", "Work:OneDrive2"), AD_OK);
        CHECK_INT(config_set(&c, "REMOTE", "/Docs"), AD_OK);
        CHECK(config_format_account(&c, t, sizeof(t)) > 0);
        CHECK(strstr(t, "LOCAL=Work:OneDrive2\n") != NULL);
        CHECK(strstr(t, "NAME=Jan Kowalski (personal)\n") != NULL);
        CHECK(strstr(t, "CHARSET") == NULL);        /* not the global settings */
        CHECK(strstr(t, "ACCOUNT") == NULL);
        config_defaults(&u);
        strcpy(u.charset, "LATIN2");
        config_account_clear(&u);
        CHECK_STR(u.local, "");
        CHECK_INT(config_parse(&u, t), AD_OK);
        CHECK_STR(u.local, "Work:OneDrive2");
        CHECK_STR(u.remote, "/Docs");
        CHECK_STR(u.charset, "LATIN2");             /* kept */
    }
    CHECK_INT(config_set(&c, "ACCOUNT", "1"), AD_OK);

    /* START: 0 (the last account used) or 1..3 */
    CHECK_INT(c.start, 0);
    CHECK_INT(config_set(&c, "START", "2"), AD_OK);
    CHECK_INT(c.start, 2);
    CHECK_INT(config_set(&c, "START", "4"), AD_ERR_PROTO);
    CHECK_INT(c.start, 2);
    {
        char t[1024];
        Config u;
        CHECK(config_format(&c, t, sizeof(t)) > 0);
        CHECK(strstr(t, "START=2\n") != NULL);
        config_defaults(&u);
        CHECK_INT(config_parse(&u, t), AD_OK);
        CHECK_INT(u.start, 2);
    }
    CHECK_INT(config_set(&c, "START", "0"), AD_OK);
    CHECK_INT(c.start, 0);
    CHECK_INT(config_set(&c, "NAME", ""), AD_OK);
    CHECK_INT(config_set(&c, "LOCAL", ""), AD_OK);
    CHECK_INT(config_set(&c, "REMOTE", ""), AD_OK);

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
