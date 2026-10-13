#include "amidrive.h"
#include "auth.h"
#include "graph.h"
#include "plat.h"
#include "tests.h"

static Session s;
static AuthDevice d;
static AuthTokens t;

static const char device_json[] =
    "{\"user_code\":\"HX4K9QPL\",\"device_code\":\"DAQABAAEAAAB-dev!code\","
    "\"verification_uri\":\"https://microsoft.com/devicelogin\",\"expires_in\":900,\"interval\":5,"
    "\"message\":\"To sign in, use a web browser ...\"}";

static const char *pending(void)
{
    return http_resp(400, NULL, "{\"error\":\"authorization_pending\",\"error_description\":\"AADSTS70016: pending\"}");
}

static const char *token_ok(const char *refresh)
{
    static char json[512];
    sprintf(json, "{\"token_type\":\"Bearer\",\"scope\":\"Files.ReadWrite\",\"expires_in\":3599,"
                  "\"access_token\":\"EwB4A8l6BAAU\\u002bACCESS=\"%s%s%s}",
            refresh ? ",\"refresh_token\":\"" : "", refresh ? refresh : "", refresh ? "\"" : "");
    return http_resp(200, NULL, json);
}

void test_auth(void)
{
    AuthApp app;
    char err[200], buf[300];
    const char *streams[2];
    unsigned long t0;
    DriveInfo di;

    app.client_id = "11111111-2222-3333-4444-555555555555";
    app.tenant = "common";
    streams[1] = NULL;

    CHECK_INT(url_encode("a b&c=d/e~f_g.h-i\xC5\xBC", buf, sizeof(buf)), 31);
    CHECK_STR(buf, "a%20b%26c%3Dd%2Fe~f_g.h-i%C5%BC");
    CHECK_INT(url_encode("abc", buf, 3), AD_ERR_TOOBIG);

    /* device code: request and answer */
    streams[0] = http_resp(200, NULL, device_json);
    fake_server(streams, 9);
    sess_init(&s, AD_LOGIN_HOST);
    CHECK_INT(auth_device_start(&s, &app, &d, err, sizeof(err)), AD_OK);
    CHECK_STR(d.user_code, "HX4K9QPL");
    CHECK_STR(d.device_code, "DAQABAAEAAAB-dev!code");
    CHECK_STR(d.uri, "https://microsoft.com/devicelogin");
    CHECK_INT(d.expires_in, 900);
    CHECK_INT(d.interval, 5);
    CHECK(strstr(fake_sent(s.conn), "POST /common/oauth2/v2.0/devicecode HTTP/1.1\r\n") != NULL);
    CHECK(strstr(fake_sent(s.conn), "Content-Type: application/x-www-form-urlencoded\r\n") != NULL);
    CHECK(strstr(fake_sent(s.conn),
                 "\r\n\r\nclient_id=11111111-2222-3333-4444-555555555555&scope=Files.ReadWrite%20offline_access") != NULL);

    /* polling: pending, slow_down (+5 s), pending, success */
    streams[0] = cat2(cat2(pending(),
                           http_resp(400, NULL, "{\"error\":\"slow_down\"}")),
                      cat2(pending(), token_ok("M.C5_BAY.refresh*1")));
    fake_server(streams, 13);
    sess_close(&s);
    fake_slept = 0;
    t0 = fake_time;
    memset(&t, 0, sizeof(t));
    CHECK_INT(auth_device_wait(&s, &app, &d, &t, err, sizeof(err)), AD_OK);
    CHECK_INT(fake_slept, 5 + 5 + 10 + 10);
    CHECK_STR(t.access, "EwB4A8l6BAAU+ACCESS=");
    CHECK_STR(t.refresh, "M.C5_BAY.refresh*1");
    CHECK(t.expires == fake_time + 3599);
    CHECK_INT(fake_connects, 1);                   /* all polls on one connection */
    CHECK(strstr(fake_sent(s.conn), "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code"
                                    "&client_id=11111111-2222-3333-4444-555555555555"
                                    "&device_code=DAQABAAEAAAB-dev%21code") != NULL);
    CHECK(t0 + 30 == fake_time);

    /* real device codes are longer than 1 KB (1045 characters seen on 14.10.2026) */
    {
        static char code[1501], json[1800];
        static AuthTokens saved;
        int i;
        saved = t;
        for (i = 0; i < 1500; i++)
            code[i] = "ABCDEFGHabcdefgh0123456789-_"[i % 28];
        code[1500] = '\0';
        sprintf(json, "{\"user_code\":\"ABCD1234\",\"device_code\":\"%s\","
                      "\"verification_uri\":\"https://login.microsoft.com/device\","
                      "\"expires_in\":900,\"interval\":5}", code);
        streams[0] = cat2(http_resp(200, NULL, json), token_ok("R2"));
        fake_server(streams, 64);
        sess_close(&s);
        CHECK_INT(auth_device_start(&s, &app, &d, err, sizeof(err)), AD_OK);
        CHECK_INT(strlen(d.device_code), 1500);
        CHECK_STR(d.uri, "https://login.microsoft.com/device");
        CHECK_INT(auth_device_wait(&s, &app, &d, &t, err, sizeof(err)), AD_OK);
        CHECK(strstr(fake_sent(s.conn), code) != NULL);
        CHECK_STR(t.refresh, "R2");
        t = saved;
    }

    /* declined, expired code */
    streams[0] = http_resp(400, NULL, "{\"error\":\"authorization_declined\"}");
    fake_server(streams, 64);
    sess_close(&s);
    CHECK_INT(auth_device_wait(&s, &app, &d, &t, err, sizeof(err)), AD_ERR_AUTH);
    CHECK_STR(err, "sign-in was declined");

    d.expires_in = 12;
    streams[0] = cat2(pending(), pending());
    fake_server(streams, 64);
    sess_close(&s);
    CHECK_INT(auth_device_wait(&s, &app, &d, &t, err, sizeof(err)), AD_ERR_AUTH);
    CHECK_STR(err, "the code has expired, run LOGIN again");

    /* unknown application: the first line of the description is shown */
    streams[0] = http_resp(400, NULL, "{\"error\":\"unauthorized_client\",\"error_description\":"
                                      "\"AADSTS700016: Application not found.\\r\\nTrace ID: 1\"}");
    fake_server(streams, 64);
    sess_close(&s);
    CHECK_INT(auth_device_start(&s, &app, &d, err, sizeof(err)), AD_ERR_HTTP);
    CHECK_STR(err, "sign-in refused: AADSTS700016: Application not found.");

    app.client_id = "";
    CHECK_INT(auth_device_start(&s, &app, &d, err, sizeof(err)), AD_ERR_AUTH);
    app.client_id = "11111111-2222-3333-4444-555555555555";
    app.tenant = "bad/tenant";
    CHECK_INT(auth_device_start(&s, &app, &d, err, sizeof(err)), AD_ERR_PROTO);
    app.tenant = "consumers";

    /* refresh: rotating token, answer without refresh token, invalid_grant */
    streams[0] = cat2(token_ok("M.C5_BAY.refresh*2"), token_ok(NULL));
    fake_server(streams, 64);
    sess_close(&s);
    CHECK_INT(auth_refresh(&s, &app, &t, err, sizeof(err)), AD_OK);
    CHECK_STR(t.refresh, "M.C5_BAY.refresh*2");
    CHECK(strstr(fake_sent(s.conn), "POST /consumers/oauth2/v2.0/token ") != NULL);
    CHECK(strstr(fake_sent(s.conn), "grant_type=refresh_token&client_id=11111111-2222-3333-4444-555555555555"
                                    "&refresh_token=M.C5_BAY.refresh%2A1&scope=") != NULL);
    CHECK_INT(auth_refresh(&s, &app, &t, err, sizeof(err)), AD_OK);
    CHECK_STR(t.refresh, "M.C5_BAY.refresh*2");    /* kept */

    streams[0] = http_resp(400, NULL, "{\"error\":\"invalid_grant\",\"error_description\":\"AADSTS70008: expired\"}");
    fake_server(streams, 64);
    sess_close(&s);
    CHECK_INT(auth_refresh(&s, &app, &t, err, sizeof(err)), AD_ERR_AUTH);
    CHECK_STR(err, "sign-in expired or revoked: run AmiDrive LOGIN");
    CHECK_STR(t.refresh, "M.C5_BAY.refresh*2");    /* a failed refresh changes nothing */

    /* token file */
    CHECK(auth_format(&t, buf, sizeof(buf)) > 0);
    CHECK_INT(auth_save(&t, "ENVARC:AmiDrive/token"), AD_OK);
    {
        AuthTokens u;
        CHECK_INT(auth_load(&u, "ENVARC:AmiDrive/token"), AD_OK);
        CHECK_STR(u.access, t.access);
        CHECK_STR(u.refresh, t.refresh);
        CHECK(u.expires == t.expires);
        CHECK_INT(auth_load(&u, "ENVARC:AmiDrive/none"), AD_ERR_AUTH);
        CHECK_INT(auth_parse(&u, "access=x\n"), AD_ERR_AUTH);
    }

    /* ensure: valid -> no network; expiring -> refresh + save */
    fake_server(NULL, 64);
    sess_close(&s);
    CHECK_INT(auth_ensure(&s, &app, &t, "ENVARC:AmiDrive/token", 0, err, sizeof(err)), AD_OK);
    CHECK_INT(fake_connects, 0);
    fake_time = t.expires - 100;                   /* less than 5 minutes left */
    streams[0] = token_ok("M.C5_BAY.refresh*3");
    fake_server(streams, 64);
    CHECK_INT(auth_ensure(&s, &app, &t, "ENVARC:AmiDrive/token", 0, err, sizeof(err)), AD_OK);
    CHECK_INT(fake_connects, 1);
    {
        AuthTokens u;
        auth_load(&u, "ENVARC:AmiDrive/token");
        CHECK_STR(u.refresh, "M.C5_BAY.refresh*3");
    }
    sess_close(&s);

    /* Graph: drive info with quota beyond 32 bits, 401 */
    streams[0] = cat2(http_resp(200, NULL,
                          "{\"driveType\":\"personal\",\"owner\":{\"user\":{\"displayName\":\"Jan Kowalski\",\"id\":\"1\"}},"
                          "\"quota\":{\"deleted\":0,\"remaining\":4294967296,\"state\":\"normal\","
                          "\"total\":1104880336896,\"used\":7516192768}}"),
                      http_resp(401, NULL, "{\"error\":{\"code\":\"InvalidAuthenticationToken\"}}"));
    fake_server(streams, 50);
    sess_init(&s, AD_GRAPH_HOST);
    CHECK_INT(graph_drive_info(&s, t.access, &di, err, sizeof(err)), AD_OK);
    CHECK_STR(di.owner, "Jan Kowalski");
    CHECK_STR(di.drive_type, "personal");
    CHECK(di.total_mb == 1104880UL);
    CHECK(di.used_mb == 7516UL);
    CHECK(strstr(fake_sent(s.conn), "GET /v1.0/me/drive?$select=driveType,owner,quota HTTP/1.1\r\n") != NULL);
    CHECK(strstr(fake_sent(s.conn), "Authorization: Bearer EwB4A8l6BAAU+ACCESS=\r\n") != NULL);
    CHECK_INT(graph_drive_info(&s, t.access, &di, err, sizeof(err)), AD_ERR_AUTH);
    sess_close(&s);
}
