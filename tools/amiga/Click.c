/* Click - move the mouse pointer to a point of the front screen and click there (for
 * remote tests through AmiAgent, which has no mouse). OS 2.04+.
 *
 *   Click X Y                 left click
 *   Click X Y DOUBLE          double click
 *   Click X Y RIGHT           right button pressed at X Y, then held while moving to
 *                             TOX TOY and released there (menus: Click X Y RIGHT TOX=.. TOY=..)
 *   Click X Y DRAG TOX=.. TOY=..   left button drag (e.g. an icon onto another)
 *   HOLD=n                    keep the right button pressed n seconds (screenshot of a menu)
 *
 * Build: vc +aos68k_win -c99 -cpu=68020 -O1 -I<NDK>/Include_H tools/amiga/Click.c -o build/amiga/Click -lamiga
 */
#include <string.h>

#include <exec/types.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

struct IntuitionBase *IntuitionBase;

static struct IOStdReq *io;
static struct Screen *scr;

static void send(struct InputEvent *ie)
{
    io->io_Command = IND_WRITEEVENT;
    io->io_Data = (APTR)ie;
    io->io_Length = sizeof(*ie);
    DoIO((struct IORequest *)io);
}

static void move_to(WORD x, WORD y)
{
    struct InputEvent ie;
    struct IEPointerPixel pp;

    pp.iepp_Screen = scr;
    pp.iepp_Position.X = x;
    pp.iepp_Position.Y = y;
    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_NEWPOINTERPOS;
    ie.ie_SubClass = IESUBCLASS_PIXEL;
    ie.ie_EventAddress = (APTR)&pp;
    send(&ie);
    Delay(2);
}

static void button(UWORD code)
{
    struct InputEvent ie;

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWMOUSE;
    ie.ie_Code = code;
    ie.ie_Qualifier = IEQUALIFIER_RELATIVEMOUSE;
    send(&ie);
    Delay(2);
}

int main(void)
{
    LONG args[8] = { 0 };
    struct RDArgs *rda = ReadArgs((STRPTR)"X/N/A,Y/N/A,DOUBLE/S,RIGHT/S,DRAG/S,TOX/K/N,TOY/K/N,HOLD/K/N", args, NULL);
    struct MsgPort *port = NULL;
    int rc = RETURN_FAIL;
    WORD x, y, tx, ty;

    if (!rda) {
        PrintFault(IoErr(), (STRPTR)"Click");
        return RETURN_FAIL;
    }
    x = (WORD)*(LONG *)args[0];
    y = (WORD)*(LONG *)args[1];
    tx = args[5] ? (WORD)*(LONG *)args[5] : x;
    ty = args[6] ? (WORD)*(LONG *)args[6] : y;

    if ((IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 36)) &&
        (port = CreateMsgPort()) &&
        (io = (struct IOStdReq *)CreateIORequest(port, sizeof(*io)))) {
        if (OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)io, 0) == 0) {
            scr = IntuitionBase->FirstScreen;
            move_to(x, y);
            if (args[3]) {                          /* right button: menus */
                button(IECODE_RBUTTON);
                Delay(10);
                move_to(tx, 4);                     /* over the menu title first */
                Delay(10);
                move_to(tx, ty);
                Delay(args[7] ? *(LONG *)args[7] * 50 : 5);
                button(IECODE_RBUTTON | IECODE_UP_PREFIX);
            } else if (args[4]) {                   /* drag */
                button(IECODE_LBUTTON);
                Delay(10);
                move_to((WORD)((x + tx) / 2), (WORD)((y + ty) / 2));
                Delay(5);
                move_to(tx, ty);
                Delay(10);
                button(IECODE_LBUTTON | IECODE_UP_PREFIX);
            } else {
                button(IECODE_LBUTTON);
                button(IECODE_LBUTTON | IECODE_UP_PREFIX);
                if (args[2]) {
                    Delay(5);
                    button(IECODE_LBUTTON);
                    button(IECODE_LBUTTON | IECODE_UP_PREFIX);
                }
            }
            rc = RETURN_OK;
            CloseDevice((struct IORequest *)io);
        }
    }
    if (io)
        DeleteIORequest((struct IORequest *)io);
    if (port)
        DeleteMsgPort(port);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    FreeArgs(rda);
    return rc;
}
