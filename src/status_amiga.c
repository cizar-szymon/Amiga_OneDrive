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

#define MARGIN    8
#define BUTTON_W  70
#define BUTTON_GAP 6
/* inner width: as wide as the buttons (the bar and the text use the same width) */
#define WIN_WIDTH (3 * BUTTON_W + 2 * BUTTON_GAP + 2 * MARGIN)
#define WIN_LEFT  64                    /* top left, beside the column of disk icons */

enum { GID_SYNC = 1, GID_STOP, GID_QUIT };

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

/* one line of text, cut to the width; the rest of the line cleared (no flicker:
 * nothing is erased before it is drawn again) */
static void text_at(struct RastPort *rp, int x, int y, int width, const char *s, UWORD fg)
{
    struct TextExtent te;
    ULONG n = TextFit(rp, (STRPTR)s, (ULONG)strlen(s), &te, NULL, 1, (WORD)width, 32767);
    int end;

    SetAPen(rp, fg);
    SetBPen(rp, pen(BACKGROUNDPEN, 0));
    SetDrMd(rp, JAM2);
    Move(rp, (WORD)x, (WORD)(y + rp->TxBaseline));
    Text(rp, (STRPTR)s, n);
    end = rp->cp_x;
    if (end < x + width) {
        SetAPen(rp, pen(BACKGROUNDPEN, 0));
        RectFill(rp, (WORD)end, (WORD)y, (WORD)(x + width - 1), (WORD)(y + rp->TxHeight - 1));
    }
}

/* full: also the background and the frame of the bar (a new window) */
static void redraw_all(int full)
{
    struct RastPort *rp;
    int x0, x1, y, fh, w, fill;

    if (!win)
        return;
    rp = win->RPort;
    fh = rp->TxHeight;
    x0 = win->BorderLeft + MARGIN;
    x1 = win->Width - win->BorderRight - MARGIN - 1;
    w = x1 - x0 + 1;
    y = win->BorderTop + 6;

    if (full) {
        SetAPen(rp, pen(BACKGROUNDPEN, 0));         /* the text area only: not the buttons */
        RectFill(rp, win->BorderLeft, win->BorderTop, win->Width - win->BorderRight - 1, text_bottom);
    }
    text_at(rp, x0, y, w, lines[0], pen(HIGHLIGHTTEXTPEN, 2));
    y += fh + 5;

    /* the bar: a recessed frame, filled up to percent, the rest cleared */
    if (full) {
        SetAPen(rp, pen(SHADOWPEN, 1));
        Move(rp, (WORD)x0, (WORD)(y + fh + 3));
        Draw(rp, (WORD)x0, (WORD)y);
        Draw(rp, (WORD)x1, (WORD)y);
        SetAPen(rp, pen(SHINEPEN, 2));
        Draw(rp, (WORD)x1, (WORD)(y + fh + 3));
        Draw(rp, (WORD)(x0 + 1), (WORD)(y + fh + 3));
    }
    fill = percent > 0 ? (w - 4) * (percent > 100 ? 100 : percent) / 100 : 0;
    if (fill > 0) {
        SetAPen(rp, pen(FILLPEN, 3));
        RectFill(rp, (WORD)(x0 + 2), (WORD)(y + 2), (WORD)(x0 + 1 + fill), (WORD)(y + fh + 1));
    }
    if (fill < w - 4) {
        SetAPen(rp, pen(BACKGROUNDPEN, 0));
        RectFill(rp, (WORD)(x0 + 2 + fill), (WORD)(y + 2), (WORD)(x1 - 2), (WORD)(y + fh + 1));
    }
    y += fh + 9;

    text_at(rp, x0, y, w, lines[1], pen(TEXTPEN, 1));
    y += fh + 3;
    text_at(rp, x0, y, w, lines[2], pen(TEXTPEN, 1));
}

static void redraw(void)
{
    redraw_all(0);
}

/* the three buttons under the text */
static struct Gadget *make_buttons(struct Screen *scr, int top, int fh)
{
    static const char *const labels[3] = { "Sync now", "Stop", "Quit" };
    struct NewGadget ng;
    struct Gadget *g;
    int i, x0 = (WIN_WIDTH - 3 * BUTTON_W - 2 * BUTTON_GAP) / 2;    /* centred, close together */

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
        ng.ng_LeftEdge = (WORD)(scr->WBorLeft + x0 + i * (BUTTON_W + BUTTON_GAP));
        ng.ng_GadgetText = (STRPTR)labels[i];
        ng.ng_GadgetID = (UWORD)(GID_SYNC + i);
        g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, (ULONG)(i == 0 && busy), TAG_DONE);
        if (i == 0)
            gsync = g;
    }
    return g;
}

/* inner height for the screen's font */
static int inner_height(int fh)
{
    return 4 * fh + 24 + 4 + fh + 6 + 6;            /* text, gap, buttons, bottom */
}

int status_layout(int *left, int *top, int *width, int *height)
{
    struct Library *ib = NULL;
    struct Screen *scr;

    if (!IntuitionBase && !(ib = OpenLibrary((STRPTR)"intuition.library", 37)))
        return 0;
    if (ib)
        IntuitionBase = (struct IntuitionBase *)ib;
    if ((scr = LockPubScreen(NULL)) != NULL) {
        *left = WIN_LEFT;
        *top = scr->BarHeight + 3;
        *width = WIN_WIDTH + scr->WBorLeft + scr->WBorRight;
        *height = inner_height(scr->RastPort.TxHeight) + scr->WBorTop + scr->Font->ta_YSize + 1 +
                  scr->WBorBottom;
        UnlockPubScreen(NULL, scr);
    }
    if (ib) {
        CloseLibrary(ib);
        IntuitionBase = NULL;
    }
    return scr != NULL;
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
                         /* top left; the drawer with the files opens below it, as wide */
                         WA_Left, WIN_LEFT,
                         WA_Top, (ULONG)(scr->BarHeight + 3),
                         WA_InnerWidth, WIN_WIDTH,
                         WA_InnerHeight, (ULONG)inner_height(fh),
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
        redraw_all(1);
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
            r |= id == GID_SYNC ? STATUS_SYNC : id == GID_STOP ? STATUS_STOP : STATUS_QUIT;
        } else if (im->Class == IDCMP_REFRESHWINDOW) {
            GT_BeginRefresh(win);
            GT_EndRefresh(win, TRUE);
        }
        GT_ReplyIMsg(im);
    }
    if (r & (STATUS_CLOSED | STATUS_QUIT))
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
