/* `config.h` for the bundled GSL subset (`gsl-an`) under MSVC.

 The stock build generates this file with `gsl-an/configure`. The values here
 describe the Universal C Runtime on x64: it declares every C99 math function
 that GSL asks about, so GSL uses the runtime's versions, as it does on
 Cygwin. */
#ifndef AN_MSVC_GSL_CONFIG_H
#define AN_MSVC_GSL_CONFIG_H

#define HAVE_DECL_ACOSH 1
#define HAVE_DECL_ASINH 1
#define HAVE_DECL_ATANH 1
#define HAVE_DECL_EXPM1 1
#define HAVE_DECL_FEENABLEEXCEPT 0
#define HAVE_DECL_FESETTRAPENABLE 0
#define HAVE_DECL_FINITE 1
#define HAVE_DECL_FPRND_T 0
#define HAVE_DECL_FREXP 1
#define HAVE_DECL_HYPOT 1
#define HAVE_DECL_ISFINITE 1
#define HAVE_DECL_ISINF 1
#define HAVE_DECL_ISNAN 1
#define HAVE_DECL_LDEXP 1
#define HAVE_DECL_LOG1P 1

#define HAVE_EXIT_SUCCESS_AND_FAILURE 1
#define HAVE_IEEE_COMPARISONS 1
#define HAVE_IEEE_DENORMALS 1
#define HAVE_INTTYPES_H 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMORY_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRDUP 1
#define HAVE_STRING_H 1
#define HAVE_STRTOL 1
#define HAVE_STRTOUL 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_VPRINTF 1
#define STDC_HEADERS 1

#define PACKAGE "gsl"
#define PACKAGE_NAME "gsl"
#define PACKAGE_TARNAME "gsl"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_URL ""
#define RELEASED

#define GSL_DISABLE_DEPRECATED 1

/* The rest follows the fixed part at the end of `gsl-an/config.h.in`. x64 code
 computes in SSE registers, so there is no extended precision to round away. */
#define GSL_COERCE_DBL(x) (x)

#define DISCARD_POINTER(p) /* ignoring discarded pointer */

#if defined(GSL_RANGE_CHECK_OFF) || !defined(GSL_RANGE_CHECK)
#define GSL_RANGE_CHECK 0  /* turn off range checking by default internally */
#endif

#define RETURN_IF_NULL(x) if (!x) { return ; }

#endif
