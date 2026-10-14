/* The status window of AmiDrive (see status_amiga.h). */
#include <string.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include "amidrive.h"
#include "status_amiga.h"

#define WIN_WIDTH 460                   /* inner width */
#define MARGIN    10

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

static struct Window *win;
static struct DrawInfo *dri;
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

    SetAPen(rp, pen(BACKGROUNDPEN, 0));
    RectFill(rp, win->BorderLeft, win->BorderTop, win->Width - win->BorderRight - 1,
             win->Height - win->BorderBottom - 1);
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

int status_open(void)
{
    struct Screen *scr;
    int fh;

    if (win) {
        WindowToFront(win);
        return 1;
    }
    if (!IntuitionBase && !(IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 37)))
        return 0;
    if (!GfxBase && !(GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 37)))
        return 0;
    if (!(scr = LockPubScreen(NULL)))   /* the default public screen: Workbench */
        return 0;
    fh = scr->RastPort.TxHeight;
    win = OpenWindowTags(NULL,
                         WA_PubScreen, (ULONG)scr,
                         WA_Left, (ULONG)((scr->Width - WIN_WIDTH) / 2),
                         WA_Top, (ULONG)(scr->BarHeight + 30),
                         WA_InnerWidth, WIN_WIDTH,
                         WA_InnerHeight, (ULONG)(4 * fh + 26),
                         WA_Title, (ULONG)AMIDRIVE_NAME " - OneDrive",
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                         WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE, WA_Activate, FALSE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW,
                         TAG_DONE);
    if (win) {
        dri = GetScreenDrawInfo(scr);
        SetFont(win->RPort, scr->RastPort.Font);
        redraw();
    }
    UnlockPubScreen(NULL, scr);
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
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
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
    int closed = 0;

    if (!win)
        return 0;
    while ((im = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
        if (im->Class == IDCMP_CLOSEWINDOW)
            closed = 1;
        else if (im->Class == IDCMP_REFRESHWINDOW) {
            BeginRefresh(win);
            EndRefresh(win, TRUE);
        }
        ReplyMsg((struct Message *)im);
    }
    if (closed)
        status_close();
    return closed;
}

void status_show(const char *line1, const char *line2, const char *line3, int pct)
{
    strncpy(lines[0], line1 ? line1 : "", sizeof(lines[0]) - 1);
    strncpy(lines[1], line2 ? line2 : "", sizeof(lines[1]) - 1);
    strncpy(lines[2], line3 ? line3 : "", sizeof(lines[2]) - 1);
    percent = pct;
    redraw();
}
