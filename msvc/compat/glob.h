/* <glob.h> for the MSVC port (see `an_msvc_compat.c`). Wildcards `*` and `?`
 are expanded in the last path component only, by `FindFirstFile`. Braces and
 `~` are not expanded. */
#ifndef AN_MSVC_GLOB_H
#define AN_MSVC_GLOB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t gl_pathc;
    char** gl_pathv;
    size_t gl_offs;
} glob_t;

#define GLOB_ERR 1
#define GLOB_NOSORT 4
#define GLOB_NOCHECK 16
#define GLOB_BRACE 1024
#define GLOB_TILDE 4096

#define GLOB_NOSPACE 1
#define GLOB_ABORTED 2
#define GLOB_NOMATCH 3

int glob(const char* pattern, int flags, int (*errfunc)(const char*, int), glob_t* pglob);
void globfree(glob_t* pglob);

#ifdef __cplusplus
}
#endif

#endif
