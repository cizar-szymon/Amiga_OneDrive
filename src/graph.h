/* Microsoft Graph calls (graph.microsoft.com/v1.0). Portable, over a Session.
 *
 * Folder listings (delta, children) are read one page at a time:
 *
 *   graph_delta_link(remote, link, sizeof link);       first full delta of the folder
 *   do {                                               (or the saved deltaLink)
 *       rc = graph_page(g, token, link, sizeof link, &done, item_fn, ud, err, errsize);
 *   } while (rc == AD_OK && !done);                    link = new deltaLink: save it
 *
 * Delta results carry no paths: items are tracked by id and parent id.
 *
 * Every call waits and retries when OneDrive is busy (429 / 503 with Retry-After,
 * other 5xx); Ctrl-C during the wait gives AD_ERR_BREAK. A 401 gives AD_ERR_AUTH:
 * refresh the token and call again with the same arguments.
 */
#ifndef GRAPH_H
#define GRAPH_H

#include "session.h"

typedef struct {
    char owner[128];             /* display name, UTF-8 */
    char drive_type[24];         /* personal, business, documentLibrary */
    unsigned long total_mb;      /* quota in MB (10^6 bytes) */
    unsigned long used_mb;
} DriveInfo;

#define GRAPH_ID_SIZE    80
#define GRAPH_NAME_SIZE  512     /* 255 UTF-16 units of OneDrive, mostly 1-2 bytes each */
#define GRAPH_TAG_SIZE   96
#define GRAPH_LINK_SIZE  2048    /* nextLink / deltaLink */

/* GraphItem.flags */
#define GI_FILE      0x01
#define GI_FOLDER    0x02
#define GI_DELETED   0x04
#define GI_ROOT      0x08        /* the root of the drive */
#define GI_PACKAGE   0x10        /* OneNote notebook and similar: not a plain file */
#define GI_TOOBIG    0x20        /* 4 GB or more: size is not valid */
#define GI_NAMECUT   0x40        /* name longer than GRAPH_NAME_SIZE: do not use */
#define GI_REMOTE    0x80        /* shared from another drive: not synchronised */

typedef struct {
    char id[GRAPH_ID_SIZE];
    char parent_id[GRAPH_ID_SIZE];   /* "" when not given (the root) */
    char name[GRAPH_NAME_SIZE];      /* UTF-8; may be "" for deleted items */
    char etag[GRAPH_TAG_SIZE];       /* changes with any change of the item */
    char ctag[GRAPH_TAG_SIZE];       /* changes with the content only */
    unsigned long size;
    unsigned long mtime;             /* UTC seconds since 1978; 0 = unknown.
                                        fileSystemInfo time (set by the client
                                        that wrote the file), else the server time */
    unsigned flags;
} GraphItem;

/* Called for every item of a page. Return AD_OK to go on, anything else stops the
 * page with that code. May call graph_download(), but no other graph_* function. */
typedef int (*GraphItemFn)(void *ud, const GraphItem *it);

/* Receives downloaded data. Returns AD_OK or AD_ERR_* (stops the download). */
typedef int (*GraphSinkFn)(void *ud, const void *data, long len);

/* GET /me/drive. Returns AD_OK, AD_ERR_AUTH (token rejected: refresh and retry),
 * AD_ERR_HTTP or another AD_ERR_*. */
int graph_drive_info(Session *g, const char *access_token, DriveInfo *di, char *err, int errsize);

/* Item at a path below the drive root ("/Amiga/Games", "" or "/" = the root), UTF-8.
 * Returns AD_OK, AD_ERR_NOTFOUND or another AD_ERR_*. */
int graph_item(Session *g, const char *access_token, const char *remote_path, GraphItem *it,
               char *err, int errsize);

/* First link of a full delta / children listing of a folder. Returns AD_OK or
 * AD_ERR_TOOBIG. */
int graph_delta_link(const char *remote_path, char *link, int size);
int graph_children_link(const char *remote_path, char *link, int size);

/* Read the page at link (a path "/v1.0/..." or a full https://graph.microsoft.com URL)
 * and call fn for each item. Then link holds the next page and *done = 0, or the
 * listing is complete and *done = 1: link holds the deltaLink for the next delta
 * (empty after a children listing). On an error link is unchanged.
 * Returns AD_OK, AD_ERR_GONE (delta link expired: start a full delta),
 * AD_ERR_NOTFOUND (folder gone), AD_ERR_AUTH or another AD_ERR_*. */
int graph_page(Session *g, const char *access_token, char *link, int size, int *done,
               GraphItemFn fn, void *ud, char *err, int errsize);

/* Download the content of a file item. Graph answers with a redirect to a download
 * server; dl is the session used for it (kept alive between downloads). Data goes to
 * sink as it arrives; *got = bytes delivered. A failure after data was delivered
 * leaves a partial result: the caller throws it away. Ctrl-C gives AD_ERR_BREAK. */
int graph_download(Session *g, Session *dl, const char *access_token, const char *item_id,
                   GraphSinkFn sink, void *ud, unsigned long *got, char *err, int errsize);

/* ---- changes (stage 2) -------------------------------------------------------------
 * Items are addressed by id; names are UTF-8. AD_ERR_CONFLICT: the name is taken
 * (409) or the item changed on OneDrive since etag (412). */

#define GRAPH_CHUNK 327680L      /* upload pieces must be multiples of 320 KiB */

/* Reads the next len bytes of the file being uploaded. Returns len (less only at the
 * end of the file) or AD_ERR_*. */
typedef long (*GraphSourceFn)(void *ud, void *buf, long len);

/* Item name in folder parent_id. AD_ERR_NOTFOUND when there is none. */
int graph_child(Session *g, const char *access_token, const char *parent_id, const char *name,
                GraphItem *it, char *err, int errsize);

/* New folder; AD_ERR_CONFLICT when the name is taken. */
int graph_create_folder(Session *g, const char *access_token, const char *parent_id,
                        const char *name, GraphItem *it, char *err, int errsize);

/* Rename and / or move (parent_id or name NULL: unchanged). */
int graph_move(Session *g, const char *access_token, const char *id, const char *parent_id,
               const char *name, GraphItem *it, char *err, int errsize);

/* Delete (to the OneDrive recycle bin) when it still has etag ("" = any). An item
 * that is gone already counts as deleted. */
int graph_delete(Session *g, const char *access_token, const char *id, const char *etag,
                 char *err, int errsize);

/* Upload a file of size bytes with the date mtime (UTC seconds since 1978): a new file
 * name in parent_id (id NULL; fails when the name is taken), or a new version of id
 * while it still has etag. Data comes from src in pieces of up to bufsize bytes
 * (at least GRAPH_CHUNK) through buf, over ul to the upload server. Ctrl-C between
 * pieces gives AD_ERR_BREAK. */
int graph_upload(Session *g, Session *ul, const char *access_token, const char *parent_id,
                 const char *name, const char *id, const char *etag, unsigned long size,
                 unsigned long mtime, GraphSourceFn src, void *ud, char *buf, long bufsize,
                 GraphItem *it, char *err, int errsize);

/* Seconds to wait before retry number attempt (0, 1, ...) when the server gave
 * retry_after (-1 = none). Exposed for the tests. */
long graph_retry_wait(long retry_after, int attempt);

#endif
