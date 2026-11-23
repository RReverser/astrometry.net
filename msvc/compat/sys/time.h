/* <sys/time.h> for the MSVC port: `gettimeofday` over
 `GetSystemTimePreciseAsFileTime`. */
#ifndef AN_MSVC_SYS_TIME_H
#define AN_MSVC_SYS_TIME_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* `struct timeval` has the layout of the one in <winsock2.h>, and the same
 guard, so the two headers can meet in one file. */
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval {
    long tv_sec;
    long tv_usec;
};
#endif

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

/* `tz` is ignored, as it is by every caller. */
int gettimeofday(struct timeval* tv, void* tz);

#ifdef __cplusplus
}
#endif

#endif
