/* TCP + TLS on the Amiga: Roadshow bsdsocket.library and AmiSSL 5 (see net_amiga.h).
 *
 * Unlike the AmiSSL SDK examples the server certificate is always verified
 * (chain against AmiSSL:Certs, host name, SNI): access tokens travel over these
 * connections.
 */
#include <sys/socket.h>
#include <devices/timer.h>      /* struct timeval */
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/socket.h>

#include <proto/amissl.h>
#include <proto/amisslmaster.h>
#include <amissl/amissl.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>

#include "amidrive.h"
#include "net_amiga.h"

struct Library *SocketBase, *AmiSSLMasterBase, *AmiSSLBase, *AmiSSLExtBase;

static SSL_CTX *ctx;
static int amissl_open;

struct NetConn {
    long sock;
    SSL *ssl;
    char info[64];
};

static void set_err(char *err, int errsize, const char *msg, const char *detail)
{
    int n = 0;

    if (!err || errsize <= 0)
        return;
    while (*msg && n < errsize - 1)
        err[n++] = *msg++;
    if (detail) {
        if (n < errsize - 3) {
            err[n++] = ':';
            err[n++] = ' ';
        }
        while (*detail && n < errsize - 1)
            err[n++] = *detail++;
    }
    err[n] = '\0';
}

/* Text of the oldest queued OpenSSL error (or NULL), clearing the queue. */
static const char *ssl_error(void)
{
    static char buf[160];
    unsigned long e = ERR_get_error();

    if (!e)
        return NULL;
    ERR_error_string_n(e, buf, sizeof(buf));
    ERR_clear_error();
    return buf;
}

int net_init(char *err, int errsize)
{
    if (!(SocketBase = OpenLibrary("bsdsocket.library", 4))) {
        set_err(err, errsize, "cannot open bsdsocket.library v4 (is the TCP/IP stack running?)", NULL);
        return AD_ERR_NET;
    }
    if (!(AmiSSLMasterBase = OpenLibrary("amisslmaster.library", AMISSLMASTER_MIN_VERSION))) {
        set_err(err, errsize, "cannot open amisslmaster.library v5 (install AmiSSL 5)", NULL);
        return AD_ERR_TLS;
    }
    if (OpenAmiSSLTags(AMISSL_CURRENT_VERSION,
                       AmiSSL_UsesOpenSSLStructs, FALSE,
                       AmiSSL_GetAmiSSLBase, &AmiSSLBase,
                       AmiSSL_GetAmiSSLExtBase, &AmiSSLExtBase,
                       AmiSSL_SocketBase, SocketBase,
                       AmiSSL_ErrNoPtr, &errno,
                       TAG_DONE) != 0) {
        set_err(err, errsize, "cannot initialise AmiSSL (version 5.24 or newer needed)", NULL);
        return AD_ERR_TLS;
    }
    amissl_open = 1;

    if (!(ctx = SSL_CTX_new(TLS_client_method()))) {
        set_err(err, errsize, "cannot create the TLS context", ssl_error());
        return AD_ERR_TLS;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    /* a server closing without close_notify ends an unsized body, it is not an error */
    SSL_CTX_set_options(ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);
    if (!SSL_CTX_set_default_verify_paths(ctx)) {
        set_err(err, errsize, "cannot load the certificates from AmiSSL:Certs", ssl_error());
        return AD_ERR_TLS;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    return AD_OK;
}

void net_cleanup(void)
{
    if (ctx) {
        SSL_CTX_free(ctx);
        ctx = NULL;
    }
    if (amissl_open) {
        CloseAmiSSL();
        amissl_open = 0;
        AmiSSLBase = AmiSSLExtBase = NULL;
    }
    if (AmiSSLMasterBase) {
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
    }
    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

NetConn *net_connect(const char *host, int port, long timeout_secs, char *err, int errsize)
{
    struct hostent *he;
    struct sockaddr_in addr;
    struct timeval tv;
    NetConn *c;
    int rc;

    if (!(c = AllocVec(sizeof(*c), MEMF_ANY | MEMF_CLEAR))) {
        set_err(err, errsize, "out of memory", NULL);
        return NULL;
    }
    c->sock = -1;

    if (!(he = gethostbyname((STRPTR)host))) {
        set_err(err, errsize, "cannot resolve host name", host);
        goto fail;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr, (size_t)he->h_length);

    if ((c->sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        set_err(err, errsize, "cannot create a socket", NULL);
        goto fail;
    }
    tv.tv_sec = timeout_secs;
    tv.tv_usec = 0;
    setsockopt(c->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(c->sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(c->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        set_err(err, errsize, Errno() == EINTR ? "interrupted" : "cannot connect", host);
        goto fail;
    }

    if (!(c->ssl = SSL_new(ctx))) {
        set_err(err, errsize, "cannot create a TLS session", ssl_error());
        goto fail;
    }
    SSL_set_fd(c->ssl, (int)c->sock);
    SSL_set_tlsext_host_name(c->ssl, (char *)host);    /* SNI */
    SSL_set1_host(c->ssl, host);                       /* certificate must match */

    if ((rc = SSL_connect(c->ssl)) != 1) {
        long vr = SSL_get_verify_result(c->ssl);
        if (vr != X509_V_OK)
            set_err(err, errsize, "certificate rejected", X509_verify_cert_error_string(vr));
        else
            set_err(err, errsize, "TLS handshake failed", ssl_error());
        goto fail;
    }
    strncpy(c->info, SSL_get_version(c->ssl), 15);
    strcat(c->info, " ");
    strncat(c->info, SSL_CIPHER_get_name(SSL_get_current_cipher(c->ssl)), 46);
    return c;

fail:
    net_close(c);
    return NULL;
}

void net_close(NetConn *c)
{
    if (!c)
        return;
    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
    }
    if (c->sock >= 0)
        CloseSocket(c->sock);
    FreeVec(c);
}

const char *net_tls_info(NetConn *c)
{
    return c->info;
}

static int io_error(NetConn *c, int rc)
{
    int e = SSL_get_error(c->ssl, rc);

    ERR_clear_error();
    if (e == SSL_ERROR_ZERO_RETURN)
        return 0;
    if (e == SSL_ERROR_SYSCALL && Errno() == EINTR)
        return AD_ERR_BREAK;
    if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
        return AD_ERR_BREAK;
    return AD_ERR_NET;
}

long net_read(NetConn *c, void *buf, long len)
{
    int n = SSL_read(c->ssl, buf, (int)len);
    return n > 0 ? n : io_error(c, n);
}

int net_write(NetConn *c, const void *buf, long len)
{
    const char *p = buf;

    while (len > 0) {
        int n = SSL_write(c->ssl, p, (int)len);
        if (n <= 0) {
            int rc = io_error(c, n);
            return rc == 0 ? AD_ERR_NET : rc;
        }
        p += n;
        len -= n;
    }
    return AD_OK;
}
