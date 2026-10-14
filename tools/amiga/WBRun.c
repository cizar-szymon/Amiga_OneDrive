/* WBRun - start a program as if its icon was double-clicked (for remote tests through
 * AmiAgent, which has no mouse). OS 3.5+ (workbench.library 44).
 *
 *   WBRun Work:AmiDrive/AmiDrive
 *
 * Build: vc +aos68k_win -c99 -cpu=68020 -O1 -I<NDK>/Include_H tools/amiga/WBRun.c -o build/amiga/WBRun -lamiga
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/wb.h>

struct Library *WorkbenchBase;

int main(void)
{
    LONG args[1] = { 0 };
    struct RDArgs *rda = ReadArgs((STRPTR)"NAME/A", args, NULL);
    int rc = RETURN_FAIL;

    if (!rda) {
        PrintFault(IoErr(), (STRPTR)"WBRun");
        return RETURN_FAIL;
    }
    if ((WorkbenchBase = OpenLibrary((STRPTR)"workbench.library", 44)) != NULL) {
        if (OpenWorkbenchObjectA((STRPTR)args[0], NULL))
            rc = RETURN_OK;
        else
            Printf("WBRun: cannot open %s\n", args[0]);
        CloseLibrary(WorkbenchBase);
    } else {
        Printf("WBRun: workbench.library 44 needed\n");
    }
    FreeArgs(rda);
    return rc;
}
