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
static struct DiskObject *dob;          /* the image shown now */
static struct DiskObject *idle_dob, *busy_dob;  /* busy: Icons/OneDrive-sync, may be NULL */
static char icon_label[32];
static struct TagItem icon_tags[] = {
    { WBAPPICONA_SupportsOpen, TRUE },          /* double-click: AMCLASSICON_Open (V44) */
    { WBAPPICONA_PropagatePosition, TRUE },     /* moved by the user: do_CurrentX/Y (V44) */
    { TAG_DONE, 0 }
};
static char local_dir[PATH_SIZE];       /* LOCAL as given (opened by double-click) */
static char local_full[PATH_SIZE];      /* LOCAL from its lock: to refuse drops from inside */
static void (*log_msg)(const char *msg);
static char msg[PATH_SIZE * 2 + 64];

static void say(void)
{
    if (log_msg)
        log_msg(msg);
}

/* an icon next to the program (PROGDIR:name.info), NULL if there is none */
static struct DiskObject *load_progdir(const char *name)
{
    struct DiskObject *d = NULL;
    BPTR dir = GetProgramDir();

    if (dir && name && *name) {
        BPTR old = CurrentDir(dir);
        d = GetDiskObject((STRPTR)name);
        CurrentDir(old);
    }
    if (d) {
        d->do_Type = 0;                 /* as AddAppIcon wants it */
        d->do_CurrentX = NO_ICON_POSITION;
        d->do_CurrentY = NO_ICON_POSITION;
    }
    return d;
}

/* the image: the program's icon, else the default drawer; placed by Workbench */
static struct DiskObject *load_image(const char *progname)
{
    struct DiskObject *d = progname ? load_progdir(FilePart((STRPTR)progname)) : NULL;

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
    BPTR lock;

    log_msg = log;
    strncpy(icon_label, label, sizeof(icon_label) - 1);
    strncpy(local_dir, local, sizeof(local_dir) - 1);
    local_full[0] = '\0';
    if ((lock = Lock((STRPTR)local, ACCESS_READ)) != 0) {
        NameFromLock(lock, (STRPTR)local_full, sizeof(local_full));
        UnLock(lock);
    }
    if (!(WorkbenchBase = OpenLibrary((STRPTR)"workbench.library", 37)) ||
        !(IconBase = OpenLibrary((STRPTR)"icon.library", 37)) ||
        !(port = CreateMsgPort()) || !(dob = idle_dob = load_image(progname)) ||
        !(icon = AddAppIconA(0, 0, (STRPTR)label, port, 0, dob, icon_tags))) {
        appicon_close();                /* no Workbench running (or no memory) */
        return 0;
    }
    NewList((struct List *)&later);
    busy_dob = load_progdir("Icons/OneDrive-sync");
    drawer_icon();
    menu_status = AddAppMenuItemA(MENU_STATUS, 0, (STRPTR)"AmiDrive: status", port, NULL);
    menu_sync = AddAppMenuItemA(MENU_SYNC, 0, (STRPTR)"AmiDrive: synchronise now", port, NULL);
    menu_stop = AddAppMenuItemA(MENU_STOP, 0, (STRPTR)"AmiDrive: quit", port, NULL);
    return 1;
}

/* Show the "synchronising" image (or the normal one again). Workbench cannot change
 * the image of an AppIcon in place: it is removed and added again with the other one,
 * at the place the user moved it to (propagated position). */
void appicon_busy(int busy)
{
    struct DiskObject *want = busy && busy_dob ? busy_dob : idle_dob;
    struct AppIcon *n;

    if (!icon || want == dob)
        return;
    want->do_CurrentX = dob->do_CurrentX;
    want->do_CurrentY = dob->do_CurrentY;
    RemoveAppIcon(icon);
    if ((n = AddAppIconA(0, 0, (STRPTR)icon_label, port, 0, want, icon_tags)) != NULL) {
        icon = n;
        dob = want;
    } else {                            /* no memory: the old image back */
        icon = AddAppIconA(0, 0, (STRPTR)icon_label, port, 0, dob, icon_tags);
    }
}

/* Another account: its directory and label ("OneDrive 2"); the icon is added again
 * when the label changes (its place is kept). */
void appicon_set(const char *label, const char *local)
{
    BPTR lock;

    strncpy(local_dir, local, sizeof(local_dir) - 1);
    local_full[0] = '\0';
    if (local[0] && (lock = Lock((STRPTR)local, ACCESS_READ)) != 0) {
        NameFromLock(lock, (STRPTR)local_full, sizeof(local_full));
        UnLock(lock);
    }
    if (!icon || strcmp(label, icon_label) == 0)
        return;
    strncpy(icon_label, label, sizeof(icon_label) - 1);
    RemoveAppIcon(icon);
    icon = AddAppIconA(0, 0, (STRPTR)icon_label, port, 0, dob, icon_tags);
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

/* Workbench does not see files changed by other programs: the open drawers of LOCAL
 * (and below it) show the old contents. They are closed and opened again (V44). */
#define MAX_REFRESH 8

void appicon_refresh(void)
{
    static struct TagItem show[] = {
        { WBOPENA_Show, DDFLAGS_SHOWALL },
        { WBOPENA_ViewBy, DDVM_BYNAME },
        { TAG_DONE, 0 }
    };
    static char names[MAX_REFRESH][PATH_SIZE];
    struct TagItem get[2];
    struct List *list = NULL;
    struct Node *nd;
    int i, pass, count = 0;

    if (!icon || WorkbenchBase->lib_Version < 44)
        return;
    get[0].ti_Tag = WBCTRLA_GetOpenDrawerList;
    get[0].ti_Data = (ULONG)&list;
    get[1].ti_Tag = TAG_DONE;
    if (!WorkbenchControlA(NULL, get) || !list)
        return;
    for (nd = list->lh_Head; nd->ln_Succ; nd = nd->ln_Succ)
        if (count < MAX_REFRESH && nd->ln_Name && strlen(nd->ln_Name) < PATH_SIZE &&
            inside_local(nd->ln_Name))
            strcpy(names[count++], nd->ln_Name);
    get[0].ti_Tag = WBCTRLA_FreeOpenDrawerList;
    get[0].ti_Data = (ULONG)list;
    WorkbenchControlA(NULL, get);

    /* LOCAL first (its window under the AmiDrive window), the drawers below it on top */
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < count; i++) {
            int is_local = strlen(names[i]) == strlen(local_full);
            if (is_local != (pass == 0))
                continue;
            CloseWorkbenchObjectA((STRPTR)names[i], NULL);
            if (is_local)
                drawer_icon();
            OpenWorkbenchObjectA((STRPTR)names[i], is_local && WorkbenchBase->lib_Version >= 45 ? show : NULL);
        }
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
                if (r & APPICON_SYNC)
                    appicon_refresh();      /* show the copied files */
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
    if (idle_dob)
        FreeDiskObject(idle_dob);
    if (busy_dob)
        FreeDiskObject(busy_dob);
    dob = idle_dob = busy_dob = NULL;
    if (IconBase)
        CloseLibrary(IconBase);
    if (WorkbenchBase)
        CloseLibrary(WorkbenchBase);
    IconBase = WorkbenchBase = NULL;
}
