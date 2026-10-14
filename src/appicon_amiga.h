/* The "OneDrive" icon on the Workbench while AmiDrive synchronises in the background
 * (AppIcon of workbench.library):
 *   double-click            opens the synchronised directory (OS 3.5+, workbench.library 44)
 *   files dropped on it     are copied into it (C:Copy ALL CLONE), then synchronised at once
 *   Tools menu              "AmiDrive: synchronise now", "AmiDrive: stop"
 * The image is the icon of the program (PROGDIR:<name>.info), else the default drawer. */
#ifndef APPICON_AMIGA_H
#define APPICON_AMIGA_H

#include <exec/types.h>

#define APPICON_SYNC 1                  /* synchronise now */
#define APPICON_STOP 2                  /* stop AmiDrive */

/* Show the icon. progname: the program file (for its image), log: messages for the
 * log/window. Returns 1 if shown, 0 without Workbench or memory (AmiDrive goes on). */
int appicon_open(const char *label, const char *local, const char *progname,
                 void (*log)(const char *msg));

/* Signal of the icon's message port (0 if not shown). */
ULONG appicon_signal(void);

/* Handle and reply to all waiting messages. Returns APPICON_ flags. */
int appicon_handle(void);

void appicon_close(void);

#endif
