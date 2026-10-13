/* Microsoft Graph calls (see graph.h). */
#include <stdio.h>
#include <string.h>

#include "amidrive.h"
#include "auth.h"
#include "graph.h"
#include "json.h"
#include "plat.h"
#include "tz.h"

#define GRAPH_RETRIES  5         /* attempts while the server is busy */
#define RETRY_MAX_WAIT 600       /* seconds; longer Retry-After values are cut */
#define MAX_REDIRECTS  5

/* Fields of an item. Without $select a delta page is several times larger. */
#define ITEM_SELECT "id,name,size,eTag,cTag,lastModifiedDateTime,fileSystemInfo," \
                    "parentReference,file,folder,deleted,root,package,remoteItem"
/* Items per page: one page is held in memory (resp, tok) as a whole. An item takes
 * ~0.7 KB of JSON and ~45 tokens, long escaped names up to ~2 KB. */
#define PAGE_TOP    "20"

static char headers[AUTH_TOKEN_SIZE + 512];
static char resp[49152];
static jsmntok_t tok[2048];
static char next_link[GRAPH_LINK_SIZE];
static char location[HTTP_LOCATION_SIZE];
static char dlbuf[8192];
static GraphItem item;

static void set_err(char *err, int errsize, const char *a, const char *b, const char *c)
{
    int n = 0;

    if (errsize <= 0)
        return;
    while (*a && n < errsize - 1)
        err[n++] = *a++;
    while (b && *b && n < errsize - 1)
        err[n++] = *b++;
    if (c && *c && n < errsize - 3) {
        err[n++] = ':';
        err[n++] = ' ';
        while (*c && n < errsize - 1)
            err[n++] = *c++;
    }
    err[n] = '\0';
}

static int busy(int status)
{
    return status == 429 || status == 500 || status == 502 || status == 503 || status == 504;
}

static int redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

long graph_retry_wait(long retry_after, int attempt)
{
    long w;

    if (retry_after >= 0)
        w = retry_after < 1 ? 1 : retry_after;
    else
        w = 2L << (attempt < 8 ? attempt : 8);     /* 2, 4, 8, ... s */
    return w > RETRY_MAX_WAIT ? RETRY_MAX_WAIT : w;
}

static void skip_body(Session *s)
{
    http_skip_body(&s->http);
    sess_done(s);
}

/* GET path and read the response headers; while the server is busy, wait and send
 * the request again. On AD_OK the body is still to be read (any status). */
static int send_retry(Session *s, const char *method, const char *path, const char *hdrs,
                      const void *body, unsigned long body_len, char *err, int errsize)
{
    int attempt, rc;

    for (attempt = 0; ; attempt++) {
        if ((rc = sess_request(s, method, path, hdrs, body, body_len)) != AD_OK) {
            set_err(err, errsize, "cannot reach ", s->host, s->err);
            return rc;
        }
        if (!busy(s->http.status) || attempt + 1 >= GRAPH_RETRIES)
            return AD_OK;
        skip_body(s);
        if (plat_sleep(graph_retry_wait(s->http.retry_after, attempt)) != AD_OK) {
            set_err(err, errsize, "interrupted", NULL, NULL);
            return AD_ERR_BREAK;
        }
    }
}

static int get_retry(Session *s, const char *path, const char *hdrs, char *err, int errsize)
{
    return send_retry(s, "GET", path, hdrs, NULL, 0, err, errsize);
}

static int auth_headers(const char *token, char *err, int errsize)
{
    if (strlen(token) + 64 > sizeof(headers)) {
        set_err(err, errsize, "access token too long", NULL, NULL);
        return AD_ERR_TOOBIG;
    }
    strcpy(headers, "Authorization: Bearer ");
    strcat(headers, token);
    strcat(headers, "\r\nAccept: application/json\r\n");
    return AD_OK;
}

/* Error status -> AD_ERR_* with a message (body already read or skipped). */
static int status_error(int status, const char *what, const char *msg, char *err, int errsize)
{
    if (status == 401) {
        set_err(err, errsize, "access token rejected", NULL, NULL);
        return AD_ERR_AUTH;
    }
    if (status == 404) {
        set_err(err, errsize, what, " not found on OneDrive", msg);
        return AD_ERR_NOTFOUND;
    }
    if (status == 410) {
        set_err(err, errsize, "list of changes expired, reading the whole folder again", NULL, msg);
        return AD_ERR_GONE;
    }
    if (status == 409 || status == 412) {
        set_err(err, errsize, status == 409 ? "the name is taken on OneDrive" :
                "changed on OneDrive meanwhile", NULL, msg);
        return AD_ERR_CONFLICT;
    }
    set_err(err, errsize, busy(status) ? "OneDrive busy, try again later" :
            "OneDrive refused the request", NULL, msg);
    return AD_ERR_HTTP;
}

/* Read the answer whose headers have arrived. 200, 201 and 202 are parsed into j
 * (a JSON object), 204 gives AD_OK with j->ntok = 0. Returns AD_OK or AD_ERR_*. */
static int answer_json(Session *g, const char *what, Json *j, char *err, int errsize)
{
    int status = g->http.status, rc;
    long n;

    n = sess_read_all(g, resp, (long)sizeof(resp));
    if (status == 401)
        return status_error(401, what, NULL, err, errsize);
    if (n < 0) {
        set_err(err, errsize, "cannot read the answer", NULL, n == AD_ERR_TOOBIG ? "too long" : g->err);
        return (int)n;
    }
    if (status == 204) {
        j->ntok = 0;
        return AD_OK;
    }
    if (status != 200 && status != 201 && status != 202) {
        static char msg[160];
        msg[0] = '\0';
        if (json_parse(j, resp, (size_t)n, tok, sizeof(tok) / sizeof(tok[0])) == AD_OK)
            json_str(j, json_path(j, 0, "error.message"), msg, sizeof(msg));
        return status_error(status, what, msg, err, errsize);
    }
    rc = json_parse(j, resp, (size_t)n, tok, sizeof(tok) / sizeof(tok[0]));
    if (rc == AD_ERR_TOOBIG) {
        set_err(err, errsize, "answer too large from ", g->host, NULL);
        return rc;
    }
    if (rc != AD_OK || j->tok[0].type != JSMN_OBJECT) {
        set_err(err, errsize, "unexpected answer from ", g->host, NULL);
        return AD_ERR_PROTO;
    }
    return AD_OK;
}

/* method path with the token, extra headers and a JSON body (or none); the JSON answer
 * is parsed into j (see answer_json). Returns AD_OK or AD_ERR_*. */
static int call(Session *g, const char *token, const char *method, const char *path,
                const char *extra, const char *body, const char *what, Json *j,
                char *err, int errsize)
{
    int rc;

    if ((rc = auth_headers(token, err, errsize)) != AD_OK)
        return rc;
    if (strlen(headers) + (extra ? strlen(extra) : 0) + 40 >= sizeof(headers)) {
        set_err(err, errsize, "request headers too long", NULL, NULL);
        return AD_ERR_TOOBIG;
    }
    if (extra)
        strcat(headers, extra);
    if (body)
        strcat(headers, "Content-Type: application/json\r\n");
    rc = send_retry(g, method, path, headers, body, body ? (unsigned long)strlen(body) : 0,
                    err, errsize);
    return rc != AD_OK ? rc : answer_json(g, what, j, err, errsize);
}

static int get_json(Session *g, const char *token, const char *path, const char *what, Json *j,
                    char *err, int errsize)
{
    return call(g, token, "GET", path, NULL, NULL, what, j, err, errsize);
}

int graph_drive_info(Session *g, const char *access_token, DriveInfo *di, char *err, int errsize)
{
    Json j;
    int rc;

    memset(di, 0, sizeof(*di));
    rc = get_json(g, access_token, "/v1.0/me/drive?$select=driveType,owner,quota", "drive",
                  &j, err, errsize);
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

/* ---- items -------------------------------------------------------------------------- */

static int has(const Json *j, int obj, const char *key)
{
    int t = json_get(j, obj, key);
    return t >= 0 && !json_is_null(j, t);
}

/* Item object at token t -> it. Returns AD_OK, or AD_ERR_PROTO when it has no id. */
static int parse_item(const Json *j, int t, GraphItem *it)
{
    char date[40];
    int v, rc;

    memset(it, 0, sizeof(*it));
    if (json_str(j, json_get(j, t, "id"), it->id, sizeof(it->id)) <= 0)
        return AD_ERR_PROTO;
    if (json_str(j, json_path(j, t, "parentReference.id"), it->parent_id, sizeof(it->parent_id)) < 0)
        it->parent_id[0] = '\0';
    rc = json_str(j, json_get(j, t, "name"), it->name, sizeof(it->name));
    if (rc == AD_ERR_TOOBIG)
        it->flags |= GI_NAMECUT;
    if (rc < 0)
        it->name[0] = '\0';
    if (json_str(j, json_get(j, t, "eTag"), it->etag, sizeof(it->etag)) < 0)
        it->etag[0] = '\0';
    if (json_str(j, json_get(j, t, "cTag"), it->ctag, sizeof(it->ctag)) < 0)
        it->ctag[0] = '\0';
    if ((v = json_get(j, t, "size")) >= 0 && json_ulong(j, v, &it->size) == AD_ERR_TOOBIG) {
        it->flags |= GI_TOOBIG;
        it->size = 0;
    }
    if ((v = json_path(j, t, "fileSystemInfo.lastModifiedDateTime")) < 0)
        v = json_get(j, t, "lastModifiedDateTime");
    if (json_str(j, v, date, sizeof(date)) <= 0 || tz_parse_iso8601(date, &it->mtime) != AD_OK)
        it->mtime = 0;

    if (has(j, t, "file"))
        it->flags |= GI_FILE;
    if (has(j, t, "folder"))
        it->flags |= GI_FOLDER;
    if (has(j, t, "deleted"))
        it->flags |= GI_DELETED;
    if (has(j, t, "root"))
        it->flags |= GI_ROOT;
    if (has(j, t, "package"))
        it->flags |= GI_PACKAGE;
    if (has(j, t, "remoteItem"))
        it->flags |= GI_REMOTE;
    return AD_OK;
}

/* "/v1.0/me/drive/root" or "/v1.0/me/drive/root:/a/b" (+ ":" with colon set) for a
 * remote folder path, encoded for the URL. */
static int root_path(const char *remote, int colon, char *out, int size)
{
    static char plain[GRAPH_LINK_SIZE];
    static const char base[] = "/v1.0/me/drive/root";
    char *p, *q;
    int len;

    while (*remote == '/')
        remote++;
    len = (int)strlen(remote);
    while (len > 0 && remote[len - 1] == '/')
        len--;
    if (len >= (int)sizeof(plain) || (int)sizeof(base) + 3 > size)
        return AD_ERR_TOOBIG;
    strcpy(out, base);
    if (len == 0)
        return AD_OK;
    memcpy(plain, remote, (size_t)len);
    plain[len] = '\0';
    strcat(out, ":/");
    p = out + strlen(out);
    if (url_encode(plain, p, (size_t)(size - (p - out) - 2)) < 0)
        return AD_ERR_TOOBIG;
    for (q = p; *p; ) {                  /* keep the slashes between the folders */
        if (p[0] == '%' && p[1] == '2' && p[2] == 'F') {
            *q++ = '/';
            p += 3;
        } else {
            *q++ = *p++;
        }
    }
    *q = '\0';
    if (colon)
        strcat(out, ":");
    return AD_OK;
}

static int add_str(char *out, int size, const char *s)
{
    if ((int)(strlen(out) + strlen(s)) >= size)
        return AD_ERR_TOOBIG;
    strcat(out, s);
    return AD_OK;
}

int graph_delta_link(const char *remote_path, char *link, int size)
{
    int rc = root_path(remote_path, 1, link, size);
    return rc != AD_OK ? rc : add_str(link, size, "/delta?$select=" ITEM_SELECT "&$top=" PAGE_TOP);
}

int graph_children_link(const char *remote_path, char *link, int size)
{
    int rc = root_path(remote_path, 1, link, size);
    return rc != AD_OK ? rc : add_str(link, size, "/children?$select=" ITEM_SELECT "&$top=" PAGE_TOP);
}

int graph_item(Session *g, const char *access_token, const char *remote_path, GraphItem *it,
               char *err, int errsize)
{
    static char path[GRAPH_LINK_SIZE];
    Json j;
    int rc;

    if (root_path(remote_path, 0, path, (int)sizeof(path)) != AD_OK ||
        add_str(path, (int)sizeof(path), "?$select=" ITEM_SELECT) != AD_OK) {
        set_err(err, errsize, "path too long", NULL, NULL);
        return AD_ERR_TOOBIG;
    }
    if ((rc = get_json(g, access_token, path, "folder", &j, err, errsize)) != AD_OK)
        return rc;
    if (parse_item(&j, 0, it) != AD_OK) {
        set_err(err, errsize, "unexpected answer from " AD_GRAPH_HOST, NULL, NULL);
        return AD_ERR_PROTO;
    }
    return AD_OK;
}

/* Path part of a link: "/v1.0/..." as it is, "https://graph.microsoft.com/v1.0/..."
 * without the host. NULL for anything else. */
static const char *link_path(const char *link)
{
    static const char host[] = "https://" AD_GRAPH_HOST;

    if (link[0] == '/')
        return link;
    if (strncmp(link, host, sizeof(host) - 1) == 0 && link[sizeof(host) - 1] == '/')
        return link + sizeof(host) - 1;
    return NULL;
}

int graph_page(Session *g, const char *access_token, char *link, int size, int *done,
               GraphItemFn fn, void *ud, char *err, int errsize)
{
    const char *path = link_path(link);
    Json j;
    int rc, arr, t, k, n, more = 0;

    *done = 0;
    if (!path) {
        set_err(err, errsize, "invalid link to the list of changes", NULL, NULL);
        return AD_ERR_PROTO;
    }
    if ((rc = get_json(g, access_token, path, "folder", &j, err, errsize)) != AD_OK)
        return rc;
    arr = json_get(&j, 0, "value");
    if (arr < 0 || j.tok[arr].type != JSMN_ARRAY) {
        set_err(err, errsize, "unexpected answer from " AD_GRAPH_HOST, NULL, NULL);
        return AD_ERR_PROTO;
    }

    /* the next link first: the callback may download, which reuses the session */
    next_link[0] = '\0';
    if ((t = json_get(&j, 0, "@odata.nextLink")) >= 0)
        more = 1;
    else
        t = json_get(&j, 0, "@odata.deltaLink");
    if ((t >= 0 && json_str(&j, t, next_link, sizeof(next_link)) < 0) ||
        (int)strlen(next_link) >= size) {
        set_err(err, errsize, "link to the next page too long", NULL, NULL);
        return AD_ERR_TOOBIG;
    }

    n = j.tok[arr].size;
    for (k = 0, t = arr + 1; k < n; k++, t = json_next(&j, t)) {
        if (j.tok[t].type != JSMN_OBJECT || parse_item(&j, t, &item) != AD_OK)
            continue;
        if ((rc = fn(ud, &item)) != AD_OK) {
            if (rc == AD_ERR_BREAK)
                set_err(err, errsize, "interrupted", NULL, NULL);
            return rc;
        }
    }
    *done = !more;
    strcpy(link, next_link);
    return AD_OK;
}

/* Point s at the host of "https://host[:443]/path" (no other scheme or port) and give
 * the path. Returns AD_OK or AD_ERR_PROTO. */
static int url_target(Session *s, const char *url, const char **path, const char *what,
                      char *err, int errsize)
{
    const char *host, *p;
    int hlen;

    if (strncmp(url, "https://", 8) != 0)
        goto bad;
    host = url + 8;
    for (p = host; *p && *p != '/' && *p != ':'; p++)
        ;
    hlen = (int)(p - host);
    if (*p == ':') {
        if (strncmp(p, ":443/", 5) != 0)
            goto bad;
        p += 4;
    }
    if (*p != '/' || sess_set_host(s, host, hlen) != AD_OK)
        goto bad;
    *path = p;
    return AD_OK;
bad:
    set_err(err, errsize, "invalid address for the ", what, NULL);
    return AD_ERR_PROTO;
}

/* ---- download ----------------------------------------------------------------------- */

/* Body of a 200 answer -> sink. */
static int receive(Session *s, GraphSinkFn sink, void *ud, unsigned long *got,
                   char *err, int errsize)
{
    long n;
    int rc;

    for (;;) {
        if (plat_break()) {
            sess_close(s);
            set_err(err, errsize, "interrupted", NULL, NULL);
            return AD_ERR_BREAK;
        }
        n = http_read_body(&s->http, dlbuf, (long)sizeof(dlbuf));
        if (n == 0)
            break;
        if (n < 0) {
            sess_close(s);
            set_err(err, errsize, "download from ", s->host, n == AD_ERR_BREAK ? "interrupted" :
                    "connection lost");
            return (int)n;
        }
        if ((rc = sink(ud, dlbuf, n)) != AD_OK) {
            sess_close(s);
            set_err(err, errsize, rc == AD_ERR_BREAK ? "interrupted" :
                    "cannot save the downloaded data", NULL, NULL);
            return rc;
        }
        *got += (unsigned long)n;
    }
    sess_done(s);
    return AD_OK;
}

int graph_download(Session *g, Session *dl, const char *access_token, const char *item_id,
                   GraphSinkFn sink, void *ud, unsigned long *got, char *err, int errsize)
{
    static char path[GRAPH_ID_SIZE * 3 + 40];
    Session *s = g;
    int rc, status, redirects;

    *got = 0;
    strcpy(path, "/v1.0/me/drive/items/");
    if (url_encode(item_id, path + strlen(path), sizeof(path) - strlen(path) - 9) < 0) {
        set_err(err, errsize, "item id too long", NULL, NULL);
        return AD_ERR_TOOBIG;
    }
    strcat(path, "/content");
    if ((rc = auth_headers(access_token, err, errsize)) != AD_OK)
        return rc;
    if ((rc = get_retry(g, path, headers, err, errsize)) != AD_OK)
        return rc;

    for (redirects = 0; ; redirects++) {
        const char *p;

        status = s->http.status;
        if (status == 200)
            return receive(s, sink, ud, got, err, errsize);
        strcpy(location, s->http.location);
        skip_body(s);
        if (!redirect(status))
            return status_error(status, "file", NULL, err, errsize);
        if (redirects >= MAX_REDIRECTS) {
            set_err(err, errsize, "too many redirects", NULL, NULL);
            return AD_ERR_PROTO;
        }

        /* only TLS; the token is not sent there - the address itself grants access
         * to the file for a short time */
        if ((rc = url_target(dl, location, &p, "download", err, errsize)) != AD_OK)
            return rc;
        s = dl;
        if ((rc = get_retry(s, p, NULL, err, errsize)) != AD_OK)
            return rc;
    }
}

/* ---- changes (stage 2) -------------------------------------------------------------- */

static char body[GRAPH_NAME_SIZE * 6 + GRAPH_ID_SIZE + 200];
static char ipath[GRAPH_ID_SIZE * 3 + GRAPH_NAME_SIZE * 3 + 96];
static char upload_url[4096];

/* Append s to out as a JSON string. */
static int add_json(char *out, int size, const char *s)
{
    static const char hexd[] = "0123456789abcdef";
    int n = (int)strlen(out);

    if (n + 2 >= size)
        return AD_ERR_TOOBIG;
    out[n++] = '"';
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (n + 8 >= size)
            return AD_ERR_TOOBIG;
        if (c == '"' || c == '\\') {
            out[n++] = '\\';
            out[n++] = (char)c;
        } else if (c < 0x20) {
            out[n++] = '\\';
            out[n++] = 'u';
            out[n++] = '0';
            out[n++] = '0';
            out[n++] = hexd[c >> 4];
            out[n++] = hexd[c & 15];
        } else {
            out[n++] = (char)c;
        }
    }
    out[n++] = '"';
    out[n] = '\0';
    return AD_OK;
}

/* ipath = "/v1.0/me/drive/items/<id>" + ":/<name>" (+ ":" before a "/..." suffix) + suffix */
static int items_path(const char *id, const char *name, const char *suffix)
{
    int n;

    strcpy(ipath, "/v1.0/me/drive/items/");
    n = (int)strlen(ipath);
    if (url_encode(id, ipath + n, sizeof(ipath) - (size_t)n) < 0)
        return AD_ERR_TOOBIG;
    if (name) {
        n = (int)strlen(ipath);
        if (add_str(ipath, (int)sizeof(ipath), ":/") != AD_OK ||
            url_encode(name, ipath + n + 2, sizeof(ipath) - (size_t)n - 2) < 0 ||
            (suffix[0] == '/' && add_str(ipath, (int)sizeof(ipath), ":") != AD_OK))
            return AD_ERR_TOOBIG;
    }
    return add_str(ipath, (int)sizeof(ipath), suffix);
}

static int too_long(char *err, int errsize)
{
    set_err(err, errsize, "name or id too long", NULL, NULL);
    return AD_ERR_TOOBIG;
}

/* the item answered in j -> it */
static int item_answer(const Json *j, GraphItem *it, char *err, int errsize)
{
    if (j->ntok == 0 || parse_item(j, 0, it) != AD_OK) {
        set_err(err, errsize, "unexpected answer from OneDrive", NULL, NULL);
        return AD_ERR_PROTO;
    }
    return AD_OK;
}

int graph_child(Session *g, const char *access_token, const char *parent_id, const char *name,
                GraphItem *it, char *err, int errsize)
{
    Json j;
    int rc;

    if (items_path(parent_id, name, "?$select=" ITEM_SELECT) != AD_OK)
        return too_long(err, errsize);
    if ((rc = get_json(g, access_token, ipath, "item", &j, err, errsize)) != AD_OK)
        return rc;
    return item_answer(&j, it, err, errsize);
}

int graph_create_folder(Session *g, const char *access_token, const char *parent_id,
                        const char *name, GraphItem *it, char *err, int errsize)
{
    Json j;
    int rc;

    if (items_path(parent_id, NULL, "/children") != AD_OK)
        return too_long(err, errsize);
    strcpy(body, "{\"name\":");
    if (add_json(body, (int)sizeof(body), name) != AD_OK ||
        add_str(body, (int)sizeof(body),
                ",\"folder\":{},\"@microsoft.graph.conflictBehavior\":\"fail\"}") != AD_OK)
        return too_long(err, errsize);
    if ((rc = call(g, access_token, "POST", ipath, NULL, body, "folder", &j, err, errsize)) != AD_OK)
        return rc;
    return item_answer(&j, it, err, errsize);
}

int graph_move(Session *g, const char *access_token, const char *id, const char *parent_id,
               const char *name, GraphItem *it, char *err, int errsize)
{
    Json j;
    int rc;

    if (items_path(id, NULL, "") != AD_OK)
        return too_long(err, errsize);
    strcpy(body, "{");
    if (name && (add_str(body, (int)sizeof(body), "\"name\":") != AD_OK ||
                 add_json(body, (int)sizeof(body), name) != AD_OK))
        return too_long(err, errsize);
    if (parent_id && (add_str(body, (int)sizeof(body), name ? ",\"parentReference\":{\"id\":" :
                              "\"parentReference\":{\"id\":") != AD_OK ||
                      add_json(body, (int)sizeof(body), parent_id) != AD_OK ||
                      add_str(body, (int)sizeof(body), "}") != AD_OK))
        return too_long(err, errsize);
    if (add_str(body, (int)sizeof(body), "}") != AD_OK)
        return too_long(err, errsize);
    if ((rc = call(g, access_token, "PATCH", ipath, NULL, body, "item", &j, err, errsize)) != AD_OK)
        return rc;
    return item_answer(&j, it, err, errsize);
}

/* "If-Match: <etag>\r\n", or "" */
static const char *if_match(const char *etag)
{
    static char h[GRAPH_TAG_SIZE + 16];

    h[0] = '\0';
    if (etag && *etag && strlen(etag) < GRAPH_TAG_SIZE) {
        strcpy(h, "If-Match: ");
        strcat(h, etag);
        strcat(h, "\r\n");
    }
    return h;
}

int graph_delete(Session *g, const char *access_token, const char *id, const char *etag,
                 char *err, int errsize)
{
    Json j;
    int rc;

    if (items_path(id, NULL, "") != AD_OK)
        return too_long(err, errsize);
    rc = call(g, access_token, "DELETE", ipath, if_match(etag), NULL, "item", &j, err, errsize);
    return rc == AD_ERR_NOTFOUND ? AD_OK : rc;
}

/* Read exactly len bytes from src (less only at the end of the file). */
static long read_piece(GraphSourceFn src, void *ud, char *buf, long len)
{
    long total = 0, n;

    while (total < len) {
        n = src(ud, buf + total, len - total);
        if (n < 0)
            return n;
        if (n == 0)
            break;
        total += n;
    }
    return total;
}

int graph_upload(Session *g, Session *ul, const char *access_token, const char *parent_id,
                 const char *name, const char *id, const char *etag, unsigned long size,
                 unsigned long mtime, GraphSourceFn src, void *ud, char *buf, long bufsize,
                 GraphItem *it, char *err, int errsize)
{
    char date[24], range[80];
    const char *upath;
    unsigned long pos = 0;
    long piece = bufsize - bufsize % GRAPH_CHUNK, n;
    Json j;
    int rc;

    tz_format_iso8601(mtime, date);
    if (size == 0) {
        /* an empty file cannot go through an upload session: put it, then date it */
        if ((id ? items_path(id, NULL, "/content") :
             items_path(parent_id, name, "/content?@microsoft.graph.conflictBehavior=fail")) != AD_OK)
            return too_long(err, errsize);
        if ((rc = call(g, access_token, "PUT", ipath, id ? if_match(etag) : NULL, NULL, "file",
                       &j, err, errsize)) != AD_OK ||
            (rc = item_answer(&j, it, err, errsize)) != AD_OK)
            return rc;
        if (items_path(it->id, NULL, "") != AD_OK)
            return too_long(err, errsize);
        strcpy(body, "{\"fileSystemInfo\":{\"lastModifiedDateTime\":\"");
        strcat(body, date);
        strcat(body, "\"}}");
        if ((rc = call(g, access_token, "PATCH", ipath, NULL, body, "file", &j, err, errsize)) != AD_OK)
            return rc;
        return item_answer(&j, it, err, errsize);
    }
    if (piece < GRAPH_CHUNK) {
        set_err(err, errsize, "upload buffer too small", NULL, NULL);
        return AD_ERR_TOOBIG;
    }

    /* the upload session: the date goes with it */
    if ((id ? items_path(id, NULL, "/createUploadSession") :
         items_path(parent_id, name, "/createUploadSession")) != AD_OK)
        return too_long(err, errsize);
    strcpy(body, "{\"item\":{\"@microsoft.graph.conflictBehavior\":\"");
    strcat(body, id ? "replace" : "fail");
    strcat(body, "\",\"fileSystemInfo\":{\"lastModifiedDateTime\":\"");
    strcat(body, date);
    strcat(body, "\"}}}");
    if ((rc = call(g, access_token, "POST", ipath, id ? if_match(etag) : NULL, body, "file",
                   &j, err, errsize)) != AD_OK)
        return rc;
    if (j.ntok == 0 ||
        json_str(&j, json_get(&j, 0, "uploadUrl"), upload_url, sizeof(upload_url)) <= 0) {
        set_err(err, errsize, "no upload address in the answer", NULL, NULL);
        return AD_ERR_PROTO;
    }

    /* the pieces, to the upload server, without the token */
    while (pos < size) {
        long want = size - pos < (unsigned long)piece ? (long)(size - pos) : piece;

        if (plat_break()) {
            set_err(err, errsize, "interrupted", NULL, NULL);
            return AD_ERR_BREAK;
        }
        n = read_piece(src, ud, buf, want);
        if (n != want) {
            set_err(err, errsize, n < 0 ? "cannot read the file" :
                    "the file changed while it was sent", NULL, NULL);
            return AD_ERR;
        }
        sprintf(range, "Content-Range: bytes %lu-%lu/%lu\r\n", pos, pos + (unsigned long)n - 1, size);
        if ((rc = url_target(ul, upload_url, &upath, "upload", err, errsize)) != AD_OK ||
            (rc = send_retry(ul, "PUT", upath, range, buf, (unsigned long)n, err, errsize)) != AD_OK ||
            (rc = answer_json(ul, "file", &j, err, errsize)) != AD_OK)
            return rc;
        pos += (unsigned long)n;
        if (ul->http.status == 200 || ul->http.status == 201)
            return pos == size ? item_answer(&j, it, err, errsize) : AD_ERR_PROTO;
    }
    set_err(err, errsize, "upload not confirmed", NULL, NULL);
    return AD_ERR_PROTO;
}
