/* <dirent.h> for the MSVC port: directory listing over `FindFirstFile`
 (see `an_msvc_compat.c`). */
#ifndef AN_MSVC_DIRENT_H
#define AN_MSVC_DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

struct dirent {
    /* `MAX_PATH` is 260. */
    char d_name[260];
};

typedef struct an_msvc_dir DIR;

DIR* opendir(const char* path);
struct dirent* readdir(DIR* dir);
int closedir(DIR* dir);

#ifdef __cplusplus
}
#endif

#endif
