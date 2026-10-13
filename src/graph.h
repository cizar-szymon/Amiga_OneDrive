/* Microsoft Graph calls (graph.microsoft.com/v1.0). Portable, over a Session. */
#ifndef GRAPH_H
#define GRAPH_H

#include "session.h"

typedef struct {
    char owner[128];             /* display name, UTF-8 */
    char drive_type[24];         /* personal, business, documentLibrary */
    unsigned long total_mb;      /* quota in MB (10^6 bytes) */
    unsigned long used_mb;
} DriveInfo;

/* GET /me/drive. Returns AD_OK, AD_ERR_AUTH (token rejected: refresh and retry),
 * AD_ERR_HTTP (with Retry-After in g->http.retry_after for 429/503) or AD_ERR_*. */
int graph_drive_info(Session *g, const char *access_token, DriveInfo *di, char *err, int errsize);

#endif
