/* Byte stream used by the HTTP client.
 *
 * On the Amiga this is a TCP socket wrapped in AmiSSL (net_amiga.c); the PC tests
 * link a fake implementation that replays canned server responses (tests/fake_net.c).
 */
#ifndef NET_H
#define NET_H

typedef struct NetConn NetConn;

/* Connect to host:port with TLS (certificate and host name verified).
 * Returns NULL and a message in err on failure. */
NetConn *net_connect(const char *host, int port, long timeout_secs, char *err, int errsize);
void     net_close(NetConn *c);

/* Read up to len bytes. Returns the number of bytes (> 0), 0 when the peer closed
 * the connection, or a negative AD_ERR_* code. */
long net_read(NetConn *c, void *buf, long len);

/* Write all len bytes. Returns AD_OK or a negative AD_ERR_* code. */
int net_write(NetConn *c, const void *buf, long len);

#endif
