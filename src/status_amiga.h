/* The status window of AmiDrive on the Workbench screen: three lines of text, a
 * progress bar (what is being done, the file, amounts / speed / time left) and the
 * buttons "Sync now", "Stop" (the synchronisation: paused until "Sync now") and "Quit"
 * (ends AmiDrive); the close gadget only hides it. It opens on request (the
 * "OneDrive" icon, the Tools menu) or by itself for long passes; closing it does not
 * stop AmiDrive. Title: "OneDrive <version>". Workbench 3.2 has no context menus for icons, so these buttons are
 * the quick way to the commands. */
#ifndef STATUS_AMIGA_H
#define STATUS_AMIGA_H

#include <exec/types.h>

/* status_handle() results */
#define STATUS_CLOSED 1                 /* close gadget: hidden */
#define STATUS_SYNC   2                 /* "Sync now" (also ends a pause) */
#define STATUS_STOP   4                 /* "Stop": stop the synchronisation, pause */
#define STATUS_QUIT   8                 /* "Quit": end AmiDrive */
#define STATUS_MENU   16                /* a menu item: status_menu() says which */

/* status_menu() results: 1..3 an account, or: */
#define STATUS_M_SIGNIN  10
#define STATUS_M_SIGNOUT 11
#define STATUS_M_START   12             /* "Start with this account" */

/* Where the window opens (outer size) on the Workbench screen; the drawer with the
 * files is placed below it, as wide. Returns 0 without a screen. */
int status_layout(int *left, int *top, int *width, int *height);

/* Open the window (or bring it to the front). Returns 1 if it is open. */
int status_open(void);
int status_front(void);                 /* open, and in front of the other windows */
int status_is_open(void);
void status_close(void);

/* Signal of the window's port, 0 when closed. */
ULONG status_signal(void);

/* Handle the window's messages. Returns STATUS_ flags. */
int status_handle(void);

/* The menu "Account": names of the 3 accounts (NULL / "" = empty), the active one,
 * whether it is signed in ("Sign out" disabled when not), the account to start with
 * (0 = the last used; marked "(start)", "Start with this account" checked when it is
 * the active one). Also the window title. */
void status_accounts(const char *const names[3], int active, int signed_in, int start);

/* The last menu item picked (then 0). */
int status_menu(void);

/* Modal dialogs on the Workbench screen. 1 = OK (buf filled), 0 = cancelled. */
int status_ask_string(const char *title, const char *prompt, char *buf, int size);
int status_ask_drawer(const char *title, const char *initial, char *buf, int size);
void status_message(const char *text);

/* busy: a pass is running ("Synchronise now" is disabled). */
void status_busy(int busy);

/* New contents: the line above and the line below the bar; percent < 0: empty bar.
 * Redrawn at once (callers limit how often). */
void status_show(const char *line1, const char *line2, int percent);

#endif
