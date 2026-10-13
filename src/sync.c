/* Synchronisation, stage 1: cloud -> Amiga (see sync.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amidrive.h"
#include "graph.h"
#include "plat.h"
#include "sync.h"

#define PATH_SIZE    512
#define SYNC_PENDING 1           /* the parent folder is not known yet: try again later */
#define ST_SEEN      0x100       /* listed in this full delta (in memory only) */

typedef struct Pending {
    struct Pending *next;
    GraphItem it;
} Pending;

/* the pass in progress (single threaded) */
static const SyncConfig *cfg;
static State *st;
static SyncStats *stats;
static Pending *pending;
static int dirty;                /* state changed since the last save */
static unsigned long last_save;
static PlatFile *out_file;

static char ierr[200];           /* message of the last failure */
static char msg[PATH_SIZE + 200];
static char dir[PATH_SIZE], target[PATH_SIZE], oldp[PATH_SIZE], tmp[PATH_SIZE], alt[PATH_SIZE];
static char lname[FS_MAXNAME_LONG + 8];
static char shown[256];

/* ---- messages ------------------------------------------------------------------------- */

static void say(const char *what, const char *path, const char *why)
{
    sprintf(msg, "%s %.*s%s%.150s", what, PATH_SIZE - 1, path, why ? ": " : "", why ? why : "");
    cfg->log(cfg->ud, msg);
}

static void fail(const char *path, const char *why)
{
    stats->failed++;
    say("Failed", path, why);
}

/* remote UTF-8 name in the Amiga code page, for messages */
static const char *show(const char *utf8)
{
    int changed;

    if (fs_from_utf8(cfg->charset, utf8, shown, sizeof(shown), &changed) < 0)
        shown[sizeof(shown) - 1] = '\0';
    return shown;
}

/* ---- helpers -------------------------------------------------------------------------- */

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

static int join(char *out, const char *d, const char *name)
{
    size_t n = strlen(d);
    int sep = n > 0 && d[n - 1] != ':' && d[n - 1] != '/';

    if (n + (size_t)sep + strlen(name) >= PATH_SIZE)
        return AD_ERR_TOOBIG;
    strcpy(out, d);
    if (sep)
        strcat(out, "/");
    strcat(out, name);
    return AD_OK;
}

static const char *token(int force, int *rc)
{
    return cfg->token(cfg->ud, force, rc, ierr, (int)sizeof(ierr));
}

/* errors that stop the pass; anything else only fails the item */
static int fatal(int rc)
{
    return rc == AD_ERR_NET || rc == AD_ERR_TLS || rc == AD_ERR_BREAK || rc == AD_ERR_AUTH ||
           rc == AD_ERR_NOMEM || rc == AD_ERR_HTTP;
}

static void maybe_save(int force)
{
    if (!dirty || (!force && plat_now() - last_save < SYNC_SAVE_SECS))
        return;
    if (state_save(st, cfg->state_file) == AD_OK)
        dirty = 0;
    else
        say("Warning: cannot save", cfg->state_file, NULL);
    last_save = plat_now();
}

static int put_item(const GraphItem *it, unsigned long ltime)
{
    StEntry e;

    memset(&e, 0, sizeof(e));
    e.id = (char *)it->id;
    e.parent = (char *)it->parent_id;
    e.name = (char *)it->name;
    e.lname = lname;
    e.ctag = (char *)it->ctag;
    e.etag = (char *)it->etag;
    e.size = it->size;
    e.mtime = it->mtime;
    e.ltime = ltime;
    e.flags = (it->flags & GI_FOLDER ? ST_FOLDER : 0) | (stats->full ? ST_SEEN : 0);
    if (!state_put(st, &e)) {
        strcpy(ierr, "out of memory");
        return AD_ERR_NOMEM;
    }
    dirty = 1;
    return AD_OK;
}

/* Local directory of a parent folder into dir: AD_OK or SYNC_PENDING. */
static int parent_dir(const char *parent_id)
{
    StEntry *p;

    if (strcmp(parent_id, st->root_id) == 0) {
        if (strlen(cfg->local) >= PATH_SIZE)
            return SYNC_PENDING;
        strcpy(dir, cfg->local);
        return AD_OK;
    }
    p = state_find(st, parent_id);
    if (!p || !(p->flags & ST_FOLDER) || state_local_path(st, p, cfg->local, dir, PATH_SIZE) < 0)
        return SYNC_PENDING;
    return AD_OK;
}

/* Local name of an item into lname, and target = dir/lname. A name that had to be
 * changed always gets the hash, so it cannot meet a real name of the same form. */
static int local_target(const GraphItem *it)
{
    int changed, n = fs_local_name(cfg->charset, it->name, cfg->maxname, 0, lname, sizeof(lname), &changed);

    if (n >= 0 && changed)
        n = fs_local_name(cfg->charset, it->name, cfg->maxname, 1, lname, sizeof(lname), &changed);
    if (n < 0 || join(target, dir, lname) != AD_OK) {
        fail(show(it->name), "name or path too long");
        return AD_ERR;
    }
    return AD_OK;
}

/* Move a file changed on the Amiga out of the way: "name-Amiga.ext", "name-Amiga2.ext" ... */
static int keep_local(const char *path, const char *ln)
{
    char name[FS_MAXNAME_LONG + 8], suffix[10];
    const char *dot = strrchr(ln, '.');
    int ext, stem, k;
    PlatInfo info;

    ext = dot && dot != ln && (int)strlen(dot) <= cfg->maxname / 3 ? (int)strlen(dot) : 0;
    for (k = 1; k <= 9; k++) {
        sprintf(suffix, k == 1 ? "-Amiga" : "-Amiga%d", k);
        stem = (int)strlen(ln) - ext;
        if (stem + (int)strlen(suffix) + ext > cfg->maxname)
            stem = cfg->maxname - (int)strlen(suffix) - ext;
        memcpy(name, ln, (size_t)stem);
        strcpy(name + stem, suffix);
        strcat(name, ln + strlen(ln) - ext);
        if (join(alt, dir, name) != AD_OK)
            break;
        if (plat_file_info(alt, &info) != AD_OK) {
            if (plat_rename(path, alt) != AD_OK)
                break;
            stats->kept++;
            say("Kept (changed on the Amiga) as", alt, NULL);
            return AD_OK;
        }
    }
    fail(path, "changed on the Amiga, cannot keep a copy");
    return AD_ERR;
}

/* ---- deleting ------------------------------------------------------------------------- */

static StEntry *find_child(const char *id)
{
    StEntry *e;

    for (e = state_next(st, NULL); e; e = state_next(st, e))
        if (strcmp(e->parent, id) == 0)
            return e;
    return NULL;
}

/* Delete the local copy of an entry (a folder with its contents) and forget it.
 * Files changed on the Amiga, and folders still holding something, are kept. */
static void do_delete(StEntry *e)
{
    char id[GRAPH_ID_SIZE];
    PlatInfo info;
    StEntry *c;

    strcpy(id, e->id);
    if (e->flags & ST_FOLDER) {
        while ((c = find_child(id)) != NULL)
            do_delete(c);
        e = state_find(st, id);
        if (state_local_path(st, e, cfg->local, target, PATH_SIZE) >= 0 &&
            plat_file_info(target, &info) == AD_OK && info.dir) {
            if (plat_delete_file(target) == AD_OK) {
                stats->deleted++;
                say("Deleted", target, NULL);
            } else {
                stats->kept++;
                say("Kept", target, "not empty");
            }
        }
    } else if (state_local_path(st, e, cfg->local, target, PATH_SIZE) >= 0 &&
               plat_file_info(target, &info) == AD_OK && !info.dir) {
        if (info.size != e->size || info.date != e->ltime) {
            stats->kept++;
            say("Kept", target, "changed on the Amiga");
        } else if (plat_delete_file(target) == AD_OK) {
            stats->deleted++;
            say("Deleted", target, NULL);
        } else {
            fail(target, "cannot delete");
        }
    }
    state_remove(st, id);
    dirty = 1;
}

/* ---- folders and files ---------------------------------------------------------------- */

/* Rename / move the local copy of old to target when its place changed. */
static int move_old(const StEntry *old, int want_dir)
{
    PlatInfo info;

    if (state_local_path(st, old, cfg->local, oldp, PATH_SIZE) < 0 || strcmp(oldp, target) == 0 ||
        plat_file_info(oldp, &info) != AD_OK || info.dir != want_dir)
        return AD_OK;                   /* nothing there to move */
    if (plat_file_info(target, &info) == AD_OK) {
        /* the place is taken by something not synchronised */
        if (want_dir || info.dir || keep_local(target, lname) != AD_OK) {
            fail(oldp, "cannot move, the new name is taken");
            return AD_ERR;
        }
    }
    if (plat_rename(oldp, target) != AD_OK) {
        fail(oldp, "cannot move");
        return AD_ERR;
    }
    stats->moved++;
    say("Moved to", target, NULL);
    return AD_OK;
}

static int do_folder(const GraphItem *it, StEntry *old)
{
    PlatInfo info;
    int rc;

    if ((rc = parent_dir(it->parent_id)) != AD_OK)
        return rc;
    if (local_target(it) != AD_OK || (old && move_old(old, 1) != AD_OK))
        return AD_OK;
    if (plat_file_info(target, &info) != AD_OK) {
        if (plat_make_dir(target) != AD_OK) {
            fail(target, "cannot create the directory");
            return AD_OK;
        }
        stats->folders++;
        say("Created", target, NULL);
    } else if (!info.dir) {
        fail(target, "a file of this name is in the way");
        return AD_OK;
    }
    return put_item(it, 0);
}

static int sink(void *ud, const void *data, long len)
{
    (void)ud;
    return plat_write(out_file, data, len);
}

/* Download the item into tmp. AD_OK, AD_ERR (item failed) or a fatal code. */
static int download(const GraphItem *it, unsigned long *got)
{
    const char *tok;
    int rc = AD_ERR_AUTH, attempt;

    for (attempt = 0; attempt < 2 && rc == AD_ERR_AUTH; attempt++) {
        if (!(tok = token(attempt > 0, &rc)))
            return rc;
        if (!(out_file = plat_create(tmp))) {
            fail(tmp, "cannot write");
            return AD_ERR;
        }
        rc = graph_download(cfg->graph, cfg->dl, tok, it->id, sink, NULL, got, ierr, (int)sizeof(ierr));
        if (rc != AD_OK)
            plat_close(out_file, 0);
    }
    if (rc == AD_OK && plat_close(out_file, 1) != AD_OK) {
        fail(target, "cannot write");
        return AD_ERR;
    }
    if (rc != AD_OK) {
        if (fatal(rc))
            return rc;
        fail(target, ierr);
        return AD_ERR;
    }
    if (*got != it->size) {
        plat_delete_file(tmp);
        fail(target, "download incomplete");
        return AD_ERR;
    }
    return AD_OK;
}

static int do_file(const GraphItem *it, StEntry *old)
{
    unsigned long lt = it->mtime ? tz_to_local(cfg->tz, it->mtime) : 0, got = 0;
    PlatInfo info;
    int rc, have;

    if (it->flags & GI_TOOBIG) {
        stats->skipped++;
        say("Skipped", show(it->name), "4 GB or larger");
        return AD_OK;
    }
    if ((rc = parent_dir(it->parent_id)) != AD_OK)
        return rc;
    if (local_target(it) != AD_OK || (old && move_old(old, 0) != AD_OK))
        return AD_OK;
    if (old && it->ctag[0] && strcmp(old->ctag, it->ctag) == 0 && old->size == it->size)
        return put_item(it, old->ltime);             /* same content */

    have = plat_file_info(target, &info) == AD_OK;
    if (have && info.dir) {
        fail(target, "a directory of this name is in the way");
        return AD_OK;
    }
    if (have && lt && info.size == it->size && info.date == lt)
        return put_item(it, info.date);              /* already here (taken over) */
    if (have && (!old || info.size != old->size || info.date != old->ltime) &&
        keep_local(target, lname) != AD_OK)
        return AD_OK;

    if (join(tmp, dir, SYNC_PART_NAME) != AD_OK) {
        fail(target, "path too long");
        return AD_OK;
    }
    rc = download(it, &got);
    if (rc != AD_OK)
        return rc == AD_ERR ? AD_OK : rc;
    if (lt)
        plat_set_date(tmp, lt);
    plat_delete_file(target);           /* the old version (unchanged here), if any */
    if (plat_rename(tmp, target) != AD_OK) {
        plat_delete_file(tmp);
        fail(target, "cannot replace");
        return AD_OK;
    }
    if (plat_file_info(target, &info) == AD_OK)
        lt = info.date;
    stats->downloaded++;
    stats->bytes += got;
    sprintf(ierr, "%lu bytes", got);
    say("Downloaded", target, ierr);
    return put_item(it, lt);
}

/* ---- items ------------------------------------------------------------------------------ */

/* names the client uses itself */
static int reserved(const GraphItem *it)
{
    return eq_ci(it->name, SYNC_PART_NAME) ||
           (eq_ci(it->name, SYNC_STATE_DIR) && strcmp(it->parent_id, st->root_id) == 0);
}

/* AD_OK, SYNC_PENDING or a fatal code. */
static int apply_item(const GraphItem *it)
{
    StEntry *old;

    if ((it->flags & GI_ROOT) || strcmp(it->id, st->root_id) == 0)
        return AD_OK;
    old = state_find(st, it->id);
    if (it->flags & GI_DELETED) {
        if (old)
            do_delete(old);
        return AD_OK;
    }
    if (old && stats->full)
        old->flags |= ST_SEEN;
    if ((it->flags & (GI_PACKAGE | GI_REMOTE | GI_NAMECUT)) || !(it->flags & (GI_FILE | GI_FOLDER)) ||
        reserved(it)) {
        stats->skipped++;
        say("Skipped", show(it->name), it->flags & GI_PACKAGE ? "OneNote notebook" :
            it->flags & GI_REMOTE ? "shared from another drive" :
            it->flags & GI_NAMECUT ? "name too long" : "not a file");
        return AD_OK;
    }
    return (it->flags & GI_FOLDER) ? do_folder(it, old) : do_file(it, old);
}

static void drop_pending(const char *id)
{
    Pending **pp = &pending;

    while (*pp) {
        if (strcmp((*pp)->it.id, id) == 0) {
            Pending *p = *pp;
            *pp = p->next;
            free(p);
        } else {
            pp = &(*pp)->next;
        }
    }
}

/* graph_page callback */
static int apply(void *ud, const GraphItem *it)
{
    Pending *p, **pp;
    int rc;

    (void)ud;
    if (plat_break())
        return AD_ERR_BREAK;
    drop_pending(it->id);               /* the last occurrence of an item counts */
    rc = apply_item(it);
    if (rc != SYNC_PENDING) {
        if (rc == AD_OK)
            maybe_save(0);
        return rc;
    }
    if (!(p = malloc(sizeof(*p)))) {
        strcpy(ierr, "out of memory");
        return AD_ERR_NOMEM;
    }
    p->it = *it;
    p->next = NULL;
    for (pp = &pending; *pp; pp = &(*pp)->next)
        ;
    *pp = p;
    return AD_OK;
}

/* Apply waiting items whose parent has arrived, until nothing moves. */
static int run_pending(void)
{
    int progress = 1, rc;

    while (progress) {
        Pending **pp = &pending;
        progress = 0;
        while (*pp) {
            Pending *p = *pp;
            rc = apply_item(&p->it);
            if (rc == SYNC_PENDING) {
                pp = &p->next;
                continue;
            }
            *pp = p->next;
            free(p);
            if (rc != AD_OK)
                return rc;
            progress = 1;
        }
    }
    return AD_OK;
}

static void free_pending(int report)
{
    while (pending) {
        Pending *p = pending;
        if (report)
            fail(show(p->it.name), "its folder is not in the synchronised folder");
        pending = p->next;
        free(p);
    }
}

/* After a full listing: entries not listed are gone from the cloud. */
static void delete_unseen(void)
{
    StEntry *e;

    for (;;) {
        for (e = state_next(st, NULL); e && (e->flags & ST_SEEN); e = state_next(st, e))
            ;
        if (!e)
            break;
        do_delete(e);
    }
    for (e = state_next(st, NULL); e; e = state_next(st, e))
        e->flags &= ~(unsigned)ST_SEEN;
}

/* ---- the pass ------------------------------------------------------------------------- */

static int find_root(GraphItem *root)
{
    const char *tok;
    int rc = AD_ERR_AUTH, attempt;

    for (attempt = 0; attempt < 2 && rc == AD_ERR_AUTH; attempt++) {
        if (!(tok = token(attempt > 0, &rc)))
            return rc;
        rc = graph_item(cfg->graph, tok, cfg->remote, root, ierr, (int)sizeof(ierr));
    }
    if (rc == AD_ERR_NOTFOUND)
        sprintf(ierr, "OneDrive folder %.150s not found", show(cfg->remote));
    else if (rc == AD_OK && !(root->flags & (GI_FOLDER | GI_ROOT))) {
        sprintf(ierr, "%.150s on OneDrive is not a folder", show(cfg->remote));
        rc = AD_ERR_NOTFOUND;
    }
    return rc;
}

static int read_changes(char *link, int size)
{
    const char *tok;
    int rc = AD_OK, attempt, done = 0;
    StEntry *e;

    if (st->delta[0]) {
        strcpy(link, st->delta);
    } else {
        if (graph_delta_link(cfg->remote, link, size) != AD_OK) {
            strcpy(ierr, "OneDrive folder path too long");
            return AD_ERR_TOOBIG;
        }
        stats->full = 1;
        for (e = state_next(st, NULL); e; e = state_next(st, e))
            e->flags &= ~(unsigned)ST_SEEN;
    }
    while (!done) {
        if (plat_break()) {
            strcpy(ierr, "interrupted");
            return AD_ERR_BREAK;
        }
        rc = AD_ERR_AUTH;
        for (attempt = 0; attempt < 2 && rc == AD_ERR_AUTH; attempt++) {
            if (!(tok = token(attempt > 0, &rc)))
                return rc;
            rc = graph_page(cfg->graph, tok, link, size, &done, apply, NULL, ierr, (int)sizeof(ierr));
        }
        if (rc == AD_OK)
            rc = run_pending();
        if (rc != AD_OK)
            return rc;
        maybe_save(0);
    }
    return AD_OK;
}

int sync_run(const SyncConfig *c, State *s, SyncStats *ss, char *err, int errsize)
{
    static GraphItem root;
    static char link[GRAPH_LINK_SIZE];
    char *slash, names[16];
    int rc;

    cfg = c;
    st = s;
    stats = ss;
    memset(stats, 0, sizeof(*stats));
    pending = NULL;
    dirty = 0;
    last_save = plat_now();
    ierr[0] = '\0';

    /* the local directory and the one for the state file */
    if (plat_make_dir(cfg->local) != AD_OK) {
        sprintf(ierr, "cannot create %.150s", cfg->local);
        rc = AD_ERR;
        goto out;
    }
    if (strlen(cfg->state_file) < sizeof(tmp)) {
        strcpy(tmp, cfg->state_file);
        if ((slash = strrchr(tmp, '/')) != NULL) {
            *slash = '\0';
            plat_make_dir(tmp);
        }
    }

    if ((rc = find_root(&root)) != AD_OK)
        goto out;
    if (strcmp(st->root_id, root.id) != 0) {      /* first pass, or another folder */
        state_free(st);
        strcpy(st->root_id, root.id);
        st->delta[0] = '\0';
        dirty = 1;
    }
    /* another code page or name length: list the whole folder once, every item is then
     * renamed to its new local name (no download) */
    sprintf(names, "%s/%d", cfg->charset == FS_LATIN2 ? "LATIN2" : "LATIN1", cfg->maxname);
    if (strcmp(st->names, names) != 0) {
        if (st->delta[0])
            say("Renaming local files:", names, "the code page or name length changed");
        strcpy(st->names, names);
        st->delta[0] = '\0';
        dirty = 1;
    }

    rc = read_changes(link, (int)sizeof(link));
    if (rc == AD_ERR_GONE && st->delta[0]) {
        say("Reading the whole folder again:", show(cfg->remote), "the list of changes expired");
        free_pending(0);
        st->delta[0] = '\0';
        dirty = 1;
        rc = read_changes(link, (int)sizeof(link));
    }
    if (rc == AD_OK) {
        free_pending(1);
        if (stats->full)
            delete_unseen();
        if (stats->failed == 0) {
            strcpy(st->delta, link);
            dirty = 1;
        }
    }
    free_pending(0);
    maybe_save(1);

out:
    if (rc != AD_OK) {
        if (rc == AD_ERR_BREAK)
            strcpy(ierr, "interrupted");
        strncpy(err, ierr, (size_t)errsize - 1);
        err[errsize - 1] = '\0';
    }
    return rc;
}
