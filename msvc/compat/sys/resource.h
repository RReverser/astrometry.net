/* <sys/resource.h> for the MSVC port: `getrusage` reports the CPU times from
 `GetProcessTimes` and the peak working set. */
#ifndef AN_MSVC_SYS_RESOURCE_H
#define AN_MSVC_SYS_RESOURCE_H

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RUSAGE_SELF 0

struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    /* Peak working set, in kilobytes as on Linux. */
    long ru_maxrss;
};

int getrusage(int who, struct rusage* usage);

/* Windows has no per-process resource limits of this kind: `getrlimit`
 reports "no limit" and `setrlimit` does nothing. */
typedef unsigned long long rlim_t;
#define RLIM_INFINITY (~(rlim_t)0)

#define RLIMIT_DATA 2
#define RLIMIT_NOFILE 7

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

int getrlimit(int resource, struct rlimit* rlim);
int setrlimit(int resource, const struct rlimit* rlim);

#ifdef __cplusplus
}
#endif

#endif
