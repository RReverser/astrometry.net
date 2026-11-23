/*
 Checks of the Windows compatibility layer against the behaviour that the
 Astrometry.net sources rely on. Exits non-zero at the first failure.

 The regular expressions are the ones in `qfits_tools.c`, `starutil.c` and
 `new-wcs.c`; the expected results are what POSIX `regexec` gives.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <regex.h>
#include <libgen.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>

static int failures = 0;

#define CHECK(cond) do { \
        if (!(cond)) { \
            printf("FAILED %s:%i: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static int matches(const char* pattern, const char* s) {
    regex_t re;
    int rtn;
    if (regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB)) {
        printf("FAILED to compile \"%s\"\n", pattern);
        failures++;
        return 0;
    }
    rtn = (regexec(&re, s, 0, NULL, 0) == 0);
    regfree(&re);
    return rtn;
}

static void test_regex(void) {
    const char* re_int = "^[+-]?[0-9]+$";
    const char* re_float = "^[+-]?([0-9]+[.]?[0-9]*|[.][0-9]+)([eEdD][+-]?[0-9]+)?$";
    const char* re_cmp =
        "^[+-]?([0-9]+[.]?[0-9]*|[.][0-9]+)([eEdD][+-]?[0-9]+)?[ ]+[+-]?([0-9]+[.]?[0-9]*|[.][0-9]+)([eEdD][+-]?[0-9]+)?$";
    regex_t re;
    regmatch_t m[6];

    CHECK(matches(re_int, "42"));
    CHECK(matches(re_int, "-7"));
    CHECK(!matches(re_int, "4.2"));
    CHECK(!matches(re_int, ""));
    CHECK(!matches(re_int, "12a"));
    CHECK(matches(re_float, "1.5"));
    CHECK(matches(re_float, "-.5E-3"));
    CHECK(matches(re_float, "3."));
    CHECK(matches(re_float, "2D+10"));
    CHECK(matches(re_float, "17"));
    CHECK(!matches(re_float, "1.5.2"));
    CHECK(!matches(re_float, "abc"));
    CHECK(!matches(re_float, "."));
    CHECK(matches(re_cmp, "1.0 2.0"));
    CHECK(!matches(re_cmp, "1.0"));
    /* Bracket expressions with character classes, from `new-wcs.c`. */
    CHECK(matches("^[AB]P?_[[:digit:]]_[[:digit:]]$", "AP_1_2"));
    CHECK(matches("^[AB]P?_[[:digit:]]_[[:digit:]]$", "B_0_3"));
    CHECK(!matches("^[AB]P?_[[:digit:]]_[[:digit:]]$", "C_0_3"));
    CHECK(matches("^PV[[:digit:]]*_[[:digit:]]*.?$", "PV1_10"));
    CHECK(matches("^CD[12]_[12]$", "CD1_2"));
    CHECK(!matches("^CD[12]_[12]$", "CD1_3"));

    /* Sub-match offsets, as `starutil.c` uses them for H:M:S strings. */
    CHECK(regcomp(&re, "^([+-])?([[:digit:]]{1,2}):([[:digit:]]{1,2}):([[:digit:]]*(\\.[[:digit:]]*)?)$",
                  REG_EXTENDED) == 0);
    CHECK(re.re_nsub == 5);
    CHECK(regexec(&re, "-12:34:56.78", 6, m, 0) == 0);
    CHECK(m[0].rm_so == 0 && m[0].rm_eo == 12);
    CHECK(m[1].rm_so == 0 && m[1].rm_eo == 1);
    CHECK(m[2].rm_so == 1 && m[2].rm_eo == 3);
    CHECK(m[3].rm_so == 4 && m[3].rm_eo == 6);
    CHECK(m[4].rm_so == 7 && m[4].rm_eo == 12);
    CHECK(regexec(&re, "12:34:56", 6, m, 0) == 0);
    CHECK(m[1].rm_so == -1);
    CHECK(regexec(&re, "12.5", 6, m, 0) == REG_NOMATCH);
    regfree(&re);
}

static void test_libgen(void) {
    char a[] = "/usr/lib/file.c";
    char b[] = "/usr/lib/file.c";
    char c[] = "D:\\dir\\sub\\x.fits";
    char d[] = "D:/dir/sub/x.fits";
    char e[] = "plain";
    char f[] = "plain";
    char g[] = "/top";
    CHECK(!strcmp(dirname(a), "/usr/lib"));
    CHECK(!strcmp(basename(b), "file.c"));
    CHECK(!strcmp(dirname(c), "D:\\dir\\sub"));
    CHECK(!strcmp(basename(d), "x.fits"));
    CHECK(!strcmp(dirname(e), "."));
    CHECK(!strcmp(basename(f), "plain"));
    CHECK(!strcmp(dirname(g), "/"));
}

static void test_getopt(void) {
    char* argv[] = { "prog", "-v", "--scale-low", "1.5", "--name=x", "-o", "out", "-ab", "file", NULL };
    int argc = 9;
    struct option longopts[] = {
        { "scale-low", required_argument, NULL, 'L' },
        { "name", required_argument, NULL, 'n' },
        { "verbose", no_argument, NULL, 'v' },
        { NULL, 0, NULL, 0 },
    };
    optind = 1;
    CHECK(getopt_long(argc, argv, "vL:n:o:ab", longopts, NULL) == 'v');
    CHECK(getopt_long(argc, argv, "vL:n:o:ab", longopts, NULL) == 'L');
    CHECK(optarg && !strcmp(optarg, "1.5"));
    CHECK(getopt_long(argc, argv, "vL:n:o:ab", longopts, NULL) == 'n');
    CHECK(optarg && !strcmp(optarg, "x"));
    CHECK(getopt_long(argc, argv, "vL:n:o:ab", longopts, NULL) == 'o');
    CHECK(optarg && !strcmp(optarg, "out"));
    CHECK(getopt_long(argc, argv, "vL:n:o:ab", longopts, NULL) == 'a');
    CHECK(getopt_long(argc, argv, "vL:n:o:ab", longopts, NULL) == 'b');
    CHECK(getopt_long(argc, argv, "vL:n:o:ab", longopts, NULL) == -1);
    CHECK(optind == 8);
}

/* Maps a window of a file at an offset aligned as `ioutils.c` aligns it, and
 checks the bytes, the copy-on-write behaviour and a window that ends at the
 end of the file. */
static void test_mmap(void) {
    char tmpl[512];
    const char* tmp = getenv("TMP");
    int fd, i;
    int ps = getpagesize();
    size_t n = (size_t)ps * 2 + 1000;
    unsigned char* buf = malloc(n);
    unsigned char* map;
    FILE* f;

    snprintf(tmpl, sizeof(tmpl), "%s/an-compat-test.XXXXXX", tmp ? tmp : ".");
    fd = mkstemp(tmpl);
    CHECK(fd >= 0);
    for (i = 0; i < (int)n; i++)
        buf[i] = (unsigned char)(i * 7 + i / 251);
    CHECK(write(fd, buf, (unsigned int)n) == (int)n);
    close(fd);

    f = fopen(tmpl, "rb");
    CHECK(f != NULL);
    map = mmap(0, ps + 1000, PROT_READ | PROT_WRITE, MAP_PRIVATE, fileno(f), ps);
    CHECK(map != MAP_FAILED);
    if (map != MAP_FAILED) {
        CHECK(!memcmp(map, buf + ps, ps + 1000));
        map[5] ^= 0xff;
        CHECK(munmap(map, ps + 1000) == 0);
    }
    map = mmap(0, n, PROT_READ, MAP_SHARED, fileno(f), 0);
    CHECK(map != MAP_FAILED);
    if (map != MAP_FAILED) {
        /* The private write above did not reach the file. */
        CHECK(!memcmp(map, buf, n));
        CHECK(munmap(map, n) == 0);
    }
    errno = 0;
    map = mmap(0, 100, PROT_READ, MAP_SHARED, fileno(f), 123);
    CHECK(map == MAP_FAILED && errno == EINVAL);
    fclose(f);
    CHECK(unlink(tmpl) == 0);
    free(buf);
}

static void test_clocks(void) {
    struct timeval tv;
    struct rusage ru;
    CHECK(gettimeofday(&tv, NULL) == 0);
    /* After 2020-01-01, as Unix seconds. */
    CHECK(tv.tv_sec > 1577836800L);
    CHECK(getrusage(RUSAGE_SELF, &ru) == 0);
    CHECK(ru.ru_maxrss > 0);
}

static void test_byte_order(void) {
    CHECK(htonl(0x01020304u) == 0x04030201u);
    CHECK(ntohl(0x01020304u) == 0x04030201u);
    CHECK(htons(0x0102) == 0x0201);
    CHECK(ntohs(0x0102) == 0x0201);
}

int main(void) {
    test_regex();
    test_libgen();
    test_getopt();
    test_mmap();
    test_clocks();
    test_byte_order();
    if (failures) {
        printf("%i check(s) failed\n", failures);
        return 1;
    }
    printf("all compatibility checks passed\n");
    return 0;
}
