/* Settings file (see config.h). Portable. */
#include <string.h>

#include "amidrive.h"
#include "config.h"

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int eq_ci(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (lower((unsigned char)*a) != lower((unsigned char)*b))
            return 0;
    return *a == *b;
}

static int copy(char *dst, size_t size, const char *src)
{
    size_t n = strlen(src);

    if (n >= size)
        return AD_ERR_TOOBIG;
    memcpy(dst, src, n + 1);
    return AD_OK;
}

int kv_get(const char *text, const char *key, char *out, size_t size)
{
    size_t klen = strlen(key);
    const char *line = text;

    while (*line) {
        const char *end = line, *p = line, *v, *vend;
        size_t i;

        while (*end && *end != '\n')
            end++;
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;
        for (i = 0; i < klen && p + i < end; i++)
            if (lower((unsigned char)p[i]) != lower((unsigned char)key[i]))
                break;
        if (i == klen && *p != '#') {
            const char *q = p + klen;
            while (q < end && (*q == ' ' || *q == '\t'))    /* "KEY = value" */
                q++;
            if (q >= end || *q != '=') {
                line = *end ? end + 1 : end;
                continue;
            }
            v = q + 1;
            while (v < end && (*v == ' ' || *v == '\t'))
                v++;
            vend = end;
            while (vend > v && (vend[-1] == ' ' || vend[-1] == '\t' || vend[-1] == '\r'))
                vend--;
            if ((size_t)(vend - v) >= size)
                return AD_ERR_TOOBIG;
            memcpy(out, v, (size_t)(vend - v));
            out[vend - v] = '\0';
            return (int)(vend - v);
        }
        line = *end ? end + 1 : end;
    }
    return AD_ERR;
}

void config_defaults(Config *c)
{
    memset(c, 0, sizeof(*c));
    strcpy(c->charset, "LATIN1");
    strcpy(c->upload, "YES");
    strcpy(c->appicon, "YES");
    strcpy(c->clock, "ASK");
    c->interval = CFG_INTERVAL_DEFAULT;
    strcpy(c->client_id, AMIDRIVE_CLIENT_ID);
    strcpy(c->tenant, AMIDRIVE_TENANT);
}

static int parse_minutes(const char *s, long *out)
{
    long v = 0;

    if (!*s)
        return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || v > 100000)
            return 0;
        v = v * 10 + (*s - '0');
    }
    if (v < 1 || v > 24 * 60)
        return 0;
    *out = v;
    return 1;
}

int config_set(Config *c, const char *key, const char *value)
{
    if (eq_ci(key, "LOCAL"))
        return copy(c->local, sizeof(c->local), value) ? AD_ERR_PROTO : AD_OK;
    if (eq_ci(key, "REMOTE")) {
        /* "Amiga", "/Amiga/" and "/" all accepted; stored as "/Amiga" or "/" */
        char tmp[CFG_PATH_SIZE];
        size_t n;
        if (!*value) {                  /* not configured: never the whole drive */
            c->remote[0] = '\0';
            return AD_OK;
        }
        tmp[0] = '/';
        if (copy(tmp + (value[0] != '/'), sizeof(tmp) - 1, value))
            return AD_ERR_PROTO;
        n = strlen(tmp);
        while (n > 1 && tmp[n - 1] == '/')
            tmp[--n] = '\0';
        return copy(c->remote, sizeof(c->remote), tmp) ? AD_ERR_PROTO : AD_OK;
    }
    if (eq_ci(key, "STATE"))
        return copy(c->state, sizeof(c->state), value) ? AD_ERR_PROTO : AD_OK;
    if (eq_ci(key, "CHARSET")) {
        if (!eq_ci(value, "LATIN1") && !eq_ci(value, "LATIN2"))
            return AD_ERR_PROTO;
        strcpy(c->charset, eq_ci(value, "LATIN1") ? "LATIN1" : "LATIN2");
        return AD_OK;
    }
    if (eq_ci(key, "UPLOAD") || eq_ci(key, "APPICON")) {
        if (!eq_ci(value, "YES") && !eq_ci(value, "NO"))
            return AD_ERR_PROTO;
        strcpy(eq_ci(key, "UPLOAD") ? c->upload : c->appicon, eq_ci(value, "YES") ? "YES" : "NO");
        return AD_OK;
    }
    if (eq_ci(key, "CLOCK")) {
        if (!eq_ci(value, "ASK") && !eq_ci(value, "AUTO") && !eq_ci(value, "NO"))
            return AD_ERR_PROTO;
        strcpy(c->clock, eq_ci(value, "ASK") ? "ASK" : eq_ci(value, "AUTO") ? "AUTO" : "NO");
        return AD_OK;
    }
    if (eq_ci(key, "INTERVAL"))
        return parse_minutes(value, &c->interval) ? AD_OK : AD_ERR_PROTO;
    /* empty: keep the built-in default */
    if (eq_ci(key, "CLIENTID"))
        return !*value ? AD_OK : copy(c->client_id, sizeof(c->client_id), value) ? AD_ERR_PROTO : AD_OK;
    if (eq_ci(key, "TENANT"))
        return !*value ? AD_OK : copy(c->tenant, sizeof(c->tenant), value) ? AD_ERR_PROTO : AD_OK;
    return AD_ERR;
}

int config_parse(Config *c, const char *text)
{
    static const char *const keys[] = {
        "LOCAL", "REMOTE", "STATE", "CHARSET", "UPLOAD", "APPICON", "CLOCK", "INTERVAL", "CLIENTID",
        "TENANT", NULL
    };
    char value[CFG_PATH_SIZE];
    int i, rc = AD_OK;

    for (i = 0; keys[i]; i++) {
        int n = kv_get(text, keys[i], value, sizeof(value));
        if (n == AD_ERR)
            continue;
        if (n < 0 || config_set(c, keys[i], value) != AD_OK)
            rc = AD_ERR_PROTO;
    }
    return rc;
}

static int append(char *buf, size_t size, size_t *len, const char *key, const char *value)
{
    size_t k = strlen(key), v = strlen(value);

    if (*len + k + v + 3 > size)
        return 0;
    memcpy(buf + *len, key, k);
    buf[*len + k] = '=';
    memcpy(buf + *len + k + 1, value, v);
    buf[*len + k + 1 + v] = '\n';
    *len += k + v + 2;
    buf[*len] = '\0';
    return 1;
}

int config_format(const Config *c, char *buf, size_t size)
{
    static const char head[] = "# AmiDrive settings (Shell arguments and Tool Types take precedence)\n";
    char num[12];
    size_t len = sizeof(head) - 1;
    long v = c->interval;
    int i = (int)sizeof(num) - 1;

    if (size <= len)
        return AD_ERR_TOOBIG;
    memcpy(buf, head, len + 1);

    num[i] = '\0';
    do {
        num[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v && i > 0);

    if (!append(buf, size, &len, "LOCAL", c->local) ||
        !append(buf, size, &len, "REMOTE", c->remote) ||
        !append(buf, size, &len, "STATE", c->state) ||
        !append(buf, size, &len, "CHARSET", c->charset) ||
        !append(buf, size, &len, "UPLOAD", c->upload) ||
        !append(buf, size, &len, "APPICON", c->appicon) ||
        !append(buf, size, &len, "CLOCK", c->clock) ||
        !append(buf, size, &len, "INTERVAL", num + i) ||
        !append(buf, size, &len, "CLIENTID", c->client_id) ||
        !append(buf, size, &len, "TENANT", c->tenant))
        return AD_ERR_TOOBIG;
    return (int)len;
}
