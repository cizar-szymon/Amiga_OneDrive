/* Test runner: make -f Makefile.host test (PC), make tests (Amiga) */
#include "tests.h"

int tests_run, tests_failed;

#define RING 16
static char ring[RING][6000];
static int ring_pos;

static char *next_buf(void)
{
    char *b = ring[ring_pos];
    ring_pos = (ring_pos + 1) % RING;
    return b;
}

const char *http_resp(int status, const char *extra_headers, const char *body)
{
    char *b = next_buf();

    sprintf(b, "HTTP/1.1 %d X\r\n%sContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s",
            status, extra_headers ? extra_headers : "", (int)strlen(body), body);
    return b;
}

const char *cat2(const char *a, const char *b)
{
    char *r = next_buf();

    strcpy(r, a);
    strcat(r, b);
    return r;
}

int main(void)
{
    test_http();
    test_json();
    test_fsmap();
    test_tz();
    test_session();
    test_config();
    test_auth();
    test_graph();
    test_state();

    printf("%d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
