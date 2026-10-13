/* Microsoft Graph calls (see graph.h). */
#include <string.h>

#include "amidrive.h"
#include "auth.h"
#include "graph.h"
#include "json.h"

static char headers[AUTH_TOKEN_SIZE + 128];
static char resp[8192];
static jsmntok_t tok[128];

static void set_err(char *err, int errsize, const char *a, const char *b)
{
    int n = 0;

    if (errsize <= 0)
        return;
    while (*a && n < errsize - 1)
        err[n++] = *a++;
    if (b && *b && n < errsize - 3) {
        err[n++] = ':';
        err[n++] = ' ';
        while (*b && n < errsize - 1)
            err[n++] = *b++;
    }
    err[n] = '\0';
}

/* GET path with the token; the JSON answer is parsed into j. Returns AD_OK or AD_ERR_*. */
static int get_json(Session *g, const char *token, const char *path, Json *j, char *err, int errsize)
{
    long n;
    int rc;

    if (strlen(token) + 64 > sizeof(headers)) {
        set_err(err, errsize, "access token too long", NULL);
        return AD_ERR_TOOBIG;
    }
    strcpy(headers, "Authorization: Bearer ");
    strcat(headers, token);
    strcat(headers, "\r\nAccept: application/json\r\n");

    if ((rc = sess_request(g, "GET", path, headers, NULL, 0)) != AD_OK) {
        set_err(err, errsize, "cannot reach " AD_GRAPH_HOST, g->err);
        return rc;
    }
    n = sess_read_all(g, resp, (long)sizeof(resp));
    if (g->http.status == 401) {
        set_err(err, errsize, "access token rejected", NULL);
        return AD_ERR_AUTH;
    }
    if (n < 0) {
        set_err(err, errsize, "cannot read the answer", n == AD_ERR_TOOBIG ? "too long" : g->err);
        return (int)n;
    }
    if (g->http.status != 200) {
        static char msg[160];
        msg[0] = '\0';
        if (json_parse(j, resp, (size_t)n, tok, sizeof(tok) / sizeof(tok[0])) == AD_OK)
            json_str(j, json_path(j, 0, "error.message"), msg, sizeof(msg));
        set_err(err, errsize, g->http.status == 429 || g->http.status == 503 ?
                "OneDrive busy, try again later" : "OneDrive refused the request", msg);
        return AD_ERR_HTTP;
    }
    if (json_parse(j, resp, (size_t)n, tok, sizeof(tok) / sizeof(tok[0])) != AD_OK ||
        j->tok[0].type != JSMN_OBJECT) {
        set_err(err, errsize, "unexpected answer from " AD_GRAPH_HOST, NULL);
        return AD_ERR_PROTO;
    }
    return AD_OK;
}

int graph_drive_info(Session *g, const char *access_token, DriveInfo *di, char *err, int errsize)
{
    Json j;
    int rc;

    memset(di, 0, sizeof(*di));
    rc = get_json(g, access_token,
                  "/v1.0/me/drive?$select=driveType,owner,quota", &j, err, errsize);
    if (rc != AD_OK)
        return rc;
    if (json_str(&j, json_path(&j, 0, "owner.user.displayName"), di->owner, sizeof(di->owner)) < 0)
        di->owner[0] = '\0';
    if (json_str(&j, json_get(&j, 0, "driveType"), di->drive_type, sizeof(di->drive_type)) < 0)
        di->drive_type[0] = '\0';
    json_ulong_cut(&j, json_path(&j, 0, "quota.total"), 6, &di->total_mb);
    json_ulong_cut(&j, json_path(&j, 0, "quota.used"), 6, &di->used_mb);
    return AD_OK;
}
