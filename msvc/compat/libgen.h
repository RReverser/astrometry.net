/* <libgen.h> for the MSVC port. `dirname` and `basename` follow POSIX (they
 may modify the argument) and take both `/` and `\` as separators. */
#ifndef AN_MSVC_LIBGEN_H
#define AN_MSVC_LIBGEN_H

#ifdef __cplusplus
extern "C" {
#endif

char* dirname(char* path);
char* basename(char* path);

#ifdef __cplusplus
}
#endif

#endif
