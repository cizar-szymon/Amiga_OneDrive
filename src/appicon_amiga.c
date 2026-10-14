/* The "OneDrive" AppIcon on the Workbench (see appicon_amiga.h). */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <utility/tagitem.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/wb.h>
#include <clib/alib_protos.h>

#include "appicon_amiga.h"
#include "status_amiga.h"

#define PATH_SIZE 512

enum { MENU_SYNC = 1, MENU_STOP = 2, MENU_STATUS = 3 };

struct Library *WorkbenchBase;
struct Library *IconBase;

static struct MsgPort *port;
static struct AppIcon *icon;
static struct AppMenuItem *menu_sync, *menu_stop, *menu_status;
static struct MinList later;            /* files dropped during a pass, not answered yet */
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

/* The drawer with the files opens right below the AmiDrive window, as wide, all files
 * by name: Workbench takes the window from the drawer's icon, so the icon is set up
 * before each opening (made if there is none; its image and place are kept). A volume
 * root ("Work:") has no drawer icon of its own. */
static void drawer_icon(void)
{
    struct DiskObject *d;
    size_t n = strlen(local_dir);
    BPTR lock;
    int x = 64, y = 140, w = 260, h = 120, made = 0;

    if (!n || local_dir[n - 1] == ':' || local_dir[n - 1] == '/')
        return;
    if ((lock = Lock((STRPTR)local_dir, ACCESS_READ)) == 0)
        return;                         /* not there (yet) */
    UnLock(lock);
    if (!(d = GetDiskObject((STRPTR)local_dir))) {
        if (!(d = GetDefDiskObject(WBDRAWER)))
            return;
        made = 1;
    }
    if (d->do_DrawerData) {
        if (status_layout(&x, &y, &w, &h))  /* below the AmiDrive window, as wide */
            y += h + 2;
        d->do_DrawerData->dd_NewWindow.LeftEdge = (WORD)x;
        d->do_DrawerData->dd_NewWindow.TopEdge = (WORD)y;
        d->do_DrawerData->dd_NewWindow.Width = (WORD)w;
        d->do_DrawerData->dd_NewWindow.Height = 240;
        d->do_DrawerData->dd_Flags = DDFLAGS_SHOWALL;
        d->do_DrawerData->dd_ViewModes = DDVM_BYNAME;
        if (made)
            d->do_CurrentX = d->do_CurrentY = NO_ICON_POSITION;
        PutDiskObject((STRPTR)local_dir, d);
    }
    FreeDiskObject(d);
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
    NewList((struct List *)&later);
    drawer_icon();
    menu_status = AddAppMenuItemA(MENU_STATUS, 0, (STRPTR)"AmiDrive: status", port, NULL);
    menu_sync = AddAppMenuItemA(MENU_SYNC, 0, (STRPTR)"AmiDrive: synchronise now", port, NULL);
    menu_stop = AddAppMenuItemA(MENU_STOP, 0, (STRPTR)"AmiDrive: stop", port, NULL);
    return 1;
}

ULONG appicon_signal(void)
{
    return icon ? 1UL << port->mp_SigBit : 0;
}

/* the drawer as text, all files, sorted by name (tags of V45; V44 opens it as it was) */
static void open_drawer(void)
{
    static struct TagItem tags[] = {
        { WBOPENA_Show, DDFLAGS_SHOWALL },
        { WBOPENA_ViewBy, DDVM_BYNAME },
        { TAG_DONE, 0 }
    };

    if (WorkbenchBase->lib_Version >= 44) {
        drawer_icon();
        OpenWorkbenchObjectA((STRPTR)local_dir, WorkbenchBase->lib_Version >= 45 ? tags : NULL);
    }
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

static int menu_flag(const struct AppMessage *am)
{
    return am->am_ID == MENU_STOP ? APPICON_STOP : am->am_ID == MENU_STATUS ? APPICON_STATUS :
           APPICON_SYNC;
}

int appicon_poll(void)
{
    struct AppMessage *am;
    int r = 0;

    if (!icon)
        return 0;
    while ((am = (struct AppMessage *)GetMsg(port)) != NULL) {
        if (am->am_Type == AMTYPE_APPICON && am->am_NumArgs > 0) {
            AddTail((struct List *)&later, (struct Node *)am);  /* copied after the pass */
            sprintf(msg, "Files dropped on the icon: copied when this pass is done");
            say();
            continue;
        }
        if (am->am_Type == AMTYPE_APPMENUITEM)
            r |= menu_flag(am);
        else if (am->am_Type == AMTYPE_APPICON && am->am_Class == AMCLASSICON_Open) {
            open_drawer();
            r |= APPICON_STATUS;        /* the files are coming: show what is going on */
        }
        ReplyMsg((struct Message *)am);
    }
    return r;
}

int appicon_handle(void)
{
    struct AppMessage *am;
    int r = 0;
    LONG i;

    if (!icon)
        return 0;
    for (;;) {
        if ((am = (struct AppMessage *)RemHead((struct List *)&later)) == NULL &&
            (am = (struct AppMessage *)GetMsg(port)) == NULL)
            break;
        if (am->am_Type == AMTYPE_APPMENUITEM) {
            r |= menu_flag(am);
        } else if (am->am_Type == AMTYPE_APPICON) {
            if (am->am_NumArgs == 0) {
                /* before V44 am_Class is 0 = AMCLASSICON_Open too */
                if (am->am_Class == AMCLASSICON_Open) {
                    open_drawer();
                    r |= APPICON_STATUS;    /* and the window with the buttons */
                }
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

    if (icon)                           /* dropped files never copied: answer them */
        while ((m = (struct Message *)RemHead((struct List *)&later)) != NULL)
            ReplyMsg(m);
    if (menu_status)
        RemoveAppMenuItem(menu_status);
    menu_status = NULL;
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
