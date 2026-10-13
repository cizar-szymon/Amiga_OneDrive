/* Sync state: what is known about every item of the synchronised folder, and the
 * delta link for the next check. Portable.
 *
 * Items are kept by OneDrive id with the id of their parent (delta answers carry no
 * paths, and renaming a folder does not list its contents again), so paths are built
 * by walking up the parents to the synchronised folder (root_id).
 *
 * The file (<LOCAL>/.AmiDrive/state) is text, one line per item, written to
 * "<file>.new" and swapped in, with an end line to detect a cut file.
 */
#ifndef STATE_H
#define STATE_H

#include "graph.h"

#define ST_BUCKETS 512
#define ST_MAXDEPTH 64              /* folder levels below the synchronised folder */

/* StEntry.flags */
#define ST_FOLDER   0x01

typedef struct StEntry {
    struct StEntry *next;           /* hash chain */
    unsigned long size;
    unsigned long mtime;            /* remote time, UTC seconds since 1978 */
    unsigned long ltime;            /* date of the local file after the last sync
                                       (local seconds since 1978), to see local changes */
    unsigned flags;
    char *id, *parent;
    char *name;                     /* remote name, UTF-8 */
    char *lname;                    /* local name, Amiga code page */
    char *ctag, *etag;
} StEntry;

typedef struct {
    char root_id[GRAPH_ID_SIZE];    /* the synchronised folder */
    char delta[GRAPH_LINK_SIZE];    /* "" = start with a full delta */
    StEntry *bucket[ST_BUCKETS];
    long count;
} State;

void state_init(State *s);
void state_free(State *s);           /* all entries; root_id and delta are kept */

StEntry *state_find(const State *s, const char *id);

/* Store a copy of e (next is ignored; NULL strings are stored as ""), replacing an
 * entry with the same id. Returns the stored entry or NULL (out of memory). */
StEntry *state_put(State *s, const StEntry *e);

/* Returns AD_OK or AD_ERR (no such id). */
int state_remove(State *s, const char *id);

/* All entries in no particular order: e = state_next(s, NULL); e; e = state_next(s, e).
 * Do not put or remove while walking. */
StEntry *state_next(const State *s, const StEntry *e);

/* Path of an entry: base + names of the folders from the synchronised folder down
 * ("Work:OneDrive" + "Games/Lha.lha", or remote "/Amiga" + "/Games/Lha.lha").
 * The local path uses lname, the remote one name. Returns the length, AD_ERR when
 * the chain of parents does not reach root_id, or AD_ERR_TOOBIG. */
int state_local_path(const State *s, const StEntry *e, const char *base, char *out, int size);
int state_remote_path(const State *s, const StEntry *e, const char *base, char *out, int size);

/* Load / save the state file. Load returns AD_OK, AD_ERR (no file: start afresh),
 * AD_ERR_PROTO (damaged: the state is left empty, start afresh) or AD_ERR_NOMEM.
 * Save returns AD_OK or AD_ERR (the old file is kept). */
int state_load(State *s, const char *path);
int state_save(const State *s, const char *path);

#endif
