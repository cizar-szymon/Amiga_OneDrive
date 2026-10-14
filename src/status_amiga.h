/* The status window of AmiDrive on the Workbench screen: three lines of text and a
 * progress bar (what is being done, the file, amounts / speed / time left). It opens
 * on request (the "OneDrive" icon, the Tools menu) or by itself for long passes, and
 * the user closes it with its close gadget; AmiDrive goes on either way. */
#ifndef STATUS_AMIGA_H
#define STATUS_AMIGA_H

#include <exec/types.h>

/* Open the window (or bring it to the front). Returns 1 if it is open. */
int status_open(void);
int status_is_open(void);
void status_close(void);

/* Signal of the window's port, 0 when closed. */
ULONG status_signal(void);

/* Handle the window's messages. Returns 1 when the user closed it. */
int status_handle(void);

/* New contents; percent < 0: no bar. Redrawn at once (callers limit how often). */
void status_show(const char *line1, const char *line2, const char *line3, int percent);

#endif
