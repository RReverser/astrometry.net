/*
 Windows port of Astrometry.net: the header that is included ahead of every
 C source file (a forced include).

 It holds only what a header with a POSIX name cannot provide, because the
 definition has to be in place before the first system header is read:
 a 64-bit `off_t`, three macros of `keywords.h`, and the POSIX functions that
 the C runtime declares in standard headers under another name.
 */
#ifndef AN_MSVC_PRELUDE_H
#define AN_MSVC_PRELUDE_H

/* The stock build passes `-D_FILE_OFFSET_BITS=64`. The C runtime defines
 `off_t` as a 32-bit `long`, unless `_OFF_T_DEFINED` says it already exists. */
#define _OFF_T_DEFINED
typedef long _off_t;
typedef long long off_t;

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef intptr_t ssize_t;
typedef int pid_t;
typedef unsigned short mode_t;
/* A BSD and glibc typedef from <sys/types.h>. */
typedef unsigned int uint;

/* `keywords.h` defines these three only in its GNU branch, and Clang for the
 MSVC target does not define `__GNUC__`. Empty definitions make every such
 function an ordinary external one, which is what the branch for current GCC
 and Clang does as well. */
#define InlineDeclare
#define InlineDefineH
#define InlineDefineC

/* 64-bit file positions and sizes, to match the 64-bit `off_t`. */
#define fseeko _fseeki64
#define ftello _ftelli64
#define stat _stat64
#define fstat _fstat64
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
/* `_stat64` does not report symbolic links. */
#ifndef S_ISLNK
#define S_ISLNK(m) 0
#endif
/* Windows files have one read-only flag, not owner, group and other bits:
 the owner bits map to it and the others are empty. */
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IROTH 0
#define S_IWOTH 0
/* `open` has no synchronous-write flag on Windows. `solvedfile.c` already
 defines `O_SYNC` as 0 for systems without it, but only on macOS. */
#define O_SYNC 0
int fchmod(int fd, mode_t mode);

/* <string.h> and <strings.h> */
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define strtok_r strtok_s

/* <stdio.h> and <stdlib.h> */
int vasprintf(char** strp, const char* format, va_list ap);
int mkstemp(char* tmpl);
char* mkdtemp(char* tmpl);

/* <time.h> */
#define timegm _mkgmtime
struct tm* gmtime_r(const time_t* t, struct tm* result);
struct tm* localtime_r(const time_t* t, struct tm* result);

/* <signal.h>: Windows has no `SIGBUS` and no `sigaction`. `ioutils.c` installs
 a `SIGBUS` handler only to print a hint when a mapped file is truncated, so
 `sigaction` here does nothing and reports success. */
#ifndef SIGBUS
#define SIGBUS 10
#endif
typedef int sigset_t;
struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int sa_flags;
};
int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact);

#ifdef __cplusplus
}
#endif

#endif
