/* The "OneDrive" AppIcon on the Workbench (see appicon_amiga.h). */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <utility/tagitem.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/wb.h>

#include "appicon_amiga.h"

#define PATH_SIZE 512

enum { MENU_SYNC = 1, MENU_STOP = 2 };

struct Library *WorkbenchBase;
struct Library *IconBase;

static struct MsgPort *port;
static struct AppIcon *icon;
static struct AppMenuItem *menu_sync, *menu_stop;
static struct DiskObject *dob;
static char local_dir[PATH_SIZE];       /* LOCAL as given (opened by double-click) */
static char local_full[PATH_SIZE];      /* LOCAL from its lock: to refuse drops from inside */
static void (*log_msg)(const char *msg);
static char msg[PATH_SIZE * 2 + 64];

static void say(void)
{
    if (log_msg)
        log_msg(msg);
}

/* the image: the program's icon, else the default drawer; placed by Workbench */
static struct DiskObject *load_image(const char *progname)
{
    struct DiskObject *d = NULL;
    BPTR dir = GetProgramDir();

    if (dir && progname && *progname) {
        BPTR old = CurrentDir(dir);
        d = GetDiskObject((STRPTR)FilePart((STRPTR)progname));
        CurrentDir(old);
    }
    if (!d)
        d = GetDefDiskObject(WBDRAWER);
    if (d) {
        d->do_Type = 0;                 /* as AddAppIcon wants it */
        d->do_CurrentX = NO_ICON_POSITION;
        d->do_CurrentY = NO_ICON_POSITION;
    }
    return d;
}

int appicon_open(const char *label, const char *local, const char *progname,
                 void (*log)(const char *msg))
{
    static struct TagItem tags[] = {
        { WBAPPICONA_SupportsOpen, TRUE },      /* double-click: AMCLASSICON_Open (V44) */
        { TAG_DONE, 0 }
    };
    BPTR lock;

    log_msg = log;
    strncpy(local_dir, local, sizeof(local_dir) - 1);
    local_full[0] = '\0';
    if ((lock = Lock((STRPTR)local, ACCESS_READ)) != 0) {
        NameFromLock(lock, (STRPTR)local_full, sizeof(local_full));
        UnLock(lock);
    }
    if (!(WorkbenchBase = OpenLibrary((STRPTR)"workbench.library", 37)) ||
        !(IconBase = OpenLibrary((STRPTR)"icon.library", 37)) ||
        !(port = CreateMsgPort()) || !(dob = load_image(progname)) ||
        !(icon = AddAppIconA(0, 0, (STRPTR)label, port, 0, dob, tags))) {
        appicon_close();                /* no Workbench running (or no memory) */
        return 0;
    }
    menu_sync = AddAppMenuItemA(MENU_SYNC, 0, (STRPTR)"AmiDrive: synchronise now", port, NULL);
    menu_stop = AddAppMenuItemA(MENU_STOP, 0, (STRPTR)"AmiDrive: stop", port, NULL);
    return 1;
}

ULONG appicon_signal(void)
{
    return icon ? 1UL << port->mp_SigBit : 0;
}

static void open_drawer(void)
{
    if (WorkbenchBase->lib_Version >= 44)
        OpenWorkbenchObjectA((STRPTR)local_dir, NULL);
}

/* 1 if path is LOCAL or inside it (both full names from locks) */
static int inside_local(const char *path)
{
    size_t i, n = strlen(local_full);

    if (!n)
        return 0;
    for (i = 0; i < n; i++)             /* AmigaDOS names: case does not matter */
        if ((path[i] | 0x20) != (local_full[i] | 0x20) || !path[i])
            return 0;
    return path[n] == '\0' || path[n] == '/' || local_full[n - 1] == ':';
}

/* Copy one dropped object into LOCAL: a file, or a drawer with all it contains. */
static int copy_in(const struct WBArg *a)
{
    static char src[PATH_SIZE], dst[PATH_SIZE], cmd[PATH_SIZE * 2 + 48];
    const char *name;
    BPTR in, out;
    LONG rc;

    if (!a->wa_Lock || !NameFromLock(a->wa_Lock, (STRPTR)src, sizeof(src)))
        return 0;
    if (a->wa_Name && *a->wa_Name && !AddPart((STRPTR)src, a->wa_Name, sizeof(src)))
        return 0;
    name = (const char *)FilePart((STRPTR)src);         /* a drawer: its own name */
    if (!*name) {
        sprintf(msg, "Not copied (a whole volume): %.200s", src);
        say();
        return 0;
    }
    if (inside_local(src)) {
        sprintf(msg, "Already in %.200s: %.200s", local_dir, src);
        say();
        return 0;
    }
    strcpy(dst, local_dir);
    if (!a->wa_Name || !*a->wa_Name)                    /* a drawer: into LOCAL/<name> */
        AddPart((STRPTR)dst, (STRPTR)name, sizeof(dst));

    sprintf(cmd, "C:Copy \"%s\" TO \"%s\" ALL CLONE QUIET", src, dst);
    in = Open((STRPTR)"NIL:", MODE_OLDFILE);
    out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    rc = (in && out) ? SystemTags((STRPTR)cmd, SYS_Input, in, SYS_Output, out, TAG_DONE) : -1;
    if (in)
        Close(in);
    if (out)
        Close(out);
    sprintf(msg, rc == 0 ? "Copied %.200s to %.200s" : "Could not copy %.200s to %.200s", src, dst);
    say();
    return rc == 0;
}

int appicon_handle(void)
{
    struct AppMessage *am;
    int r = 0;
    LONG i;

    if (!icon)
        return 0;
    while ((am = (struct AppMessage *)GetMsg(port)) != NULL) {
        if (am->am_Type == AMTYPE_APPMENUITEM) {
            r |= am->am_ID == MENU_STOP ? APPICON_STOP : APPICON_SYNC;
        } else if (am->am_Type == AMTYPE_APPICON) {
            if (am->am_NumArgs == 0) {
                /* before V44 am_Class is 0 = AMCLASSICON_Open too */
                if (am->am_Class == AMCLASSICON_Open)
                    open_drawer();
            } else {
                for (i = 0; i < am->am_NumArgs; i++)
                    if (copy_in(&am->am_ArgList[i]))
                        r |= APPICON_SYNC;
            }
        }
        ReplyMsg((struct Message *)am);
    }
    return r;
}

void appicon_close(void)
{
    struct Message *m;

    if (menu_stop)
        RemoveAppMenuItem(menu_stop);
    if (menu_sync)
        RemoveAppMenuItem(menu_sync);
    menu_sync = menu_stop = NULL;
    if (icon)
        RemoveAppIcon(icon);
    icon = NULL;
    if (port) {
        while ((m = GetMsg(port)) != NULL)
            ReplyMsg(m);
        DeleteMsgPort(port);
        port = NULL;
    }
    if (dob)
        FreeDiskObject(dob);
    dob = NULL;
    if (IconBase)
        CloseLibrary(IconBase);
    if (WorkbenchBase)
        CloseLibrary(WorkbenchBase);
    IconBase = WorkbenchBase = NULL;
}
