/* The Amiga clock from the Internet (see clock_amiga.h). Kept apart from net_amiga.c:
 * the timer.device 'struct timeval' differs from the one of the socket headers. */
#include <string.h>

#include <exec/types.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <resources/battclock.h>
#include <proto/exec.h>
#include <proto/battclock.h>

#include "amidrive.h"
#include "clock_amiga.h"
#include "net_amiga.h"
#include "session.h"

struct Library *BattClockBase;

int clock_internet(const char *host, unsigned long *utc, char *err, int errsize)
{
    static Session s;                   /* large: not on the stack */
    int rc;

    sess_init(&s, host);
    net_ignore_cert_time = 1;
    rc = sess_request(&s, "GET", "/v1.0/", "Accept: application/json\r\n", NULL, 0);
    net_ignore_cert_time = 0;
    if (rc == AD_OK) {
        *utc = s.http.date;
        rc = http_skip_body(&s.http);
        if (rc == AD_OK && !*utc) {
            rc = AD_ERR_PROTO;
            strncpy(err, "the server sent no time", (size_t)errsize - 1);
            err[errsize - 1] = '\0';
        }
    } else {
        strncpy(err, s.err, (size_t)errsize - 1);
        err[errsize - 1] = '\0';
    }
    sess_close(&s);                     /* never reused by requests with a token */
    return rc;
}

int clock_set_local(unsigned long local, int *battery)
{
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *tr = port ? (struct timerequest *)CreateIORequest(port, sizeof(*tr)) : NULL;
    int rc = AD_ERR;

    *battery = 0;
    if (tr && OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0) == 0) {
        tr->tr_node.io_Command = TR_SETSYSTIME;
        tr->tr_time.tv_secs = local;
        tr->tr_time.tv_micro = 0;
        if (DoIO((struct IORequest *)tr) == 0)
            rc = AD_OK;
        CloseDevice((struct IORequest *)tr);
    }
    if (tr)
        DeleteIORequest((struct IORequest *)tr);
    if (port)
        DeleteMsgPort(port);

    /* like "SetClock SAVE": the time survives a restart (machines without one: none) */
    if (rc == AD_OK && (BattClockBase = (struct Library *)OpenResource((STRPTR)BATTCLOCKNAME)) != NULL) {
        WriteBattClock(local);
        *battery = 1;
    }
    return rc;
}
