#include "amidrive.h"
#include "fsmap.h"
#include "tests.h"

void test_fsmap(void)
{
    char out[256], back[256];
    int changed, i;

    /* Latin-1 */
    CHECK_INT(fs_from_utf8(FS_LATIN1, "Caf\xC3\xA9 M\xC3\xBCller", out, sizeof(out), &changed), 11);
    CHECK_STR(out, "Caf\xE9 M\xFCller");
    CHECK(!changed);
    CHECK_INT(fs_to_utf8(FS_LATIN1, out, back, sizeof(back)), 13);
    CHECK_STR(back, "Caf\xC3\xA9 M\xC3\xBCller");

    /* Polish letters: lost in Latin-1, kept in Latin-2 */
    CHECK_INT(fs_from_utf8(FS_LATIN1, "Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87", out, sizeof(out), &changed), 6);
    CHECK_STR(out, "Za_\xF3__");
    CHECK(changed);
    CHECK_INT(fs_from_utf8(FS_LATIN2, "Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87", out, sizeof(out), &changed), 6);
    CHECK_STR(out, "Za\xBF\xF3\xB3\xE6");
    CHECK(!changed);
    CHECK_INT(fs_to_utf8(FS_LATIN2, out, back, sizeof(back)), 10);
    CHECK_STR(back, "Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87");

    /* every Latin-2 byte converts to UTF-8 and back unchanged */
    for (i = 0x20; i < 0x100; i++) {
        char one[2], u[8];
        if (i >= 0x7F && i < 0xA0)
            continue;
        one[0] = (char)i;
        one[1] = '\0';
        fs_to_utf8(FS_LATIN2, one, u, sizeof(u));
        fs_from_utf8(FS_LATIN2, u, out, sizeof(out), &changed);
        CHECK((unsigned char)out[0] == i && !changed);
    }

    /* invalid UTF-8, overlong, surrogates, emoji, C1 controls */
    CHECK_INT(fs_from_utf8(FS_LATIN1, "a\xFF" "b\xC0\xAF" "c\xED\xA0\x80" "d\xF0\x9F\x98\x80" "e\xC2\x85",
                           out, sizeof(out), &changed), 10);
    CHECK_STR(out, "a_b_c_d_e_");    /* one "_" per bad sequence */
    CHECK(changed);
    CHECK_INT(fs_from_utf8(FS_LATIN1, "abc\xC3", out, sizeof(out), &changed), 4); /* cut sequence */
    CHECK_INT(fs_from_utf8(FS_LATIN1, "abcdef", out, 4, &changed), AD_ERR_TOOBIG);

    /* local names */
    CHECK_INT(fs_local_name(FS_LATIN1, "Readme.txt", 30, 0, out, sizeof(out), &changed), 10);
    CHECK_STR(out, "Readme.txt");
    CHECK(!changed);

    CHECK_INT(fs_local_name(FS_LATIN1, "a:b/c\x01", 30, 0, out, sizeof(out), &changed), 6);
    CHECK_STR(out, "a_b_c_");
    CHECK(changed);

    CHECK_INT(fs_local_name(FS_LATIN1, "", 30, 0, out, sizeof(out), &changed), 1);
    CHECK_STR(out, "_");

    /* too long for OFS/FFS: deterministic, keeps the extension */
    CHECK_INT(fs_local_name(FS_LATIN1, "A very long document name for the Amiga.docx", 30, 0,
                            out, sizeof(out), &changed), 30);
    CHECK(changed);
    CHECK(strncmp(out, "A very long docume~", 19) == 0);
    CHECK(strcmp(out + 30 - 5, ".docx") == 0);
    fs_local_name(FS_LATIN1, "A very long document name for the Amiga.docx", 30, 0, back, sizeof(back), &changed);
    CHECK_STR(out, back);
    fs_local_name(FS_LATIN1, "A very long document name for the Amiga2.docx", 30, 0, back, sizeof(back), &changed);
    CHECK(strcmp(out, back) != 0);                 /* different source, different hash */

    /* fits FFS2 */
    CHECK_INT(fs_local_name(FS_LATIN1, "A very long document name for the Amiga.docx", FS_MAXNAME_LONG, 0,
                            out, sizeof(out), &changed), 44);

    /* long extension is not kept */
    CHECK_INT(fs_local_name(FS_LATIN1, "archive-of-everything-2026.backup-old", 30, 0,
                            out, sizeof(out), &changed), 30);
    CHECK(out[23] == '~' && strchr(out, '.') == NULL);

    /* forced hash on a short name (collision handling) */
    CHECK_INT(fs_local_name(FS_LATIN1, "a.txt", 30, 1, out, sizeof(out), &changed), 12);
    CHECK(strncmp(out, "a~", 2) == 0 && strcmp(out + 8, ".txt") == 0);
    CHECK(changed);

    /* remote name checks (upload) */
    CHECK(fs_remote_name_problem("Report 2026.txt") == NULL);
    CHECK(fs_remote_name_problem("Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87.txt") == NULL);
    CHECK(fs_remote_name_problem("a?b") != NULL);
    CHECK(fs_remote_name_problem("a\\b") != NULL);
    CHECK(fs_remote_name_problem("name.") != NULL);
    CHECK(fs_remote_name_problem("name ") != NULL);
    CHECK(fs_remote_name_problem("con") != NULL);
    CHECK(fs_remote_name_problem("LPT1") != NULL);
    CHECK(fs_remote_name_problem("LPT10") == NULL);
    CHECK(fs_remote_name_problem("desktop.ini") != NULL);
    CHECK(fs_remote_name_problem("~$doc.docx") != NULL);
    CHECK(fs_remote_name_problem("x_vti_y") != NULL);
    CHECK(fs_remote_name_problem("bad\xFFutf8") != NULL);
    CHECK(fs_remote_name_problem("") != NULL);

    /* FNV-1a reference values */
    CHECK(fs_hash("") == 2166136261UL);
    CHECK(fs_hash("a") == 0xE40C292CUL);
}
