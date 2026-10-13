/* Sync state (see state.h). */
#include <stdlib.h>
#include <string.h>

#include "amidrive.h"
#include "fsmap.h"
#include "plat.h"
#include "state.h"

#define STATE_MAGIC "AmiDrive state 1"
#define LINE_SIZE   (GRAPH_LINK_SIZE * 2 + 64)   /* the delta line, escaped */
#define MAX_FIELDS  12

/* ---- entries ------------------------------------------------------------------------ */

static unsigned slot(const char *id)
{
    return (unsigned)(fs_hash(id) % ST_BUCKETS);
}

static unsigned name_slot(const char *parent, const char *lname)
{
    return (unsigned)((fs_hash(parent) * 31UL + fs_hash(lname)) % ST_BUCKETS);
}

void state_init(State *s)
{
    memset(s, 0, sizeof(*s));
}

void state_free(State *s)
{
    int i;

    for (i = 0; i < ST_BUCKETS; i++) {
        StEntry *e = s->bucket[i];
        while (e) {
            StEntry *next = e->next;
            free(e);
            e = next;
        }
        s->bucket[i] = NULL;
        s->by_name[i] = NULL;
    }
    s->count = 0;
}

StEntry *state_find(const State *s, const char *id)
{
    StEntry *e;

    for (e = s->bucket[slot(id)]; e; e = e->next)
        if (strcmp(e->id, id) == 0)
            return e;
    return NULL;
}

StEntry *state_find_name(const State *s, const char *parent_id, const char *lname)
{
    StEntry *e;

    for (e = s->by_name[name_slot(parent_id, lname)]; e; e = e->next_name)
        if (strcmp(e->lname, lname) == 0 && strcmp(e->parent, parent_id) == 0)
            return e;
    return NULL;
}

static char *put_s(char **p, const char *v)
{
    char *r = *p;
    size_t n = v ? strlen(v) : 0;

    if (n)
        memcpy(r, v, n);
    r[n] = '\0';
    *p += n + 1;
    return r;
}

static size_t len0(const char *v)
{
    return v ? strlen(v) + 1 : 1;
}

StEntry *state_put(State *s, const StEntry *e)
{
    StEntry *n;
    char *p;
    unsigned h;

    /* one block: the entry and its strings. Made before the old entry is removed,
     * because e may point into it. */
    n = malloc(sizeof(*n) + len0(e->id) + len0(e->parent) + len0(e->name) + len0(e->lname) +
               len0(e->ctag) + len0(e->etag));
    if (!n)
        return NULL;
    *n = *e;
    p = (char *)(n + 1);
    n->id = put_s(&p, e->id);
    n->parent = put_s(&p, e->parent);
    n->name = put_s(&p, e->name);
    n->lname = put_s(&p, e->lname);
    n->ctag = put_s(&p, e->ctag);
    n->etag = put_s(&p, e->etag);

    state_remove(s, n->id);
    h = slot(n->id);
    n->next = s->bucket[h];
    s->bucket[h] = n;
    h = name_slot(n->parent, n->lname);
    n->next_name = s->by_name[h];
    s->by_name[h] = n;
    s->count++;
    return n;
}

int state_remove(State *s, const char *id)
{
    StEntry **pp;

    for (pp = &s->bucket[slot(id)]; *pp; pp = &(*pp)->next) {
        if (strcmp((*pp)->id, id) == 0) {
            StEntry *e = *pp, **np;
            *pp = e->next;
            for (np = &s->by_name[name_slot(e->parent, e->lname)]; *np; np = &(*np)->next_name)
                if (*np == e) {
                    *np = e->next_name;
                    break;
                }
            free(e);
            s->count--;
            return AD_OK;
        }
    }
    return AD_ERR;
}

StEntry *state_next(const State *s, const StEntry *e)
{
    unsigned i;

    if (e && e->next)
        return e->next;
    for (i = e ? slot(e->id) + 1 : 0; i < ST_BUCKETS; i++)
        if (s->bucket[i])
            return s->bucket[i];
    return NULL;
}

/* ---- paths -------------------------------------------------------------------------- */

static int build_path(const State *s, const StEntry *e, const char *base, int local,
                      char *out, int size)
{
    const StEntry *chain[ST_MAXDEPTH];
    int depth = 0, len, i;

    if (strcmp(e->id, s->root_id) != 0) {
        for (;;) {
            if (depth >= ST_MAXDEPTH)
                return AD_ERR;              /* too deep, or a loop */
            chain[depth++] = e;
            if (strcmp(e->parent, s->root_id) == 0)
                break;
            if (!(e = state_find(s, e->parent)))
                return AD_ERR;
        }
    }

    len = (int)strlen(base);
    if (len >= size)
        return AD_ERR_TOOBIG;
    strcpy(out, base);
    for (i = depth - 1; i >= 0; i--) {
        const char *name = local ? chain[i]->lname : chain[i]->name;
        int n = (int)strlen(name);
        int sep = local ? len > 0 && out[len - 1] != ':' && out[len - 1] != '/'
                        : len == 0 || out[len - 1] != '/';
        if (len + sep + n >= size)
            return AD_ERR_TOOBIG;
        if (sep)
            out[len++] = '/';
        memcpy(out + len, name, (size_t)n + 1);
        len += n;
    }
    return len;
}

int state_local_path(const State *s, const StEntry *e, const char *base, char *out, int size)
{
    return build_path(s, e, base, 1, out, size);
}

int state_remote_path(const State *s, const StEntry *e, const char *base, char *out, int size)
{
    return build_path(s, e, base, 0, out, size);
}

/* ---- saving --------------------------------------------------------------------------- */

static PlatFile *wf;
static char wbuf[4096];
static int wlen, wfail;
static char line[LINE_SIZE];

static void flush(void)
{
    if (wlen > 0 && !wfail && plat_write(wf, wbuf, wlen) != AD_OK)
        wfail = 1;
    wlen = 0;
}

static void put_c(char c)
{
    if (wlen >= (int)sizeof(wbuf))
        flush();
    wbuf[wlen++] = c;
}

static void put_raw(const char *s)
{
    while (*s)
        put_c(*s++);
}

/* tab, newline, CR and backslash escaped, so a value never splits a line or field */
static void put_esc(const char *s)
{
    put_c('\t');
    for (; *s; s++) {
        switch (*s) {
        case '\t': put_raw("\\t"); break;
        case '\n': put_raw("\\n"); break;
        case '\r': put_raw("\\r"); break;
        case '\\': put_raw("\\\\"); break;
        default:   put_c(*s);
        }
    }
}

static void put_num(unsigned long v)
{
    char d[12];
    int n = 0;

    put_c('\t');
    do {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n > 0)
        put_c(d[--n]);
}

int state_save(const State *s, const char *path)
{
    const StEntry *e;

    if (!(wf = plat_open_write(path)))
        return AD_ERR;
    wlen = 0;
    wfail = 0;
    put_raw(STATE_MAGIC "\nroot");
    put_esc(s->root_id);
    put_raw("\nnames");
    put_esc(s->names);
    put_raw("\ndelta");
    put_esc(s->delta);
    put_c('\n');
    for (e = state_next(s, NULL); e; e = state_next(s, e)) {
        put_c('e');
        put_num(e->flags & 0xFF);       /* higher bits: in memory only */
        put_num(e->size);
        put_num(e->mtime);
        put_num(e->ltime);
        put_esc(e->id);
        put_esc(e->parent);
        put_esc(e->ctag);
        put_esc(e->etag);
        put_esc(e->name);
        put_esc(e->lname);
        put_c('\n');
    }
    put_raw("end");
    put_num((unsigned long)s->count);
    put_c('\n');
    flush();
    return plat_close(wf, !wfail) == AD_OK && !wfail ? AD_OK : AD_ERR;
}

/* ---- loading -------------------------------------------------------------------------- */

static PlatFile *rf;
static char rbuf[4096];
static long rpos, rlen;

/* Next line without the '\n'. Returns its length, -1 at the end of the file,
 * AD_ERR_PROTO when too long or unreadable. A last line without '\n' counts as cut. */
static int get_line(void)
{
    int n = 0;

    for (;;) {
        if (rpos >= rlen) {
            rlen = plat_read(rf, rbuf, (long)sizeof(rbuf));
            rpos = 0;
            if (rlen < 0)
                return AD_ERR_PROTO;
            if (rlen == 0)
                return n == 0 ? -1 : AD_ERR_PROTO;
        }
        if (rbuf[rpos] == '\n') {
            rpos++;
            line[n] = '\0';
            return n;
        }
        if (n >= LINE_SIZE - 1)
            return AD_ERR_PROTO;
        line[n++] = rbuf[rpos++];
    }
}

/* Split line at tabs and undo the escapes, in place. Returns the number of fields. */
static int split(char **f)
{
    char *r = line, *w = line;
    int n = 0;

    f[n++] = w;
    for (; *r; r++) {
        if (*r == '\t') {
            *w++ = '\0';
            if (n >= MAX_FIELDS)
                return -1;
            f[n++] = w;
        } else if (*r == '\\' && r[1]) {
            r++;
            *w++ = *r == 't' ? '\t' : *r == 'n' ? '\n' : *r == 'r' ? '\r' : *r;
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
    return n;
}

static int get_num(const char *s, unsigned long *out)
{
    unsigned long v = 0;

    if (!*s)
        return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || v > (0xFFFFFFFFUL - (unsigned long)(*s - '0')) / 10)
            return 0;
        v = v * 10 + (unsigned long)(*s - '0');
    }
    *out = v;
    return 1;
}

static int copy_field(char *dst, size_t size, const char *src)
{
    if (strlen(src) >= size)
        return 0;
    strcpy(dst, src);
    return 1;
}

static int load_lines(State *s)
{
    char *f[MAX_FIELDS];
    int n;

    if (get_line() < 0 || strcmp(line, STATE_MAGIC) != 0)
        return AD_ERR_PROTO;
    while ((n = get_line()) >= 0) {
        n = split(f);
        if (n == 2 && strcmp(f[0], "root") == 0) {
            if (!copy_field(s->root_id, sizeof(s->root_id), f[1]))
                return AD_ERR_PROTO;
        } else if (n == 2 && strcmp(f[0], "names") == 0) {
            if (!copy_field(s->names, sizeof(s->names), f[1]))
                return AD_ERR_PROTO;
        } else if (n == 2 && strcmp(f[0], "delta") == 0) {
            if (!copy_field(s->delta, sizeof(s->delta), f[1]))
                return AD_ERR_PROTO;
        } else if (n == 11 && strcmp(f[0], "e") == 0) {
            StEntry e;
            unsigned long flags;
            memset(&e, 0, sizeof(e));
            if (!get_num(f[1], &flags) || !get_num(f[2], &e.size) || !get_num(f[3], &e.mtime) ||
                !get_num(f[4], &e.ltime) || !*f[5])
                return AD_ERR_PROTO;
            e.flags = (unsigned)flags;
            e.id = f[5];
            e.parent = f[6];
            e.ctag = f[7];
            e.etag = f[8];
            e.name = f[9];
            e.lname = f[10];
            if (!state_put(s, &e))
                return AD_ERR_NOMEM;
        } else if (n == 2 && strcmp(f[0], "end") == 0) {
            unsigned long count;
            return get_num(f[1], &count) && count == (unsigned long)s->count ? AD_OK : AD_ERR_PROTO;
        } else {
            return AD_ERR_PROTO;
        }
    }
    return AD_ERR_PROTO;                /* no end line: the file was cut */
}

int state_load(State *s, const char *path)
{
    int rc;

    state_free(s);
    s->root_id[0] = s->delta[0] = s->names[0] = '\0';
    if (!(rf = plat_open_read(path)))
        return AD_ERR;
    rpos = rlen = 0;
    rc = load_lines(s);
    plat_close(rf, 0);
    if (rc != AD_OK) {
        state_free(s);
        s->root_id[0] = s->delta[0] = s->names[0] = '\0';
    }
    return rc;
}
