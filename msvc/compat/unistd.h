/* <unistd.h> for the MSVC port, over <io.h>, <process.h> and <direct.h>.

 The C runtime already exports `read`, `write`, `close`, `unlink`, `getcwd`,
 `isatty`, `getpid`, `dup2`, `chdir`, `rmdir` and `lseek` under their POSIX
 names (as aliases of the underscore names), so including its headers covers
 them. The rest is declared here and implemented in `an_msvc_compat.c`. */
#ifndef AN_MSVC_UNISTD_H
#define AN_MSVC_UNISTD_H

#include <io.h>
#include <process.h>
#include <direct.h>
#include <fcntl.h>
/* POSIX declares `getopt`, `optarg` and `optind` in <unistd.h>. */
#include <getopt.h>

#ifdef __cplusplus
extern "C" {
#endif

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* `_access` rejects `X_OK`; `an_msvc_access` tests it as existence. */
int an_msvc_access(const char* path, int mode);
#define access an_msvc_access

/* `mkdir` takes no mode on Windows. */
int an_msvc_mkdir(const char* path, int mode);
#define mkdir an_msvc_mkdir

/* Returns the allocation granularity (64 KiB), not the 4 KiB page size:
 the callers use it to align file offsets for `mmap`, and `MapViewOfFile`
 accepts only offsets that are multiples of the allocation granularity. */
int getpagesize(void);

#ifdef __cplusplus
}
#endif

#endif
