/* JSON helpers over jsmn (see json.h). This file also holds the jsmn implementation. */
#include <string.h>

#define JSMN_STRICT
#define JSMN_STATIC                     /* jsmn is used only through this file */
#include "../third_party/jsmn/jsmn.h"   /* implementation (no JSMN_HEADER here) */

#include "amidrive.h"
#include "json.h"

int json_parse(Json *j, const char *js, size_t len, jsmntok_t *tok, unsigned int maxtok)
{
    jsmn_parser p;
    int n;

    jsmn_init(&p);
    n = jsmn_parse(&p, js, len, tok, maxtok);
    j->js = js;
    j->tok = tok;
    j->ntok = n > 0 ? n : 0;
    if (n == JSMN_ERROR_NOMEM)
        return AD_ERR_TOOBIG;
    if (n <= 0)
        return AD_ERR_PROTO;
    return AD_OK;
}

int json_next(const Json *j, int t)
{
    int pending = 1;                    /* values still to skip, children included */

    while (pending > 0 && t < j->ntok) {
        const jsmntok_t *k = &j->tok[t];
        pending--;
        /* object: its keys; array: its elements; key (string with size 1): its value */
        pending += k->size;
        t++;
    }
    return t;
}

int json_eq(const Json *j, int t, const char *s)
{
    const jsmntok_t *k;
    size_t n = strlen(s);

    if (t < 0 || t >= j->ntok)
        return 0;
    k = &j->tok[t];
    return k->type == JSMN_STRING && (size_t)(k->end - k->start) == n &&
           memcmp(j->js + k->start, s, n) == 0;
}

int json_get(const Json *j, int obj, const char *key)
{
    int i, t;

    if (obj < 0 || obj >= j->ntok || j->tok[obj].type != JSMN_OBJECT)
        return -1;
    t = obj + 1;
    for (i = 0; i < j->tok[obj].size && t < j->ntok; i++) {
        if (json_eq(j, t, key))
            return t + 1 < j->ntok ? t + 1 : -1;
        t = json_next(j, t + 1);        /* skip the key's value */
    }
    return -1;
}

int json_path(const Json *j, int obj, const char *path)
{
    char key[64];

    while (obj >= 0 && *path) {
        size_t n = 0;
        while (path[n] && path[n] != '.')
            n++;
        if (n >= sizeof(key))
            return -1;
        memcpy(key, path, n);
        key[n] = '\0';
        obj = json_get(j, obj, key);
        path += n;
        if (*path == '.')
            path++;
    }
    return obj;
}

int json_elem(const Json *j, int arr, int i)
{
    int t;

    if (arr < 0 || arr >= j->ntok || j->tok[arr].type != JSMN_ARRAY ||
        i < 0 || i >= j->tok[arr].size)
        return -1;
    t = arr + 1;
    while (i-- > 0)
        t = json_next(j, t);
    return t < j->ntok ? t : -1;
}

int json_is_null(const Json *j, int t)
{
    return t >= 0 && t < j->ntok && j->tok[t].type == JSMN_PRIMITIVE &&
           j->js[j->tok[t].start] == 'n';
}

static int hex4(const char *s, unsigned long *out)
{
    unsigned long v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        int c = (unsigned char)s[i];
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= (unsigned long)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned long)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned long)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

/* Append code point cp as UTF-8; returns the new length or AD_ERR_TOOBIG. */
static int put_utf8(char *out, size_t size, int len, unsigned long cp)
{
    unsigned char b[4];
    int n, i;

    if (cp < 0x80)         { b[0] = (unsigned char)cp; n = 1; }
    else if (cp < 0x800)   { b[0] = (unsigned char)(0xC0 | (cp >> 6));
                             b[1] = (unsigned char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) { b[0] = (unsigned char)(0xE0 | (cp >> 12));
                             b[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                             b[2] = (unsigned char)(0x80 | (cp & 0x3F)); n = 3; }
    else                   { b[0] = (unsigned char)(0xF0 | (cp >> 18));
                             b[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
                             b[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                             b[3] = (unsigned char)(0x80 | (cp & 0x3F)); n = 4; }
    if ((size_t)(len + n) >= size)
        return AD_ERR_TOOBIG;
    for (i = 0; i < n; i++)
        out[len++] = (char)b[i];
    return len;
}

int json_str(const Json *j, int t, char *out, size_t size)
{
    const char *s, *end;
    int len = 0;

    if (size == 0)
        return AD_ERR_TOOBIG;
    out[0] = '\0';
    if (t < 0 || t >= j->ntok || j->tok[t].type != JSMN_STRING)
        return AD_ERR_PROTO;
    s = j->js + j->tok[t].start;
    end = j->js + j->tok[t].end;

    while (s < end) {
        unsigned long cp;

        if (*s != '\\') {
            if ((size_t)len + 1 >= size) {
                out[len] = '\0';                        /* cut, but terminated */
                return AD_ERR_TOOBIG;
            }
            out[len++] = *s++;
            continue;
        }
        s++;
        if (s >= end)
            return AD_ERR_PROTO;
        switch (*s) {
        case '"':  cp = '"';  break;
        case '\\': cp = '\\'; break;
        case '/':  cp = '/';  break;
        case 'b':  cp = '\b'; break;
        case 'f':  cp = '\f'; break;
        case 'n':  cp = '\n'; break;
        case 'r':  cp = '\r'; break;
        case 't':  cp = '\t'; break;
        case 'u':
            if (end - s < 5 || !hex4(s + 1, &cp))
                return AD_ERR_PROTO;
            s += 4;
            if (cp >= 0xD800 && cp < 0xDC00) {          /* high surrogate */
                unsigned long lo;
                if (end - s < 7 || s[1] != '\\' || s[2] != 'u' || !hex4(s + 3, &lo) ||
                    lo < 0xDC00 || lo > 0xDFFF)
                    return AD_ERR_PROTO;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                s += 6;
            } else if (cp >= 0xDC00 && cp < 0xE000) {
                return AD_ERR_PROTO;                    /* lone low surrogate */
            }
            break;
        default:
            return AD_ERR_PROTO;
        }
        s++;
        {
            int n = put_utf8(out, size, len, cp);
            if (n < 0) {
                out[len] = '\0';
                return n;
            }
            len = n;
        }
    }
    out[len] = '\0';
    return len;
}

int json_ulong(const Json *j, int t, unsigned long *out)
{
    const char *s, *end;
    unsigned long v = 0;

    if (t < 0 || t >= j->ntok || j->tok[t].type != JSMN_PRIMITIVE)
        return AD_ERR_PROTO;
    s = j->js + j->tok[t].start;
    end = j->js + j->tok[t].end;
    if (s >= end || *s < '0' || *s > '9')
        return AD_ERR_PROTO;
    for (; s < end; s++) {
        unsigned long d;
        if (*s < '0' || *s > '9')
            return AD_ERR_PROTO;
        d = (unsigned long)(*s - '0');
        if (v > (0xFFFFFFFFUL - d) / 10)
            return AD_ERR_TOOBIG;                       /* files >= 4 GB */
        v = v * 10 + d;
    }
    *out = v;
    return AD_OK;
}

int json_ulong_cut(const Json *j, int t, int drop, unsigned long *out)
{
    const char *s, *end;
    unsigned long v = 0;

    if (t < 0 || t >= j->ntok || j->tok[t].type != JSMN_PRIMITIVE)
        return AD_ERR_PROTO;
    s = j->js + j->tok[t].start;
    end = j->js + j->tok[t].end;
    if (s >= end || *s < '0' || *s > '9')
        return AD_ERR_PROTO;
    for (; s < end; s++)
        if (*s < '0' || *s > '9')
            return AD_ERR_PROTO;
    s = j->js + j->tok[t].start;
    end -= drop;
    for (; s < end; s++) {
        unsigned long d = (unsigned long)(*s - '0');
        if (v > (0xFFFFFFFFUL - d) / 10)
            return AD_ERR_TOOBIG;
        v = v * 10 + d;
    }
    *out = v;
    return AD_OK;
}

int json_bool(const Json *j, int t, int *out)
{
    char c;

    if (t < 0 || t >= j->ntok || j->tok[t].type != JSMN_PRIMITIVE)
        return AD_ERR_PROTO;
    c = j->js[j->tok[t].start];
    if (c != 't' && c != 'f')
        return AD_ERR_PROTO;
    *out = c == 't';
    return AD_OK;
}
