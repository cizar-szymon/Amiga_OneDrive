/* Synchronisation, stage 1: changes in the OneDrive folder -> the Amiga directory.
 * Portable (Graph over sessions, files through plat.h).
 *
 * One pass: find the folder, read its delta (the whole folder the first time or after
 * the delta link expired), and apply every item at once:
 *   - folders are created, renamed and moved (their contents move along);
 *   - files are downloaded to "<dir>/.AmiDrive.part", dated and swapped in;
 *     a rename or move without new content is only a local rename;
 *   - deleted items are deleted, folders recursively.
 * Local files are never lost: a file changed on the Amiga since the last pass is kept
 * (renamed to "name-Amiga.ext" when a new version arrives, left in place when the
 * item was deleted). A local file equal to the cloud one (size and date) is taken
 * over without a download.
 *
 * The new delta link is kept only when every item was applied; otherwise the next
 * pass reads the same changes again (applying them twice does no harm). The state is
 * saved during the pass now and then, and at the end.
 */
#ifndef SYNC_H
#define SYNC_H

#include "fsmap.h"
#include "session.h"
#include "state.h"
#include "tz.h"

#define SYNC_PART_NAME ".AmiDrive.part"   /* download in progress, in each directory */
#define SYNC_STATE_DIR ".AmiDrive"        /* in the synchronised directory: skipped */
#define SYNC_SAVE_SECS 30                 /* save the state at most this often while busy */
#define SYNC_DELETE_GUARD 10              /* more items missing here than this, and more than
                                             half of all: nothing is deleted on OneDrive */

/* Progress of a pass, for a status window. */
enum { SYNC_CLOUD = 1,           /* changes from OneDrive (downloads) */
       SYNC_SCAN,                /* looking through the Amiga directory */
       SYNC_SEND };              /* sending the changes made on the Amiga */

typedef struct {
    int phase;                   /* SYNC_CLOUD, SYNC_SCAN, SYNC_SEND */
    const char *file;            /* the file being transferred (local path), else NULL */
    unsigned long file_done, file_size;
    long items;                  /* OneDrive items read in this pass */
    long files;                  /* files downloaded and sent in this pass */
    unsigned long done;          /* bytes of the phase handled (transferred or found equal) */
    unsigned long total;         /* bytes of the whole phase, 0 = unknown: known for the
                                    first (whole folder) listing and for sending */
} SyncProgress;

typedef struct {
    Session *graph;              /* graph.microsoft.com */
    Session *dl;                 /* download servers (redirects) */
    const char *local;           /* Amiga directory, e.g. "Work:OneDrive" */
    const char *remote;          /* OneDrive folder, UTF-8, e.g. "/Amiga" */
    const char *state_file;      /* e.g. "Work:OneDrive/.AmiDrive/state" */
    FsCharset charset;
    int maxname;                 /* longest local file name (30 OFS/FFS, 107 others) */
    const Tz *tz;                /* for file dates */
    int upload;                  /* stage 2: send the changes made on the Amiga */

    /* A valid access token; force = 1 after it was rejected. NULL on failure, with a
     * message in err and the code in *rc (AD_ERR_AUTH: sign in again). */
    const char *(*token)(void *ud, int force, int *rc, char *err, int errsize);
    /* One line for the user / the log file (Amiga code page, no '\n'). */
    void (*log)(void *ud, const char *msg);
    /* NULL or called often (each piece of data, each item): show the progress. */
    void (*progress)(void *ud, const SyncProgress *p);
    void *ud;
} SyncConfig;

typedef struct {
    long downloaded, moved, deleted, folders;
    long kept;                   /* local files kept because they were changed here */
    long skipped;                /* not synchronised: notebooks, shared, >= 4 GB, ... */
    long failed;                 /* will be tried again in the next pass */
    unsigned long bytes;
    long uploaded;               /* files sent to OneDrive (new or new versions) */
    long remote_moved, remote_deleted, remote_folders;
    unsigned long up_bytes;
    int full;                    /* the whole folder was listed */
} SyncStats;

/* One pass. st is the state in memory (loaded with state_load before the first pass;
 * a state of another folder is dropped). Returns AD_OK (also when single items
 * failed: see stats->failed), AD_ERR_BREAK, AD_ERR_AUTH, AD_ERR_NOTFOUND (no such
 * OneDrive folder), AD_ERR_NET ... with a message in err. */
int sync_run(const SyncConfig *cfg, State *st, SyncStats *stats, char *err, int errsize);

#endif
