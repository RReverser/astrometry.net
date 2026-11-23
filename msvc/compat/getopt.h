/* <getopt.h> for the MSVC port: `getopt` and `getopt_long`
 (see `an_msvc_compat.c`). Options and arguments are not permuted: parsing
 stops at the first argument that is not an option, as POSIX specifies. */
#ifndef AN_MSVC_GETOPT_H
#define AN_MSVC_GETOPT_H

#ifdef __cplusplus
extern "C" {
#endif

extern char* optarg;
extern int optind, opterr, optopt;

#define no_argument 0
#define required_argument 1
#define optional_argument 2

struct option {
    const char* name;
    int has_arg;
    int* flag;
    int val;
};

int getopt(int argc, char* const argv[], const char* optstring);
int getopt_long(int argc, char* const argv[], const char* optstring,
                const struct option* longopts, int* longindex);

#ifdef __cplusplus
}
#endif

#endif
