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
static SyncProgress prog;
static unsigned long prog_base;  /* prog.done before the current file */
static char msg[PATH_SIZE + 200];
static char dir[PATH_SIZE], target[PATH_SIZE], oldp[PATH_SIZE], tmp[PATH_SIZE], alt[PATH_SIZE];
static char lname[FS_MAXNAME_LONG + 8];
static char shown[256];

/* ---- messages ------------------------------------------------------------------------- */

static void report(void)
{
    if (cfg->progress)
        cfg->progress(cfg->ud, &prog);
}

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
    prog.file_done += (unsigned long)len;
    prog.done = prog_base + prog.file_done;
    report();
    return plat_write(out_file, data, len);
}

/* Download the item into tmp. AD_OK, AD_ERR (item failed) or a fatal code. */
static int download(const GraphItem *it, unsigned long *got)
{
    const char *tok;
    int rc = AD_ERR_AUTH, attempt;

    prog.file = target;
    prog.file_size = it->size;
    for (attempt = 0; attempt < 2 && rc == AD_ERR_AUTH; attempt++) {
        prog.file_done = 0;
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
    prog.files++;
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
    int rc;

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
    if (it->flags & GI_FOLDER)
        return do_folder(it, old);
    prog_base = prog.done;
    rc = do_file(it, old);
    prog.file = NULL;
    if (rc != SYNC_PENDING && !(it->flags & GI_TOOBIG))
        prog.done = prog_base + it->size;   /* downloaded, equal or kept: handled */
    else
        prog.done = prog_base;
    report();
    return rc;
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
    prog.items++;
    report();
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

/* ---- stage 2: changes on the Amiga -> OneDrive ----------------------------------------
 *
 * After the cloud changes were applied, the local directory is scanned and compared
 * with the state: new files and folders are created on OneDrive, changed files are
 * sent as new versions (only while the cloud version is still the one we know), a
 * new file with the size and date of a missing one is a rename / move, and items
 * missing here are deleted there (into the recycle bin). */

#define ST_LOCAL 0x200           /* found by the local scan (in memory only) */

/* a new or changed local item, in scan order (folders before their contents) */
typedef struct NewItem {
    struct NewItem *next;
    struct NewItem *pdir;        /* a new parent folder, or NULL: parent_id */
    char *path, *lname;
    char *parent_id;             /* remote id of the parent when it is known */
    char *mod_id;                /* a changed file: its entry; NULL: a new item */
    unsigned long size, date;
    int dir, skip;
    char id[GRAPH_ID_SIZE];      /* a new folder, once made on OneDrive */
} NewItem;

static NewItem *items_head, **items_tail;
static char spath[PATH_SIZE], sname[FS_MAXNAME_LONG + 8];
static char rname[GRAPH_NAME_SIZE];
static GraphItem up_item;
static char *up_buf;
static PlatFile *in_file;
static int scan_depth;
static char note[100];

/* call with a valid token, again with a fresh one when it was rejected */
#define WITH_TOKEN(expr) \
    for (attempt = 0, rc = AD_ERR_AUTH; attempt < 2 && rc == AD_ERR_AUTH; attempt++) { \
        if (!(tok = token(attempt > 0, &rc))) \
            break; \
        rc = (expr); \
    }

static char *dup_str(const char *s)
{
    char *p = s ? malloc(strlen(s) + 1) : NULL;

    if (p)
        strcpy(p, s);
    return p;
}

static void free_item(NewItem *n)
{
    free(n->path);
    free(n->lname);
    free(n->parent_id);
    free(n->mod_id);
    free(n);
}

static void free_items(void)
{
    while (items_head) {
        NewItem *n = items_head;
        items_head = n->next;
        free_item(n);
    }
    items_tail = &items_head;
}

/* spath / sname -> a new list item */
static NewItem *add_item(int dir, const PlatInfo *info, const char *parent_id, NewItem *pdir,
                         const char *mod_id)
{
    NewItem *n = malloc(sizeof(*n));

    if (!n)
        return NULL;
    memset(n, 0, sizeof(*n));
    n->dir = dir;
    n->size = info->size;
    n->date = info->date;
    n->pdir = pdir;
    n->path = dup_str(spath);
    n->lname = dup_str(sname);
    n->parent_id = dup_str(parent_id);
    n->mod_id = dup_str(mod_id);
    if (!n->path || !n->lname || (parent_id && !n->parent_id) || (mod_id && !n->mod_id)) {
        free_item(n);
        return NULL;
    }
    *items_tail = n;
    items_tail = &n->next;
    return n;
}

/* Scan the directory spath: its remote id is dir_id, or it is the new folder pdir.
 * Entries found get ST_LOCAL; new and changed items go to the list. Any failure
 * makes the scan incomplete (AD_ERR): then nothing may be taken as missing. */
static int scan_dir(const char *dir_id, NewItem *pdir)
{
    size_t len = strlen(spath);
    int sep = len > 0 && spath[len - 1] != ':' && spath[len - 1] != '/';
    PlatInfo info;
    PlatDir *d;
    int r, rc = AD_OK;

    if (scan_depth >= ST_MAXDEPTH || !(d = plat_dir_open(spath)))
        return AD_ERR;
    scan_depth++;
    while ((r = plat_dir_next(d, sname, (int)sizeof(sname), &info)) == 1) {
        StEntry *e = dir_id ? state_find_name(st, dir_id, sname) : NULL;
        NewItem *n;

        if (eq_ci(sname, SYNC_PART_NAME) || (scan_depth == 1 && eq_ci(sname, SYNC_STATE_DIR)))
            continue;
        if (e && !(e->flags & ST_FOLDER) != !info.dir)
            e = NULL;                   /* a file became a folder or the other way */
        if (e)
            e->flags |= ST_LOCAL;
        if (len + (size_t)sep + strlen(sname) >= PATH_SIZE) {
            rc = AD_ERR;
            break;
        }
        if (sep)
            spath[len] = '/';
        strcpy(spath + len + sep, sname);

        if (e && info.dir) {
            rc = scan_dir(e->id, NULL);
        } else if (e) {
            if ((info.size != e->size || info.date != e->ltime) &&
                !add_item(0, &info, e->parent, NULL, e->id))
                rc = AD_ERR_NOMEM;
        } else if (!(n = add_item(info.dir, &info, dir_id, pdir, NULL))) {
            rc = AD_ERR_NOMEM;
        } else if (info.dir) {
            rc = scan_dir(NULL, n);
        }
        spath[len] = '\0';
        if (rc != AD_OK)
            break;
    }
    plat_dir_close(d);
    scan_depth--;
    return rc == AD_OK && r < 0 ? AD_ERR : rc;
}

static long source(void *ud, void *buf, long len)
{
    long n;

    (void)ud;
    n = plat_read(in_file, buf, len);
    if (n > 0) {
        prog.file_done += (unsigned long)n;
        prog.done = prog_base + prog.file_done;
        report();
    }
    return n;
}

/* entry for an item made / changed on OneDrive, with its local name */
static int put_up(GraphItem *it, const char *parent, const char *ln, unsigned long ltime)
{
    StEntry *e;
    int rc;

    if (!it->parent_id[0])
        strcpy(it->parent_id, parent);
    strcpy(lname, ln);
    if ((rc = put_item(it, ltime)) == AD_OK && (e = state_find(st, it->id)) != NULL)
        e->flags |= ST_LOCAL;
    return rc;
}

/* Send the file n->path (new: name in parent; else a new version of id while it has
 * etag). AD_OK, AD_ERR (item failed, message in ierr) or a fatal code. */
static int upload(NewItem *n, const char *parent, const char *name, const char *id,
                  const char *etag)
{
    PlatInfo info;
    const char *tok;
    int attempt, rc;

    if (plat_file_info(n->path, &info) != AD_OK || info.dir)
        return SYNC_PENDING;            /* gone since the scan */
    n->size = info.size;
    n->date = info.date;
    if (!up_buf && n->size > 0 && !(up_buf = malloc(GRAPH_CHUNK))) {
        strcpy(ierr, "out of memory");
        return AD_ERR_NOMEM;
    }
    for (attempt = 0, rc = AD_ERR_AUTH; attempt < 2 && rc == AD_ERR_AUTH; attempt++) {
        if (!(tok = token(attempt > 0, &rc)))
            break;
        if (!(in_file = plat_open_read(n->path))) {
            strcpy(ierr, "cannot read the file");
            return AD_ERR;
        }
        prog.file = n->path;
        prog.file_size = n->size;
        prog.file_done = 0;
        rc = graph_upload(cfg->graph, cfg->dl, tok, parent, name, id, etag, n->size,
                          tz_to_utc(cfg->tz, n->date), source, NULL, up_buf, GRAPH_CHUNK,
                          &up_item, ierr, (int)sizeof(ierr));
        plat_close(in_file, 0);
        in_file = NULL;
    }
    prog.file = NULL;
    if (rc == AD_OK)
        prog.files++;
    return rc;
}

/* a file missing here with this size and date: the item was renamed / moved */
static StEntry *moved_from(const PlatInfo *info)
{
    StEntry *e;

    for (e = state_next(st, NULL); e; e = state_next(st, e))
        if (!(e->flags & (ST_LOCAL | ST_FOLDER)) && e->size == info->size && e->ltime == info->date)
            return e;
    return NULL;
}

static int push_new(NewItem *n)
{
    const char *parent = n->pdir ? n->pdir->id : n->parent_id, *tok, *problem;
    PlatInfo info;
    StEntry *m;
    int attempt, rc;

    if (n->pdir && n->pdir->skip) {
        n->skip = 1;                    /* its folder was not made: already reported */
        return AD_OK;
    }
    if (!parent || !*parent) {
        n->skip = 1;
        fail(n->path, "its folder is not on OneDrive");
        return AD_OK;
    }
    problem = fs_to_utf8(cfg->charset, n->lname, rname, sizeof(rname)) < 0 ? "name too long" :
              fs_remote_name_problem(rname);
    if (problem) {
        n->skip = 1;
        stats->skipped++;
        say("Not sent", n->path, problem);
        return AD_OK;
    }

    if (n->dir) {
        WITH_TOKEN(graph_create_folder(cfg->graph, tok, parent, rname, &up_item, ierr, (int)sizeof(ierr)));
        if (rc == AD_ERR_CONFLICT) {    /* made meanwhile: use it */
            WITH_TOKEN(graph_child(cfg->graph, tok, parent, rname, &up_item, ierr, (int)sizeof(ierr)));
            if (rc == AD_OK && !(up_item.flags & GI_FOLDER)) {
                strcpy(ierr, "a file of this name is on OneDrive");
                rc = AD_ERR_CONFLICT;
            }
        }
        if (rc == AD_OK) {
            strcpy(n->id, up_item.id);
            stats->remote_folders++;
            say("Created on OneDrive", n->path, NULL);
            return put_up(&up_item, parent, n->lname, 0);
        }
        n->skip = 1;
        if (fatal(rc))
            return rc;
        fail(n->path, ierr);
        return AD_OK;
    }

    /* the same size and date as a file missing here: rename / move it there */
    if (plat_file_info(n->path, &info) == AD_OK && (m = moved_from(&info)) != NULL) {
        char id[GRAPH_ID_SIZE];
        int new_name = strcmp(m->name, rname) != 0, new_parent = strcmp(m->parent, parent) != 0;

        strcpy(id, m->id);
        WITH_TOKEN(graph_move(cfg->graph, tok, id, new_parent ? parent : NULL,
                              new_name ? rname : NULL, &up_item, ierr, (int)sizeof(ierr)));
        if (rc == AD_OK) {
            stats->remote_moved++;
            say("Moved on OneDrive to", n->path, NULL);
            return put_up(&up_item, parent, n->lname, info.date);
        }
        if (fatal(rc))
            return rc;                  /* else: send it as a new file */
    }

    rc = upload(n, parent, rname, NULL, NULL);
    if (rc == AD_OK) {
        stats->uploaded++;
        stats->up_bytes += n->size;
        sprintf(note, "%lu bytes", n->size);
        say("Uploaded", n->path, note);
        return put_up(&up_item, parent, n->lname, n->date);
    }
    if (rc == SYNC_PENDING || fatal(rc))
        return rc == SYNC_PENDING ? AD_OK : rc;
    fail(n->path, ierr);
    return AD_OK;
}

static int push_changed(NewItem *n)
{
    char id[GRAPH_ID_SIZE], etag[GRAPH_TAG_SIZE], parent[GRAPH_ID_SIZE], ln[FS_MAXNAME_LONG + 8];
    StEntry *e = state_find(st, n->mod_id);
    int rc;

    if (!e)
        return AD_OK;
    strcpy(id, e->id);
    strcpy(etag, e->etag);
    strcpy(parent, e->parent);
    strcpy(ln, e->lname);
    rc = upload(n, parent, NULL, id, etag);
    if (rc == AD_OK) {
        stats->uploaded++;
        stats->up_bytes += n->size;
        sprintf(note, "%lu bytes", n->size);
        say("Uploaded new version", n->path, note);
        return put_up(&up_item, parent, ln, n->date);
    }
    if (rc == SYNC_PENDING || fatal(rc))
        return rc == SYNC_PENDING ? AD_OK : rc;
    fail(n->path, rc == AD_ERR_CONFLICT ? "changed on OneDrive too, sorted out in the next pass" : ierr);
    return AD_OK;
}

/* forget an entry and everything below it */
static void forget(const char *id)
{
    char cid[GRAPH_ID_SIZE];
    StEntry *c;

    while ((c = find_child(id)) != NULL) {
        strcpy(cid, c->id);
        forget(cid);
    }
    state_remove(st, id);
    dirty = 1;
}

static void mark_local(const char *id)
{
    StEntry *e;

    for (e = state_next(st, NULL); e; e = state_next(st, e))
        if (strcmp(e->parent, id) == 0 && !(e->flags & ST_LOCAL)) {
            e->flags |= ST_LOCAL;
            mark_local(e->id);
        }
}

/* the topmost entry missing here (its parent is not missing too), or NULL */
static StEntry *next_missing(void)
{
    StEntry *e, *p;

    for (e = state_next(st, NULL); e; e = state_next(st, e))
        if (!(e->flags & ST_LOCAL) &&
            (strcmp(e->parent, st->root_id) == 0 || !(p = state_find(st, e->parent)) ||
             (p->flags & ST_LOCAL)))
            return e;
    return NULL;
}

static int push_deletes(void)
{
    char id[GRAPH_ID_SIZE], etag[GRAPH_TAG_SIZE];
    const char *tok;
    StEntry *e;
    int attempt, rc;

    while ((e = next_missing()) != NULL) {
        strcpy(id, e->id);
        strcpy(etag, e->etag);
        if (state_local_path(st, e, cfg->local, target, PATH_SIZE) < 0)
            strcpy(target, show(e->name));
        WITH_TOKEN(graph_delete(cfg->graph, tok, id, etag, ierr, (int)sizeof(ierr)));
        if (rc == AD_OK) {
            stats->remote_deleted++;
            say("Deleted on OneDrive", target, NULL);
            forget(id);
            continue;
        }
        if (fatal(rc))
            return rc;
        fail(target, rc == AD_ERR_CONFLICT ? "changed on OneDrive, not deleted" : ierr);
        e = state_find(st, id);
        e->flags |= ST_LOCAL;           /* leave it and what is below it */
        mark_local(id);
    }
    return AD_OK;
}

static int push_changes(void)
{
    StEntry *e;
    NewItem *n;
    long missing = 0;
    int rc, counted;

    items_head = NULL;
    items_tail = &items_head;
    for (e = state_next(st, NULL); e; e = state_next(st, e))
        e->flags &= ~(unsigned)ST_LOCAL;
    if (strlen(cfg->local) >= PATH_SIZE)
        return AD_OK;
    strcpy(spath, cfg->local);
    scan_depth = 0;
    prog.phase = SYNC_SCAN;
    prog.file = NULL;
    report();
    rc = scan_dir(st->root_id, NULL);
    if (rc != AD_OK) {
        free_items();
        if (rc == AD_ERR_NOMEM) {
            strcpy(ierr, "out of memory");
            return rc;
        }
        stats->failed++;
        say("Nothing sent:", cfg->local, "cannot read it completely");
        return AD_OK;
    }

    /* what is to be sent: the files new or changed here (renames turn out on the way) */
    prog.phase = SYNC_SEND;
    prog.done = prog.total = 0;
    for (n = items_head; n; n = n->next)
        if (!n->dir && !n->skip)
            prog.total += n->size;
    report();
    for (n = items_head; n && rc == AD_OK; n = n->next) {
        if (plat_break()) {
            rc = AD_ERR_BREAK;
            break;
        }
        prog_base = prog.done;
        counted = !n->mod_id && !n->dir && !n->skip;    /* as summed above */
        rc = n->mod_id ? AD_OK : push_new(n);
        if (counted)
            prog.done = prog_base + n->size;            /* sent, renamed or failed */
        report();
        maybe_save(0);
    }
    for (n = items_head; n && rc == AD_OK; n = n->next) {
        if (plat_break()) {
            rc = AD_ERR_BREAK;
            break;
        }
        prog_base = prog.done;
        counted = n->mod_id && !n->skip;
        rc = n->mod_id ? push_changed(n) : AD_OK;
        if (counted)
            prog.done = prog_base + n->size;
        report();
        maybe_save(0);
    }

    if (rc == AD_OK) {
        for (e = state_next(st, NULL); e; e = state_next(st, e))
            missing += !(e->flags & ST_LOCAL);
        if (missing > SYNC_DELETE_GUARD && missing * 2 > st->count) {
            sprintf(note, "%ld of %ld items missing here", missing, st->count);
            say("Warning: nothing deleted on OneDrive,", note,
                "is the directory complete? Delete them there by hand if this is right");
        } else {
            rc = push_deletes();
        }
    }
    free_items();
    free(up_buf);
    up_buf = NULL;
    if (rc == AD_ERR_BREAK)
        strcpy(ierr, "interrupted");
    return rc;
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
    memset(&prog, 0, sizeof(prog));
    prog.phase = SYNC_CLOUD;
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

    /* the whole folder is listed: its size is the work to do (folders give the sum) */
    if (!st->delta[0] && !(root.flags & GI_TOOBIG))
        prog.total = root.size;
    report();
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
    if (rc == AD_OK && cfg->upload)
        rc = push_changes();
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
