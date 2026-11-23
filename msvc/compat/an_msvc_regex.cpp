// POSIX <regex.h> for the MSVC port, over `std::regex`.
//
// `std::regex` implements the POSIX extended and basic grammars, so the
// patterns in `qfits_tools.c`, `starutil.c` and `new-wcs.c` compile as written.

#include <regex>
#include <new>
#include <cstring>

#include "regex.h"

extern "C" int regcomp(regex_t* preg, const char* pattern, int cflags) {
    std::regex::flag_type flags =
        (cflags & REG_EXTENDED) ? std::regex::extended : std::regex::basic;
    if (cflags & REG_ICASE)
        flags |= std::regex::icase;
    if (cflags & REG_NOSUB)
        flags |= std::regex::nosubs;
    preg->an_msvc_impl = nullptr;
    preg->an_msvc_cflags = cflags;
    preg->re_nsub = 0;
    try {
        std::regex* re = new std::regex(pattern, flags);
        preg->an_msvc_impl = re;
        preg->re_nsub = re->mark_count();
    } catch (const std::regex_error&) {
        return REG_BADPAT;
    } catch (const std::bad_alloc&) {
        return REG_ESPACE;
    }
    return 0;
}

extern "C" int regexec(const regex_t* preg, const char* string, size_t nmatch,
                       regmatch_t pmatch[], int eflags) {
    const std::regex* re = static_cast<const std::regex*>(preg->an_msvc_impl);
    std::regex_constants::match_flag_type flags = std::regex_constants::match_default;
    if (eflags & REG_NOTBOL)
        flags |= std::regex_constants::match_not_bol;
    if (eflags & REG_NOTEOL)
        flags |= std::regex_constants::match_not_eol;
    std::cmatch m;
    try {
        if (!std::regex_search(string, m, *re, flags))
            return REG_NOMATCH;
    } catch (const std::regex_error&) {
        return REG_ESPACE;
    } catch (const std::bad_alloc&) {
        return REG_ESPACE;
    }
    if (preg->an_msvc_cflags & REG_NOSUB)
        return 0;
    for (size_t i = 0; i < nmatch; i++) {
        if (i < m.size() && m[i].matched) {
            pmatch[i].rm_so = m.position(i);
            pmatch[i].rm_eo = m.position(i) + m.length(i);
        } else {
            pmatch[i].rm_so = -1;
            pmatch[i].rm_eo = -1;
        }
    }
    return 0;
}

extern "C" size_t regerror(int errcode, const regex_t*, char* errbuf, size_t errbuf_size) {
    const char* msg;
    switch (errcode) {
    case 0: msg = "Success"; break;
    case REG_NOMATCH: msg = "No match"; break;
    case REG_BADPAT: msg = "Invalid regular expression"; break;
    case REG_ESPACE: msg = "Out of memory"; break;
    default: msg = "Unknown regular expression error"; break;
    }
    size_t len = std::strlen(msg) + 1;
    if (errbuf && errbuf_size) {
        std::strncpy(errbuf, msg, errbuf_size - 1);
        errbuf[errbuf_size - 1] = '\0';
    }
    return len;
}

extern "C" void regfree(regex_t* preg) {
    delete static_cast<std::regex*>(preg->an_msvc_impl);
    preg->an_msvc_impl = nullptr;
}
