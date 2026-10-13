/* Tiny test harness for the portable modules (PC and Amiga). */
#ifndef TESTS_H
#define TESTS_H

#include <stdio.h>
#include <string.h>

extern int tests_run, tests_failed;

#define CHECK(cond) do { \
        tests_run++; \
        if (!(cond)) { \
            tests_failed++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_INT(a, b) do { \
        long a_ = (long)(a), b_ = (long)(b); \
        tests_run++; \
        if (a_ != b_) { \
            tests_failed++; \
            printf("FAIL %s:%d: %s == %ld, expected %ld\n", __FILE__, __LINE__, #a, a_, b_); \
        } \
    } while (0)

#define CHECK_STR(a, b) do { \
        const char *a_ = (a), *b_ = (b); \
        tests_run++; \
        if (strcmp(a_, b_) != 0) { \
            tests_failed++; \
            printf("FAIL %s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, a_, b_); \
        } \
    } while (0)

void test_http(void);
void test_json(void);
void test_fsmap(void);
void test_tz(void);
void test_session(void);
void test_config(void);
void test_auth(void);

/* fake_net.c */
struct NetConn;
struct NetConn *fake_conn(const char *server_data, long chunk);   /* one direct stream */
void fake_server(const char *const *streams, long chunk);       /* stream per net_connect */
const char *fake_sent(struct NetConn *c);
int fake_is_open(struct NetConn *c);
extern int fake_connects;

/* fake_plat.c */
extern unsigned long fake_time;
extern long fake_slept;

/* tests.c: "HTTP/1.1 <status> X\r\nContent-Length: n\r\n\r\n<body>" (static ring of buffers) */
const char *http_resp(int status, const char *extra_headers, const char *body);
const char *cat2(const char *a, const char *b);

#endif
