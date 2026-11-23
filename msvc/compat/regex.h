/* <regex.h> for the MSVC port: POSIX `regcomp` and `regexec` over the
 C++ standard library's `std::regex` with the POSIX extended or basic
 grammar (see `an_msvc_regex.cpp`). No third-party library is involved. */
#ifndef AN_MSVC_REGEX_H
#define AN_MSVC_REGEX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ptrdiff_t regoff_t;

typedef struct {
    size_t re_nsub;
    /* The compiled `std::regex`. */
    void* an_msvc_impl;
    int an_msvc_cflags;
} regex_t;

typedef struct {
    regoff_t rm_so;
    regoff_t rm_eo;
} regmatch_t;

#define REG_EXTENDED 1
#define REG_ICASE 2
#define REG_NOSUB 4
#define REG_NEWLINE 8

#define REG_NOTBOL 1
#define REG_NOTEOL 2

#define REG_NOMATCH 1
#define REG_BADPAT 2
#define REG_ESPACE 12

int regcomp(regex_t* preg, const char* pattern, int cflags);
int regexec(const regex_t* preg, const char* string, size_t nmatch,
            regmatch_t pmatch[], int eflags);
size_t regerror(int errcode, const regex_t* preg, char* errbuf, size_t errbuf_size);
void regfree(regex_t* preg);

#ifdef __cplusplus
}
#endif

#endif
