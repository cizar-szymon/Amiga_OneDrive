/* File name mapping between OneDrive (UTF-8) and the Amiga (see fsmap.h). */
#include <string.h>

#include "amidrive.h"
#include "fsmap.h"

#define BAD_CP 0xFFFFFFFFUL
#define HASH_LEN 7                       /* "~hhhhhh" */

/* ISO-8859-2 0xA0..0xFF -> Unicode (generated from Python's iso8859_2 codec). */
static const unsigned short latin2[96] = {
    0x00A0, 0x0104, 0x02D8, 0x0141, 0x00A4, 0x013D, 0x015A, 0x00A7,
    0x00A8, 0x0160, 0x015E, 0x0164, 0x0179, 0x00AD, 0x017D, 0x017B,
    0x00B0, 0x0105, 0x02DB, 0x0142, 0x00B4, 0x013E, 0x015B, 0x02C7,
    0x00B8, 0x0161, 0x015F, 0x0165, 0x017A, 0x02DD, 0x017E, 0x017C,
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
    0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
    0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
    0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
    0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

/* Next code point of a UTF-8 string; BAD_CP for an invalid sequence (one byte
 * consumed), so bad input can never stall or overrun. */
static unsigned long next_cp(const unsigned char **ps)
{
    const unsigned char *s = *ps;
    unsigned long cp, min;
    int n, i;

    if (s[0] < 0x80)                { *ps = s + 1; return s[0]; }
    else if ((s[0] & 0xE0) == 0xC0) { cp = s[0] & 0x1F; n = 1; min = 0x80; }
    else if ((s[0] & 0xF0) == 0xE0) { cp = s[0] & 0x0F; n = 2; min = 0x800; }
    else if ((s[0] & 0xF8) == 0xF0) { cp = s[0] & 0x07; n = 3; min = 0x10000; }
    else                            { *ps = s + 1; return BAD_CP; }

    for (i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) {    /* also stops at the terminating zero */
            *ps = s + 1;
            return BAD_CP;
        }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *ps = s + n + 1;
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000))
        return BAD_CP;
    return cp;
}

/* Code point -> byte of the code page, or -1. C1 controls (0x80..0x9F) are
 * treated as unmappable: they have no glyphs on the Amiga. */
static int to_byte(FsCharset cs, unsigned long cp)
{
    int i;

    if (cp < 0x80)
        return (int)cp;
    if (cs == FS_LATIN1)
        return (cp >= 0xA0 && cp <= 0xFF) ? (int)cp : -1;
    for (i = 0; i < 96; i++)
        if (latin2[i] == cp)
            return 0xA0 + i;
    return -1;
}

static unsigned long from_byte(FsCharset cs, unsigned char b)
{
    if (b < 0xA0 || cs == FS_LATIN1)
        return b;
    return latin2[b - 0xA0];
}

int fs_from_utf8(FsCharset cs, const char *in, char *out, size_t size, int *changed)
{
    const unsigned char *s = (const unsigned char *)in;
    size_t len = 0;

    *changed = 0;
    if (size == 0)
        return AD_ERR_TOOBIG;
    while (*s) {
        unsigned long cp = next_cp(&s);
        int b = cp == BAD_CP ? -1 : to_byte(cs, cp);

        if (b < 0) {
            b = '_';
            *changed = 1;
        }
        if (len + 1 >= size) {
            out[len] = '\0';
            return AD_ERR_TOOBIG;
        }
        out[len++] = (char)b;
    }
    out[len] = '\0';
    return (int)len;
}

int fs_to_utf8(FsCharset cs, const char *in, char *out, size_t size)
{
    const unsigned char *s = (const unsigned char *)in;
    size_t len = 0;

    if (size == 0)
        return AD_ERR_TOOBIG;
    for (; *s; s++) {
        unsigned long cp = from_byte(cs, *s);
        size_t n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : 3;

        if (len + n >= size) {
            out[len] = '\0';
            return AD_ERR_TOOBIG;
        }
        if (n == 1) {
            out[len++] = (char)cp;
        } else if (n == 2) {
            out[len++] = (char)(0xC0 | (cp >> 6));
            out[len++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[len++] = (char)(0xE0 | (cp >> 12));
            out[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[len++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[len] = '\0';
    return (int)len;
}

unsigned long fs_hash(const char *s)
{
    unsigned long h = 2166136261UL;

    while (*s) {
        h ^= (unsigned char)*s++;
        h = (h * 16777619UL) & 0xFFFFFFFFUL;
    }
    return h;
}

int fs_local_name(FsCharset cs, const char *utf8, int maxlen, int force_hash,
                  char *out, size_t size, int *changed)
{
    static const char hexd[] = "0123456789abcdef";
    char tmp[512];                       /* OneDrive allows 255 characters per name */
    int len, i, dot, extlen, keep, n;
    unsigned long h;

    if (maxlen < HASH_LEN + 1 || size < (size_t)maxlen + 1)
        return AD_ERR_TOOBIG;

    len = fs_from_utf8(cs, utf8, tmp, sizeof(tmp), changed);
    if (len < 0) {                       /* absurdly long: keep the start, force a hash */
        len = (int)strlen(tmp);
        force_hash = 1;
    }
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)tmp[i];
        if (c == ':' || c == '/' || c < 0x20 || c == 0x7F) {
            tmp[i] = '_';
            *changed = 1;
        }
    }
    if (len == 0) {
        tmp[len++] = '_';
        tmp[len] = '\0';
        *changed = 1;
    }

    if (len <= maxlen && !force_hash) {
        memcpy(out, tmp, (size_t)len + 1);
        return len;
    }

    /* "<start>~hhhhhh<.ext>", keeping a short extension when it fits */
    dot = -1;
    for (i = len - 1; i > 0; i--)
        if (tmp[i] == '.') {
            dot = i;
            break;
        }
    extlen = dot > 0 ? len - dot : 0;
    if (extlen > 8 || extlen > maxlen - HASH_LEN - 1)
        extlen = 0, dot = -1;
    keep = maxlen - HASH_LEN - extlen;
    if (dot > 0 && keep > dot)
        keep = dot;
    if (keep > len)
        keep = len;

    h = fs_hash(utf8);
    n = 0;
    memcpy(out, tmp, (size_t)keep);
    n += keep;
    out[n++] = '~';
    for (i = 5; i >= 0; i--)
        out[n++] = hexd[(h >> (i * 4)) & 0xF];
    if (extlen > 0) {
        memcpy(out + n, tmp + dot, (size_t)extlen);
        n += extlen;
    }
    out[n] = '\0';
    *changed = 1;
    return n;
}

static int eq_ci(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return 0;
    }
    return *a == *b;
}

const char *fs_remote_name_problem(const char *utf8)
{
    static const char *const reserved[] = {
        ".lock", "CON", "PRN", "AUX", "NUL", "desktop.ini", NULL
    };
    const unsigned char *s = (const unsigned char *)utf8;
    size_t n = strlen(utf8), chars = 0;
    int i;

    if (n == 0)
        return "empty name";
    while (*s) {
        unsigned long cp = next_cp(&s);
        if (cp == BAD_CP)
            return "invalid UTF-8";
        if (cp < 0x20 || (cp < 0x80 && strchr("\"*:<>?/\\|", (int)cp)))
            return "forbidden character";
        chars++;
    }
    if (chars > 255)
        return "name longer than 255 characters";
    if (utf8[n - 1] == ' ' || utf8[n - 1] == '.')
        return "name ends with a space or a dot";
    for (i = 0; reserved[i]; i++)
        if (eq_ci(utf8, reserved[i]))
            return "reserved name";
    if (n == 4 && utf8[3] >= '0' && utf8[3] <= '9') {      /* COM0..9, LPT0..9 */
        char head[4];
        memcpy(head, utf8, 3);
        head[3] = '\0';
        if (eq_ci(head, "COM") || eq_ci(head, "LPT"))
            return "reserved name";
    }
    if (strncmp(utf8, "~$", 2) == 0)
        return "name starts with ~$";
    if (strstr(utf8, "_vti_"))
        return "name contains _vti_";
    return NULL;
}
