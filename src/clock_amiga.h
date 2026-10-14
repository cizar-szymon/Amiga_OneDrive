/* The Amiga clock from the Internet.
 *
 * The time is the Date header of an HTTPS server (graph.microsoft.com), read over a
 * connection whose certificate chain and host name are verified as always - only the
 * validity dates are not checked, as they cannot be while the clock is wrong. No token
 * is sent on that connection. Precision: about a second.
 */
#ifndef CLOCK_AMIGA_H
#define CLOCK_AMIGA_H

/* UTC seconds since 1978 from host. Returns AD_OK, else an AD_ERR_* code and err. */
int clock_internet(const char *host, unsigned long *utc, char *err, int errsize);

/* Set the system time (timer.device) and the battery clock to local seconds since
 * 1978. Returns AD_OK (battery: 1 if a battery clock was written, else 0). */
int clock_set_local(unsigned long local, int *battery);

#endif
