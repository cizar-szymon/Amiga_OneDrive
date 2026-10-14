/* TCP + TLS connections on the Amiga: bsdsocket.library (Roadshow) and AmiSSL 5.
 * net_connect / net_close / net_read / net_write are declared in net.h. */
#ifndef NET_AMIGA_H
#define NET_AMIGA_H

#include "net.h"

/* Open bsdsocket.library and AmiSSL and create the TLS context (certificates
 * verified against AmiSSL:Certs, TLS 1.2 minimum). err gets a message on failure. */
int  net_init(char *err, int errsize);
void net_cleanup(void);

/* Set by net_connect when the certificate was rejected for its dates (not yet valid or
 * expired): nearly always a wrong Amiga clock. Cleared by each net_connect. */
extern int net_cert_time_error;

/* While set, certificates are checked (chain, host name) but not their dates: only to
 * read the time from a server when the Amiga clock is wrong (clock_amiga.c). */
extern int net_ignore_cert_time;

/* "TLSv1.3 TLS_AES_256_GCM_SHA384" */
const char *net_tls_info(NetConn *c);

#endif
