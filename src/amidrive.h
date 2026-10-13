/* AmiDrive - OneDrive client for AmigaOS: common definitions.
 *
 * Everything included from here must stay portable (no Amiga headers), so the
 * logic modules can be tested on the PC (Makefile.host).
 */
#ifndef AMIDRIVE_H
#define AMIDRIVE_H

#define AMIDRIVE_NAME     "AmiDrive"
#define AMIDRIVE_VERSION  "0.1.0"
#define AMIDRIVE_VER_NUM  0
#define AMIDRIVE_REV_NUM  1
#define AMIDRIVE_DATE     "13.10.2026"

/* Result codes of the portable modules (0 = success, negative = error). */
enum {
    AD_OK          =  0,
    AD_ERR         = -1,   /* generic failure */
    AD_ERR_NOMEM   = -2,
    AD_ERR_NET     = -3,   /* connection lost, timeout, socket error */
    AD_ERR_TLS     = -4,   /* handshake or certificate failure */
    AD_ERR_PROTO   = -5,   /* malformed HTTP / JSON */
    AD_ERR_TOOBIG  = -6,   /* value does not fit the supplied buffer */
    AD_ERR_BREAK   = -7,   /* Ctrl-C */
    AD_ERR_AUTH    = -8,   /* not signed in / sign-in expired: run LOGIN again */
    AD_ERR_HTTP    = -9,   /* unexpected HTTP status (details in the error text) */
    AD_ERR_NOTFOUND = -10, /* the item or folder does not exist (HTTP 404) */
    AD_ERR_GONE    = -11   /* delta link expired (HTTP 410): start again with a full delta */
};

/* Application (client) ID of the AmiDrive registration in Microsoft Entra ID
 * (tenant cslabsc.onmicrosoft.com, 14.10.2026): public client, personal and work
 * accounts, delegated Files.ReadWrite + offline_access. Not a secret.
 * An organisation with its own registration overrides it with CLIENTID=. */
#ifndef AMIDRIVE_CLIENT_ID
#define AMIDRIVE_CLIENT_ID "0474e444-3266-446b-b49c-d47a01cadf56"
#endif

/* Directory for sign-in: "common" (personal + work accounts), "consumers", "organizations". */
#define AMIDRIVE_TENANT    "common"

#define AD_LOGIN_HOST "login.microsoftonline.com"
#define AD_GRAPH_HOST "graph.microsoft.com"

#endif
