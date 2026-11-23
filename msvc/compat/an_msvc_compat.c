/*
 Windows port of Astrometry.net: the POSIX functions that need real code.

 The headers beside this file declare them under their POSIX names. Nothing
 here changes what the solver computes: these are file mapping, clocks,
 directory listing, option parsing and thread primitives.
 */

#include <windows.h>
#include <psapi.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>
#include <direct.h>

#include "unistd.h"
#include "sys/mman.h"
#include "sys/time.h"
#include "sys/resource.h"
#include "dirent.h"
#include "libgen.h"
#include "getopt.h"
#include "glob.h"
#include "pthread.h"

/* ---- file mapping ------------------------------------------------------ */

int getpagesize(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwAllocationGranularity;
}

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    HANDLE file, mapping;
    DWORD protect, access;
    LARGE_INTEGER filesize;
    void* view;

    (void)addr;
    file = (HANDLE)_get_osfhandle(fd);
    if (file == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return MAP_FAILED;
    }
    if (flags & MAP_PRIVATE) {
        /* Copy-on-write: changes stay in this process, as with `MAP_PRIVATE`. */
        protect = PAGE_WRITECOPY;
        access = (prot & PROT_WRITE) ? FILE_MAP_COPY : FILE_MAP_READ;
    } else if (prot & PROT_WRITE) {
        protect = PAGE_READWRITE;
        access = FILE_MAP_WRITE;
    } else {
        protect = PAGE_READONLY;
        access = FILE_MAP_READ;
    }
    if (!GetFileSizeEx(file, &filesize)) {
        errno = EBADF;
        return MAP_FAILED;
    }
    if (offset < 0 || offset >= filesize.QuadPart || length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    /* POSIX lets a mapping run past the end of the file to the end of its
     last page. `MapViewOfFile` refuses that, so the view stops at the end of
     the file; the bytes beyond it are not the file's in either case. */
    if ((unsigned long long)offset + length > (unsigned long long)filesize.QuadPart)
        length = (size_t)(filesize.QuadPart - offset);

    mapping = CreateFileMappingA(file, NULL, protect, 0, 0, NULL);
    if (!mapping) {
        errno = EACCES;
        return MAP_FAILED;
    }
    view = MapViewOfFile(mapping, access, (DWORD)((unsigned long long)offset >> 32),
                         (DWORD)(offset & 0xffffffff), length);
    /* The view keeps the mapping object alive. */
    CloseHandle(mapping);
    if (!view) {
        errno = (GetLastError() == ERROR_MAPPED_ALIGNMENT) ? EINVAL : ENOMEM;
        return MAP_FAILED;
    }
    return view;
}

int munmap(void* addr, size_t length) {
    (void)length;
    if (!UnmapViewOfFile(addr)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

/* ---- clocks and resources --------------------------------------------- */

/* A `FILETIME` counts 100 ns ticks. */
#define AN_FILETIME_TICKS_PER_SEC 10000000ULL
/* Seconds from the `FILETIME` epoch (1601) to the Unix epoch (1970). */
#define AN_FILETIME_UNIX_EPOCH_SECS 11644473600ULL

static unsigned long long filetime_ticks(const FILETIME* ft) {
    return ((unsigned long long)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
}

static void ticks_to_timeval(unsigned long long ticks, struct timeval* tv) {
    tv->tv_sec = (long)(ticks / AN_FILETIME_TICKS_PER_SEC);
    tv->tv_usec = (long)((ticks % AN_FILETIME_TICKS_PER_SEC) / 10);
}

int gettimeofday(struct timeval* tv, void* tz) {
    FILETIME ft;
    (void)tz;
    GetSystemTimePreciseAsFileTime(&ft);
    ticks_to_timeval(filetime_ticks(&ft)
                     - AN_FILETIME_UNIX_EPOCH_SECS * AN_FILETIME_TICKS_PER_SEC, tv);
    return 0;
}

int getrusage(int who, struct rusage* usage) {
    FILETIME creation, exited, kernel, user;
    PROCESS_MEMORY_COUNTERS pmc;
    HANDLE proc = GetCurrentProcess();

    if (who != RUSAGE_SELF) {
        errno = EINVAL;
        return -1;
    }
    if (!GetProcessTimes(proc, &creation, &exited, &kernel, &user)) {
        errno = EINVAL;
        return -1;
    }
    ticks_to_timeval(filetime_ticks(&user), &usage->ru_utime);
    ticks_to_timeval(filetime_ticks(&kernel), &usage->ru_stime);
    usage->ru_maxrss = 0;
    if (GetProcessMemoryInfo(proc, &pmc, sizeof(pmc)))
        usage->ru_maxrss = (long)(pmc.PeakWorkingSetSize / 1024);
    return 0;
}

int getrlimit(int resource, struct rlimit* rlim) {
    (void)resource;
    rlim->rlim_cur = RLIM_INFINITY;
    rlim->rlim_max = RLIM_INFINITY;
    return 0;
}

int setrlimit(int resource, const struct rlimit* rlim) {
    (void)resource; (void)rlim;
    return 0;
}

/* The argument order differs from `gmtime_s` and `localtime_s`, and the
 return value is a pointer. */
struct tm* gmtime_r(const time_t* t, struct tm* result) {
    return gmtime_s(result, t) ? NULL : result;
}

struct tm* localtime_r(const time_t* t, struct tm* result) {
    return localtime_s(result, t) ? NULL : result;
}

/* ---- files -------------------------------------------------------------- */

#undef access
#undef mkdir

int an_msvc_access(const char* path, int mode) {
    /* `_access` knows only existence, read and write. */
    return _access(path, mode & (R_OK | W_OK));
}

int an_msvc_mkdir(const char* path, int mode) {
    (void)mode;
    return _mkdir(path);
}

/* Replaces the trailing "XXXXXX" of `tmpl` with a name that does not exist
 yet, built from the process id and a counter. */
static int fill_template(char* tmpl, unsigned int attempt) {
    static const char digits[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    size_t len = strlen(tmpl);
    unsigned long long v;
    int i;
    if (len < 6 || strcmp(tmpl + len - 6, "XXXXXX")) {
        errno = EINVAL;
        return -1;
    }
    v = ((unsigned long long)GetCurrentProcessId() << 20)
        ^ ((unsigned long long)GetTickCount64() << 8) ^ attempt;
    for (i = 0; i < 6; i++) {
        tmpl[len - 6 + i] = digits[v % 36];
        v /= 36;
    }
    return 0;
}

/* The number of names to try before giving up; `TMP_MAX` is the C library's
 own bound for the same purpose. */
#define AN_TEMP_ATTEMPTS TMP_MAX

int mkstemp(char* tmpl) {
    unsigned int attempt;
    size_t len = strlen(tmpl);
    for (attempt = 0; attempt < AN_TEMP_ATTEMPTS; attempt++) {
        int fd;
        if (attempt)
            memcpy(tmpl + len - 6, "XXXXXX", 6);
        if (fill_template(tmpl, attempt))
            return -1;
        fd = _open(tmpl, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (fd >= 0)
            return fd;
        if (errno != EEXIST)
            return -1;
    }
    errno = EEXIST;
    return -1;
}

char* mkdtemp(char* tmpl) {
    unsigned int attempt;
    size_t len = strlen(tmpl);
    for (attempt = 0; attempt < AN_TEMP_ATTEMPTS; attempt++) {
        if (attempt)
            memcpy(tmpl + len - 6, "XXXXXX", 6);
        if (fill_template(tmpl, attempt))
            return NULL;
        if (_mkdir(tmpl) == 0)
            return tmpl;
        if (errno != EEXIST)
            return NULL;
    }
    errno = EEXIST;
    return NULL;
}

/* Windows files have no permission bits beyond the read-only flag. */
int fchmod(int fd, mode_t mode) {
    (void)fd; (void)mode;
    return 0;
}

/* `ioutils.c` installs a `SIGBUS` handler only to print a hint when a mapped
 file is truncated. Windows has no such signal. */
int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) {
    (void)signum; (void)act;
    if (oldact)
        memset(oldact, 0, sizeof(*oldact));
    return 0;
}

/* ---- strings ------------------------------------------------------------ */

int vasprintf(char** strp, const char* format, va_list ap) {
    va_list ap2;
    int len;
    char* buf;
    va_copy(ap2, ap);
    len = _vscprintf(format, ap2);
    va_end(ap2);
    if (len < 0) {
        *strp = NULL;
        return -1;
    }
    buf = malloc((size_t)len + 1);
    if (!buf) {
        *strp = NULL;
        return -1;
    }
    len = vsnprintf(buf, (size_t)len + 1, format, ap);
    if (len < 0) {
        free(buf);
        *strp = NULL;
        return -1;
    }
    *strp = buf;
    return len;
}

static int is_sep(char c) {
    return c == '/' || c == '\\';
}

/* The length of a leading "X:" drive prefix, or 0. */
static size_t drive_prefix(const char* path) {
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
        && path[1] == ':')
        return 2;
    return 0;
}

char* dirname(char* path) {
    static char dot[] = ".";
    size_t root, len;
    if (!path || !*path)
        return dot;
    root = drive_prefix(path);
    len = strlen(path);
    /* Trailing separators, then the last component, then the separators
     before it. */
    while (len > root + 1 && is_sep(path[len - 1]))
        len--;
    while (len > root && !is_sep(path[len - 1]))
        len--;
    if (len == root) {
        if (root == 0)
            return dot;
        /* "C:name": the directory is the current one on that drive. */
        path[root] = '\0';
        return path;
    }
    while (len > root + 1 && is_sep(path[len - 1]))
        len--;
    path[len] = '\0';
    return path;
}

char* basename(char* path) {
    static char dot[] = ".";
    size_t root, len, start;
    if (!path || !*path)
        return dot;
    root = drive_prefix(path);
    len = strlen(path);
    while (len > root + 1 && is_sep(path[len - 1]))
        len--;
    path[len] = '\0';
    start = len;
    while (start > root && !is_sep(path[start - 1]))
        start--;
    if (start == len)
        /* The path is a root such as "/" or "C:\". */
        return path;
    return path + start;
}

/* ---- directory listing --------------------------------------------------- */

struct an_msvc_dir {
    HANDLE handle;
    WIN32_FIND_DATAA data;
    /* `FindFirstFile` has already fetched the first entry. */
    int have_entry;
    struct dirent entry;
};

DIR* opendir(const char* path) {
    DIR* dir;
    char* pattern;
    size_t len = strlen(path);
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        errno = ENOENT;
        return NULL;
    }
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        errno = ENOTDIR;
        return NULL;
    }
    pattern = malloc(len + 3);
    dir = calloc(1, sizeof(DIR));
    if (!pattern || !dir) {
        free(pattern);
        free(dir);
        errno = ENOMEM;
        return NULL;
    }
    memcpy(pattern, path, len);
    if (len && !is_sep(path[len - 1]))
        pattern[len++] = '/';
    pattern[len++] = '*';
    pattern[len] = '\0';
    dir->handle = FindFirstFileA(pattern, &dir->data);
    free(pattern);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        free(dir);
        errno = ENOENT;
        return NULL;
    }
    dir->have_entry = 1;
    return dir;
}

struct dirent* readdir(DIR* dir) {
    if (!dir->have_entry) {
        if (!FindNextFileA(dir->handle, &dir->data)) {
            /* The end of the directory is not an error: `errno` stays as
             the caller set it. */
            if (GetLastError() != ERROR_NO_MORE_FILES)
                errno = EIO;
            return NULL;
        }
    }
    dir->have_entry = 0;
    strncpy(dir->entry.d_name, dir->data.cFileName, sizeof(dir->entry.d_name) - 1);
    dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = '\0';
    return &dir->entry;
}

int closedir(DIR* dir) {
    FindClose(dir->handle);
    free(dir);
    return 0;
}

static int compare_strings(const void* a, const void* b) {
    return strcmp(*(char* const*)a, *(char* const*)b);
}

int glob(const char* pattern, int flags, int (*errfunc)(const char*, int), glob_t* pglob) {
    WIN32_FIND_DATAA data;
    HANDLE h;
    size_t dirlen = strlen(pattern);
    size_t cap = 0;
    (void)errfunc;

    pglob->gl_pathc = 0;
    pglob->gl_pathv = NULL;
    while (dirlen > 0 && !is_sep(pattern[dirlen - 1]) && pattern[dirlen - 1] != ':')
        dirlen--;

    h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE)
        return GLOB_NOMATCH;
    do {
        char* full;
        size_t namelen;
        if (!strcmp(data.cFileName, ".") || !strcmp(data.cFileName, ".."))
            continue;
        if (pglob->gl_pathc + 1 >= cap) {
            char** grown;
            cap = cap ? cap * 2 : 16;
            grown = realloc(pglob->gl_pathv, cap * sizeof(char*));
            if (!grown) {
                FindClose(h);
                globfree(pglob);
                return GLOB_NOSPACE;
            }
            pglob->gl_pathv = grown;
        }
        namelen = strlen(data.cFileName);
        full = malloc(dirlen + namelen + 1);
        if (!full) {
            FindClose(h);
            globfree(pglob);
            return GLOB_NOSPACE;
        }
        memcpy(full, pattern, dirlen);
        memcpy(full + dirlen, data.cFileName, namelen + 1);
        pglob->gl_pathv[pglob->gl_pathc++] = full;
        pglob->gl_pathv[pglob->gl_pathc] = NULL;
    } while (FindNextFileA(h, &data));
    FindClose(h);
    if (!pglob->gl_pathc)
        return GLOB_NOMATCH;
    if (!(flags & GLOB_NOSORT))
        qsort(pglob->gl_pathv, pglob->gl_pathc, sizeof(char*), compare_strings);
    return 0;
}

void globfree(glob_t* pglob) {
    size_t i;
    for (i = 0; i < pglob->gl_pathc; i++)
        free(pglob->gl_pathv[i]);
    free(pglob->gl_pathv);
    pglob->gl_pathv = NULL;
    pglob->gl_pathc = 0;
}

/* ---- getopt -------------------------------------------------------------- */

char* optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

/* The position inside a cluster of short options such as "-vvx". */
static const char* short_cursor = NULL;

static int parse_long(int argc, char* const argv[], const char* optstring,
                      const struct option* longopts, int* longindex) {
    const char* arg = argv[optind] + 2;
    const char* eq = strchr(arg, '=');
    size_t namelen = eq ? (size_t)(eq - arg) : strlen(arg);
    int i, match = -1, nmatches = 0;

    for (i = 0; longopts[i].name; i++) {
        if (strncmp(longopts[i].name, arg, namelen))
            continue;
        if (strlen(longopts[i].name) == namelen) {
            /* An exact name beats any number of prefix matches. */
            match = i;
            nmatches = 1;
            break;
        }
        match = i;
        nmatches++;
    }
    optind++;
    if (nmatches != 1) {
        if (opterr)
            fprintf(stderr, "%s: option '--%.*s' is %s\n", argv[0], (int)namelen, arg,
                    nmatches ? "ambiguous" : "unrecognized");
        optopt = 0;
        return '?';
    }
    if (longindex)
        *longindex = match;
    if (longopts[match].has_arg == no_argument) {
        if (eq) {
            if (opterr)
                fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n",
                        argv[0], longopts[match].name);
            optopt = longopts[match].val;
            return '?';
        }
    } else if (eq) {
        optarg = (char*)eq + 1;
    } else if (longopts[match].has_arg == required_argument) {
        if (optind >= argc) {
            if (opterr)
                fprintf(stderr, "%s: option '--%s' requires an argument\n",
                        argv[0], longopts[match].name);
            optopt = longopts[match].val;
            return (optstring[0] == ':') ? ':' : '?';
        }
        optarg = argv[optind++];
    }
    if (longopts[match].flag) {
        *longopts[match].flag = longopts[match].val;
        return 0;
    }
    return longopts[match].val;
}

int getopt_long(int argc, char* const argv[], const char* optstring,
                const struct option* longopts, int* longindex) {
    const char* spec;
    int c;

    optarg = NULL;
    if (!short_cursor || !*short_cursor) {
        const char* arg;
        short_cursor = NULL;
        if (optind >= argc)
            return -1;
        arg = argv[optind];
        if (arg[0] != '-' || arg[1] == '\0')
            return -1;
        if (arg[1] == '-' && arg[2] == '\0') {
            optind++;
            return -1;
        }
        if (arg[1] == '-' && longopts)
            return parse_long(argc, argv, optstring, longopts, longindex);
        short_cursor = arg + 1;
    }

    c = (unsigned char)*short_cursor++;
    spec = (c == ':') ? NULL : strchr(optstring, c);
    if (!spec) {
        if (opterr && optstring[0] != ':')
            fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], c);
        optopt = c;
        if (!*short_cursor) {
            optind++;
            short_cursor = NULL;
        }
        return '?';
    }
    if (spec[1] == ':') {
        if (*short_cursor) {
            optarg = (char*)short_cursor;
            optind++;
        } else if (spec[2] == ':') {
            /* An optional argument must be attached to the option. */
            optind++;
        } else if (optind + 1 < argc) {
            optarg = argv[optind + 1];
            optind += 2;
        } else {
            if (opterr && optstring[0] != ':')
                fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], c);
            optopt = c;
            optind++;
            short_cursor = NULL;
            return (optstring[0] == ':') ? ':' : '?';
        }
        short_cursor = NULL;
    } else if (!*short_cursor) {
        optind++;
        short_cursor = NULL;
    }
    return c;
}

int getopt(int argc, char* const argv[], const char* optstring) {
    return getopt_long(argc, argv, optstring, NULL, NULL);
}

/* ---- threads ------------------------------------------------------------- */

static BOOL CALLBACK once_trampoline(PINIT_ONCE once, PVOID param, PVOID* context) {
    void (*init)(void) = (void (*)(void))param;
    (void)once; (void)context;
    init();
    return TRUE;
}

int pthread_once(pthread_once_t* once, void (*init)(void)) {
    InitOnceExecuteOnce((PINIT_ONCE)once, once_trampoline, (PVOID)init, NULL);
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
    AcquireSRWLockExclusive((PSRWLOCK)mutex);
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    ReleaseSRWLockExclusive((PSRWLOCK)mutex);
    return 0;
}

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) {
    DWORD slot;
    /* Every caller passes a null destructor. One that does not would leak
     silently here, so it is refused. */
    if (destructor)
        return ENOSYS;
    slot = TlsAlloc();
    if (slot == TLS_OUT_OF_INDEXES)
        return EAGAIN;
    *key = slot;
    return 0;
}

void* pthread_getspecific(pthread_key_t key) {
    return TlsGetValue(key);
}

int pthread_setspecific(pthread_key_t key, const void* value) {
    return TlsSetValue(key, (LPVOID)value) ? 0 : EINVAL;
}
