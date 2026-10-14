/* The status window of AmiDrive (see status_amiga.h). */
#include <string.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>

#include "amidrive.h"
#include "status_amiga.h"

#define WIN_WIDTH 360                   /* inner width */
#define MARGIN    8
#define BUTTON_W  100

enum { GID_SYNC = 1, GID_STOP, GID_HIDE };

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase;

static struct Window *win;
static struct DrawInfo *dri;
static APTR vi;                         /* GadTools visual info */
static struct Gadget *glist, *gsync;
static int text_bottom;                 /* the text area ends here (buttons below) */
static int busy;
static char lines[3][160];
static int percent = -1;

static UWORD pen(int which, UWORD fallback)
{
    return dri && which < dri->dri_NumPens ? dri->dri_Pens[which] : fallback;
}

/* one line of text, cut to the width */
static void text_at(struct RastPort *rp, int x, int y, int width, const char *s)
{
    struct TextExtent te;
    ULONG n = TextFit(rp, (STRPTR)s, (ULONG)strlen(s), &te, NULL, 1, (WORD)width, 32767);

    Move(rp, (WORD)x, (WORD)(y + rp->TxBaseline));
    Text(rp, (STRPTR)s, n);
}

static void redraw(void)
{
    struct RastPort *rp;
    int x0, x1, y, fh, w;

    if (!win)
        return;
    rp = win->RPort;
    fh = rp->TxHeight;
    x0 = win->BorderLeft + MARGIN;
    x1 = win->Width - win->BorderRight - MARGIN - 1;
    w = x1 - x0 + 1;
    y = win->BorderTop + 6;

    SetAPen(rp, pen(BACKGROUNDPEN, 0));             /* the text area only: not the buttons */
    RectFill(rp, win->BorderLeft, win->BorderTop, win->Width - win->BorderRight - 1, text_bottom);
    SetDrMd(rp, JAM1);

    SetAPen(rp, pen(HIGHLIGHTTEXTPEN, 2));
    text_at(rp, x0, y, w, lines[0]);
    y += fh + 5;

    /* the bar: a recessed frame, filled up to percent */
    SetAPen(rp, pen(SHADOWPEN, 1));
    Move(rp, (WORD)x0, (WORD)(y + fh + 3));
    Draw(rp, (WORD)x0, (WORD)y);
    Draw(rp, (WORD)x1, (WORD)y);
    SetAPen(rp, pen(SHINEPEN, 2));
    Draw(rp, (WORD)x1, (WORD)(y + fh + 3));
    Draw(rp, (WORD)(x0 + 1), (WORD)(y + fh + 3));
    if (percent > 0) {
        int fill = (w - 4) * (percent > 100 ? 100 : percent) / 100;
        if (fill > 0) {
            SetAPen(rp, pen(FILLPEN, 3));
            RectFill(rp, (WORD)(x0 + 2), (WORD)(y + 2), (WORD)(x0 + 1 + fill), (WORD)(y + fh + 1));
        }
    }
    y += fh + 9;

    SetAPen(rp, pen(TEXTPEN, 1));
    text_at(rp, x0, y, w, lines[1]);
    y += fh + 3;
    text_at(rp, x0, y, w, lines[2]);
}

/* the three buttons under the text */
static struct Gadget *make_buttons(struct Screen *scr, int top, int fh)
{
    static const char *const labels[3] = { "Sync now", "Stop", "Hide" };
    struct NewGadget ng;
    struct Gadget *g;
    int i, gap = (WIN_WIDTH - 2 * MARGIN - 3 * BUTTON_W) / 2;

    if (!(g = CreateContext(&glist)))
        return NULL;
    memset(&ng, 0, sizeof(ng));
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
    ng.ng_TopEdge = (WORD)top;
    ng.ng_Width = BUTTON_W;
    ng.ng_Height = (WORD)(fh + 6);
    ng.ng_Flags = PLACETEXT_IN;
    for (i = 0; i < 3 && g; i++) {
        ng.ng_LeftEdge = (WORD)(scr->WBorLeft + MARGIN + i * (BUTTON_W + gap));
        ng.ng_GadgetText = (STRPTR)labels[i];
        ng.ng_GadgetID = (UWORD)(GID_SYNC + i);
        g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, (ULONG)(i == 0 && busy), TAG_DONE);
        if (i == 0)
            gsync = g;
    }
    return g;
}

int status_open(void)
{
    struct Screen *scr;
    int fh, top;

    if (win) {
        WindowToFront(win);
        return 1;
    }
    if ((!IntuitionBase && !(IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 37))) ||
        (!GfxBase && !(GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 37))) ||
        (!GadToolsBase && !(GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 37))))
        return 0;
    if (!(scr = LockPubScreen(NULL)))   /* the default public screen: Workbench */
        return 0;
    fh = scr->RastPort.TxHeight;
    top = scr->WBorTop + scr->Font->ta_YSize + 1;   /* the inner area starts here */
    text_bottom = top + 4 * fh + 24;
    if (!(vi = GetVisualInfo(scr, TAG_DONE)) || !make_buttons(scr, text_bottom + 4, fh)) {
        status_close();
        UnlockPubScreen(NULL, scr);
        return 0;
    }
    win = OpenWindowTags(NULL,
                         WA_PubScreen, (ULONG)scr,
                         WA_Left, (ULONG)((scr->Width - WIN_WIDTH) / 2),
                         WA_Top, (ULONG)(scr->BarHeight + 30),
                         WA_InnerWidth, WIN_WIDTH,
                         WA_InnerHeight, (ULONG)(text_bottom - top + fh + 16),
                         WA_Title, (ULONG)AMIDRIVE_NAME " - OneDrive",
                         WA_Gadgets, (ULONG)glist,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                         WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE, WA_Activate, FALSE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | BUTTONIDCMP,
                         TAG_DONE);
    if (win) {
        dri = GetScreenDrawInfo(scr);
        SetFont(win->RPort, scr->RastPort.Font);
        GT_RefreshWindow(win, NULL);
        redraw();
    }
    UnlockPubScreen(NULL, scr);
    if (!win)
        status_close();
    return win != NULL;
}

int status_is_open(void)
{
    return win != NULL;
}

void status_close(void)
{
    if (win) {
        if (dri)
            FreeScreenDrawInfo(win->WScreen, dri);
        dri = NULL;
        CloseWindow(win);               /* replies to waiting messages */
        win = NULL;
    }
    if (glist)
        FreeGadgets(glist);
    glist = gsync = NULL;
    if (vi)
        FreeVisualInfo(vi);
    vi = NULL;
    if (GadToolsBase)
        CloseLibrary(GadToolsBase);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    GadToolsBase = NULL;
    GfxBase = NULL;
    IntuitionBase = NULL;
}

ULONG status_signal(void)
{
    return win ? 1UL << win->UserPort->mp_SigBit : 0;
}

int status_handle(void)
{
    struct IntuiMessage *im;
    int r = 0;

    if (!win)
        return 0;
    while ((im = GT_GetIMsg(win->UserPort)) != NULL) {
        if (im->Class == IDCMP_CLOSEWINDOW) {
            r |= STATUS_CLOSED;
        } else if (im->Class == IDCMP_GADGETUP) {
            UWORD id = ((struct Gadget *)im->IAddress)->GadgetID;
            r |= id == GID_SYNC ? STATUS_SYNC : id == GID_STOP ? STATUS_STOP : STATUS_CLOSED;
        } else if (im->Class == IDCMP_REFRESHWINDOW) {
            GT_BeginRefresh(win);
            GT_EndRefresh(win, TRUE);
        }
        GT_ReplyIMsg(im);
    }
    if (r & (STATUS_CLOSED | STATUS_STOP))
        status_close();
    return r;
}

void status_busy(int b)
{
    busy = b;
    if (win && gsync)
        GT_SetGadgetAttrs(gsync, win, NULL, GA_Disabled, (ULONG)b, TAG_DONE);
}

void status_show(const char *line1, const char *line2, const char *line3, int pct)
{
    strncpy(lines[0], line1 ? line1 : "", sizeof(lines[0]) - 1);
    strncpy(lines[1], line2 ? line2 : "", sizeof(lines[1]) - 1);
    strncpy(lines[2], line3 ? line3 : "", sizeof(lines[2]) - 1);
    percent = pct;
    redraw();
}
