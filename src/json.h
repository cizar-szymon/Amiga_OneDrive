/* Thin layer over jsmn: look up keys, walk arrays, copy strings with escapes decoded.
 *
 * jsmn needs the whole JSON text in memory, so callers keep responses small
 * (Graph: $select with the needed fields only, modest page size) and parse one page
 * at a time. The token array is supplied by the caller (no allocation here).
 */
#ifndef JSON_H
#define JSON_H

#include <stddef.h>

#define JSMN_HEADER
#define JSMN_STRICT
#include "../third_party/jsmn/jsmn.h"

typedef struct {
    const char *js;
    jsmntok_t  *tok;
    int         ntok;
} Json;

/* Parse js[0..len). Returns AD_OK, AD_ERR_TOOBIG (not enough tokens) or AD_ERR_PROTO. */
int json_parse(Json *j, const char *js, size_t len, jsmntok_t *tok, unsigned int maxtok);

/* Index of the token following the whole value at index t. */
int json_next(const Json *j, int t);

/* Value of key in the object at index obj, or -1. */
int json_get(const Json *j, int obj, const char *key);

/* Value at path "a.b.c" (object keys only), or -1. */
int json_path(const Json *j, int obj, const char *path);

/* i-th element of the array at index arr, or -1. Walk with json_next() for speed. */
int json_elem(const Json *j, int arr, int i);

int json_is_null(const Json *j, int t);

/* String value with escapes decoded to UTF-8 (\uXXXX, surrogate pairs).
 * Returns the length, or AD_ERR_TOOBIG (out then holds the cut, terminated start)
 * / AD_ERR_PROTO. */
int json_str(const Json *j, int t, char *out, size_t size);

/* Compare a string token with a plain C string (no escapes in s). */
int json_eq(const Json *j, int t, const char *s);

/* Non-negative integer, e.g. "size". Returns AD_OK or AD_ERR_PROTO / AD_ERR_TOOBIG. */
int json_ulong(const Json *j, int t, unsigned long *out);

/* Non-negative integer divided by 10^drop (last digits cut), for values beyond 32 bits,
 * e.g. drive quota in bytes -> MB with drop = 6. */
int json_ulong_cut(const Json *j, int t, int drop, unsigned long *out);

/* true / false. Returns AD_OK or AD_ERR_PROTO. */
int json_bool(const Json *j, int t, int *out);

#endif
