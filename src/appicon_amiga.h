/* The "OneDrive" icon on the Workbench while AmiDrive synchronises in the background
 * (AppIcon of workbench.library):
 *   double-click            opens the synchronised directory (OS 3.5+, workbench.library 44)
 *                           and asks for the status window (APPICON_STATUS)
 *   files dropped on it     are copied into it (C:Copy ALL CLONE), then synchronised at once
 *   Tools menu              "AmiDrive: status", "AmiDrive: synchronise now", "AmiDrive: stop"
 * During a pass appicon_poll() opens the drawer and answers the menu at once; dropped
 * files wait (their messages unanswered) until appicon_handle() after the pass.
 * The image is the icon of the program (PROGDIR:<name>.info), else the default drawer. */
#ifndef APPICON_AMIGA_H
#define APPICON_AMIGA_H

#include <exec/types.h>

#define APPICON_SYNC 1                  /* synchronise now */
#define APPICON_STOP 2                  /* stop AmiDrive */
#define APPICON_STATUS 4                /* show the status window */

/* Show the icon. progname: the program file (for its image), log: messages for the
 * log/window. Returns 1 if shown, 0 without Workbench or memory (AmiDrive goes on). */
int appicon_open(const char *label, const char *local, const char *progname,
                 void (*log)(const char *msg));

/* Signal of the icon's message port (0 if not shown). */
ULONG appicon_signal(void);

/* Between passes: handle and reply to all messages (also the dropped files kept by
 * appicon_poll). Returns APPICON_ flags. */
int appicon_handle(void);

/* During a pass: double-click and menu at once, dropped files kept for later.
 * Returns APPICON_ flags (APPICON_SYNC: ignore, a pass is running). */
int appicon_poll(void);

void appicon_close(void);

#endif
