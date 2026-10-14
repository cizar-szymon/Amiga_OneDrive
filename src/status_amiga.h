/* The status window of AmiDrive on the Workbench screen: three lines of text, a
 * progress bar (what is being done, the file, amounts / speed / time left) and the
 * buttons "Sync now", "Stop" (the synchronisation: paused until "Sync now") and "Quit"
 * (ends AmiDrive); the close gadget only hides it. It opens on request (the
 * "OneDrive" icon, the Tools menu) or by itself for long passes; closing it does not
 * stop AmiDrive. Workbench 3.2 has no context menus for icons, so these buttons are
 * the quick way to the commands. */
#ifndef STATUS_AMIGA_H
#define STATUS_AMIGA_H

#include <exec/types.h>

/* status_handle() results */
#define STATUS_CLOSED 1                 /* close gadget: hidden */
#define STATUS_SYNC   2                 /* "Sync now" (also ends a pause) */
#define STATUS_STOP   4                 /* "Stop": stop the synchronisation, pause */
#define STATUS_QUIT   8                 /* "Quit": end AmiDrive */

/* Where the window opens (outer size) on the Workbench screen; the drawer with the
 * files is placed below it, as wide. Returns 0 without a screen. */
int status_layout(int *left, int *top, int *width, int *height);

/* Open the window (or bring it to the front). Returns 1 if it is open. */
int status_open(void);
int status_is_open(void);
void status_close(void);

/* Signal of the window's port, 0 when closed. */
ULONG status_signal(void);

/* Handle the window's messages. Returns STATUS_ flags. */
int status_handle(void);

/* busy: a pass is running ("Synchronise now" is disabled). */
void status_busy(int busy);

/* New contents; percent < 0: no bar. Redrawn at once (callers limit how often). */
void status_show(const char *line1, const char *line2, const char *line3, int percent);

#endif
