/* <sys/param.h> for the MSVC port. `os-features.h` includes it for `MIN` and
 `MAX`, and defines them itself when they are missing. */
#ifndef AN_MSVC_SYS_PARAM_H
#define AN_MSVC_SYS_PARAM_H

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#endif
