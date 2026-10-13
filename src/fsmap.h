/* Mapping OneDrive names (UTF-8) to Amiga file names (8-bit code page) and back.
 *
 * Local names are deterministic: the same remote name always gives the same local
 * name. When a name is changed (unmappable characters, too long), the caller stores
 * the pair in the sync state; on a collision it asks again with force_hash = 1.
 */
#ifndef FSMAP_H
#define FSMAP_H

#include <stddef.h>

typedef enum {
    FS_LATIN1 = 0,      /* ISO-8859-1, AmigaOS default */
    FS_LATIN2 = 1       /* ISO-8859-2, Polish / Central European setups */
} FsCharset;

/* Longest name for the file system: OFS/FFS 30, FFS2/SFS/PFS ~107. */
#define FS_MAXNAME_OFS   30
#define FS_MAXNAME_LONG  107

/* UTF-8 -> code page. Unmappable or invalid sequences become '_'.
 * Returns the length or AD_ERR_TOOBIG; *changed is set when anything was replaced. */
int fs_from_utf8(FsCharset cs, const char *in, char *out, size_t size, int *changed);

/* Code page -> UTF-8. Returns the length or AD_ERR_TOOBIG. */
int fs_to_utf8(FsCharset cs, const char *in, char *out, size_t size);

/* Local name for a remote UTF-8 name, at most maxlen characters. Characters the
 * Amiga file system cannot hold (':' '/' control codes) become '_'. A name that is
 * too long, or any name when force_hash is set, is shortened to
 * "<start>~hhhhhh<.ext>" where hhhhhh is a hash of the full remote name.
 * Returns the length or AD_ERR_*; *changed tells whether it differs from the
 * plain conversion of the remote name. */
int fs_local_name(FsCharset cs, const char *utf8, int maxlen, int force_hash,
                  char *out, size_t size, int *changed);

/* Check a UTF-8 name before uploading it to OneDrive. Returns NULL when allowed,
 * otherwise a short reason. */
const char *fs_remote_name_problem(const char *utf8);

/* 32-bit FNV-1a hash (also used for the state file). */
unsigned long fs_hash(const char *s);

#endif
