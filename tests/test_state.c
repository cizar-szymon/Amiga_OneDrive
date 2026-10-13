#include "amidrive.h"
#include "plat.h"
#include "state.h"
#include "tests.h"

static State s, t;

static int put_text(const char *path, const char *text)
{
    return plat_write_file(path, text, (long)strlen(text));
}

static StEntry *add(State *st, const char *id, const char *parent, const char *name,
                    const char *lname, unsigned flags, unsigned long size)
{
    StEntry e;

    memset(&e, 0, sizeof(e));
    e.id = (char *)id;
    e.parent = (char *)parent;
    e.name = (char *)name;
    e.lname = (char *)lname;
    e.flags = flags;
    e.size = size;
    e.mtime = 1500000000UL + size;
    e.ltime = 1500003600UL + size;
    e.ctag = "c";
    return state_put(st, &e);
}

static void test_entries(void)
{
    StEntry *e, copy;
    char p[128];
    long n;

    state_init(&s);
    strcpy(s.root_id, "R!1");
    CHECK(add(&s, "D!2", "R!1", "Gry \xC5\xBC\xC3\xB3\xC5\x82te", "Gry _\xF3_te", ST_FOLDER, 0) != NULL);
    CHECK(add(&s, "D!3", "D!2", "Demo", "Demo", ST_FOLDER, 0) != NULL);
    CHECK(add(&s, "F!4", "D!3", "a.lha", "a.lha", 0, 100) != NULL);
    CHECK(add(&s, "F!5", "R!1", "readme.txt", "readme.txt", 0, 5) != NULL);
    CHECK(add(&s, "F!6", "X!9", "orphan", "orphan", 0, 1) != NULL);
    CHECK_INT(s.count, 5);

    e = state_find(&s, "F!4");
    CHECK(e != NULL && e->size == 100UL);
    CHECK_INT(state_local_path(&s, e, "Work:OneDrive", p, sizeof(p)), 34);
    CHECK_STR(p, "Work:OneDrive/Gry _\xF3_te/Demo/a.lha");
    CHECK(state_local_path(&s, e, "DH1:", p, sizeof(p)) > 0);
    CHECK_STR(p, "DH1:Gry _\xF3_te/Demo/a.lha");
    CHECK(state_remote_path(&s, e, "/Amiga", p, sizeof(p)) > 0);
    CHECK_STR(p, "/Amiga/Gry \xC5\xBC\xC3\xB3\xC5\x82te/Demo/a.lha");
    CHECK(state_remote_path(&s, e, "", p, sizeof(p)) > 0);
    CHECK_STR(p, "/Gry \xC5\xBC\xC3\xB3\xC5\x82te/Demo/a.lha");
    CHECK(state_remote_path(&s, state_find(&s, "F!5"), "/", p, sizeof(p)) > 0);
    CHECK_STR(p, "/readme.txt");
    CHECK_INT(state_local_path(&s, e, "Work:OneDrive", p, 20), AD_ERR_TOOBIG);
    CHECK_INT(state_local_path(&s, state_find(&s, "F!6"), "W:", p, sizeof(p)), AD_ERR);

    /* the synchronised folder itself; a loop of parents */
    {
        StEntry r;
        memset(&r, 0, sizeof(r));
        r.id = "R!1";
        CHECK_INT(state_local_path(&s, &r, "W:x", p, sizeof(p)), 3);
    }
    CHECK(add(&s, "L!1", "L!2", "l1", "l1", ST_FOLDER, 0) != NULL);
    CHECK(add(&s, "L!2", "L!1", "l2", "l2", ST_FOLDER, 0) != NULL);
    CHECK_INT(state_local_path(&s, state_find(&s, "L!1"), "W:", p, sizeof(p)), AD_ERR);
    CHECK_INT(state_remove(&s, "L!1"), AD_OK);
    CHECK_INT(state_remove(&s, "L!2"), AD_OK);
    CHECK_INT(state_remove(&s, "L!2"), AD_ERR);

    /* replace from a copy of the stored entry (strings point into the old block) */
    copy = *state_find(&s, "D!3");
    copy.name = "Dema";
    e = state_put(&s, &copy);
    CHECK(e != NULL);
    CHECK_STR(e->id, "D!3");
    CHECK_STR(e->name, "Dema");
    CHECK_STR(e->lname, "Demo");
    CHECK_STR(e->etag, "");
    CHECK_INT(s.count, 5);

    for (n = 0, e = state_next(&s, NULL); e; e = state_next(&s, e))
        n++;
    CHECK_INT(n, 5);
}

static void test_file(void)
{
    StEntry *e;
    char id[16], name[32];
    long i, n;

    /* names with tabs, newlines and backslashes; an empty parent */
    strcpy(s.delta, "https://graph.microsoft.com/v1.0/me/drive/root/delta?token=a\\b");
    strcpy(s.names, "LATIN2/30");
    CHECK(add(&s, "T!7", "R!1", "tab\there\nnl\\bs", "tab_here", 0, 7) != NULL);
    CHECK(add(&s, "Z!0", "", "noparent", "noparent", 0, 0) != NULL);
    CHECK_INT(state_save(&s, "W:.AmiDrive/state"), AD_OK);

    state_init(&t);
    CHECK_INT(state_load(&t, "W:.AmiDrive/state"), AD_OK);
    CHECK_INT(t.count, s.count);
    CHECK_STR(t.root_id, "R!1");
    CHECK_STR(t.delta, s.delta);
    CHECK_STR(t.names, "LATIN2/30");
    e = state_find(&t, "T!7");
    CHECK(e != NULL);
    if (e) {
        CHECK_STR(e->name, "tab\there\nnl\\bs");
        CHECK_STR(e->parent, "R!1");
        CHECK_STR(e->ctag, "c");
        CHECK(e->size == 7UL && e->mtime == 1500000007UL && e->ltime == 1500003607UL);
    }
    e = state_find(&t, "D!3");
    CHECK(e != NULL && e->flags == ST_FOLDER && strcmp(e->name, "Dema") == 0);
    CHECK(state_find(&t, "Z!0") != NULL);

    /* many entries: several write buffers, long hash chains */
    for (i = 0; i < 300; i++) {
        sprintf(id, "M!%ld", i);
        sprintf(name, "file%03ld.dat", i);
        CHECK(add(&s, id, "D!2", name, name, 0, (unsigned long)i * 1000) != NULL);
    }
    n = s.count;
    CHECK_INT(state_save(&s, "W:.AmiDrive/state"), AD_OK);
    CHECK_INT(state_load(&t, "W:.AmiDrive/state"), AD_OK);
    CHECK_INT(t.count, n);
    e = state_find(&t, "M!299");
    CHECK(e != NULL && e->size == 299000UL && strcmp(e->name, "file299.dat") == 0);

    /* a failed save keeps the old file */
    CHECK(add(&s, "N!1", "R!1", "new", "new", 0, 1) != NULL);
    fake_write_fail = 5000;
    CHECK_INT(state_save(&s, "W:.AmiDrive/state"), AD_ERR);
    fake_write_fail = 0;
    CHECK_INT(state_load(&t, "W:.AmiDrive/state"), AD_OK);
    CHECK_INT(t.count, n);
    CHECK(state_find(&t, "N!1") == NULL);

    /* missing, cut, damaged */
    CHECK_INT(state_load(&t, "W:none"), AD_ERR);
    CHECK_INT(put_text("W:cut", "AmiDrive state 1\nroot\tR!1\ndelta\t\ne\t0\t1\t2\t3\tA\tR!1\t\t\ta\ta\n"), AD_OK);
    CHECK_INT(state_load(&t, "W:cut"), AD_ERR_PROTO);
    CHECK_INT(t.count, 0);
    CHECK_STR(t.root_id, "");
    CHECK_INT(put_text("W:cut", "AmiDrive state 1\nroot\tR!1\ndelta\t\ne\t0\tx\t2\t3\tA\tR!1\t\t\ta\ta\nend\t1\n"), AD_OK);
    CHECK_INT(state_load(&t, "W:cut"), AD_ERR_PROTO);
    CHECK_INT(put_text("W:cut", "AmiDrive state 1\nroot\tR!1\ndelta\t\nend\t1\n"), AD_OK);
    CHECK_INT(state_load(&t, "W:cut"), AD_ERR_PROTO);
    CHECK_INT(put_text("W:cut", "AmiDrive state 9\nend\t0\n"), AD_OK);
    CHECK_INT(state_load(&t, "W:cut"), AD_ERR_PROTO);
    CHECK_INT(put_text("W:cut", "AmiDrive state 1\nroot\tR!1\ndelta\t\nend\t0"), AD_OK);
    CHECK_INT(state_load(&t, "W:cut"), AD_ERR_PROTO);   /* end line without '\n' */
    CHECK_INT(put_text("W:cut", "AmiDrive state 1\nroot\tR!1\ndelta\tx\nend\t0\n"), AD_OK);
    CHECK_INT(state_load(&t, "W:cut"), AD_OK);
    CHECK_STR(t.delta, "x");
    plat_delete_file("W:cut");
    plat_delete_file("W:.AmiDrive/state");

    state_free(&s);
    state_free(&t);
    CHECK_INT(s.count, 0);
    CHECK(state_next(&s, NULL) == NULL);
}

void test_state(void)
{
    test_entries();
    test_file();
}
