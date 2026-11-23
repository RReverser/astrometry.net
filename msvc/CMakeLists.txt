# Windows build of the Astrometry.net solver core with Clang for the MSVC
# target (`clang-cl`, or `clang` with an `x86_64-pc-windows-msvc` target).
#
# This file builds the same object lists as util/Makefile, libkd/Makefile,
# qfits-an/Makefile, gsl-an/Makefile and the ENGINE_OBJS of solver/Makefile,
# less the parts that need cfitsio, netpbm, cairo or Python.
#
#   cmake -G Ninja -S msvc -B msvc/build -DCMAKE_BUILD_TYPE=Release ^
#         -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
#   cmake --build msvc/build

cmake_minimum_required(VERSION 3.20)
project(astrometry_windows C CXX)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
  message(FATAL_ERROR
    "This build file needs Clang that targets the MSVC ABI (clang-cl, or clang with an "
    "x86_64-pc-windows-msvc target), and found ${CMAKE_C_COMPILER_ID} "
    "(simulating \"${CMAKE_C_SIMULATE_ID}\"). Use the stock Makefile elsewhere.")
endif()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(COMPAT ${CMAKE_CURRENT_SOURCE_DIR}/compat)

add_compile_definitions(
  _CRT_SECURE_NO_WARNINGS
  _CRT_NONSTDC_NO_WARNINGS
  _USE_MATH_DEFINES
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  AN_GIT_REVISION="0.99"
  AN_GIT_DATE="windows-port"
  AN_GIT_URL="https://github.com/dstndstn/astrometry.net"
)
# `an_msvc_prelude.h` is included ahead of every C file. It holds what a header
# with a POSIX name cannot provide: a 64-bit `off_t` and the macros that
# `keywords.h` leaves undefined when `__GNUC__` is not. CMake has no
# abstraction for a forced include.
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(AN_PRELUDE "/FI${COMPAT}/an_msvc_prelude.h")
else()
  set(AN_PRELUDE "SHELL:-include ${COMPAT}/an_msvc_prelude.h")
endif()
add_compile_options("$<$<COMPILE_LANGUAGE:C>:${AN_PRELUDE}>")
# C11 gives `inline`, mixed declarations and designated initialisers.
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# The compat directory comes first, so its POSIX-named headers are found
# where the Windows SDK has none. `config` holds the two headers that the
# Makefile build generates; `config/astrometry` is on the path because
# `an-pnmtofits.c` includes `os-features-config.h` without the directory.
include_directories(BEFORE
  ${COMPAT}
  ${CMAKE_CURRENT_SOURCE_DIR}/config
  ${CMAKE_CURRENT_SOURCE_DIR}/config/astrometry
  ${ROOT}/include
  ${ROOT}/include/astrometry
  ${ROOT}/util
  ${ROOT}/gsl-an
)

# ---- compatibility layer -------------------------------------------------
add_library(ancompat STATIC
  ${COMPAT}/an_msvc_compat.c
  ${COMPAT}/an_msvc_regex.cpp
)
# `binmode.obj` ships with the C runtime and makes binary mode the default for
# `fopen` and `open`. The sources open FITS files with "r", "w" and "a", which
# is binary on POSIX but would translate line endings on Windows.
target_link_options(ancompat INTERFACE LINKER:binmode.obj)
target_link_libraries(ancompat INTERFACE psapi)
# `libkd/kdint_ttype_l.h` uses `unsigned __int128`, which Clang defines.
# Converting it to and from `double` calls `__floatuntidf` and `__fixunsdfti`
# of the compiler runtime. The linker does not add that library by itself when
# CMake links with `lld-link`. The option that prints the resource directory
# has a different spelling in each driver.
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(AN_PRINT_RESOURCE_DIR /clang:-print-resource-dir)
else()
  set(AN_PRINT_RESOURCE_DIR -print-resource-dir)
endif()
execute_process(COMMAND "${CMAKE_C_COMPILER}" ${AN_PRINT_RESOURCE_DIR}
  OUTPUT_VARIABLE CLANG_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
find_library(CLANG_BUILTINS clang_rt.builtins-x86_64
  PATHS "${CLANG_RESOURCE_DIR}/lib/windows" NO_DEFAULT_PATH REQUIRED)
target_link_libraries(ancompat INTERFACE "${CLANG_BUILTINS}")

# ---- gsl-an ---------------------------------------------------------------
file(GLOB GSL_CBLAS ${ROOT}/gsl-an/cblas/*.c)
set(GSL_SRC ${GSL_CBLAS})
foreach(f
    linalg/qr linalg/householder linalg/cholesky linalg/svd linalg/bidiag linalg/lu
    matrix/matrix matrix/rowcol matrix/init matrix/submatrix matrix/copy matrix/swap matrix/view
    vector/vector vector/subvector vector/copy vector/init vector/swap vector/oper vector/prop
    block/block block/init
    err/error err/stream err/strerror
    sys/infnan sys/fdiv sys/coerce sys/ldfrexp
    blas/blas
    permutation/init permutation/permutation permutation/permute
    multiroots/fdjac multiroots/fsolver multiroots/fdfsolver multiroots/convergence
    multiroots/newton multiroots/gnewton multiroots/dnewton multiroots/broyden
    multiroots/hybrid multiroots/hybridj)
  list(APPEND GSL_SRC ${ROOT}/gsl-an/${f}.c)
endforeach()
add_library(gsl-an STATIC ${GSL_SRC})
target_compile_definitions(gsl-an PRIVATE HAVE_CONFIG_H)

# ---- qfits-an -------------------------------------------------------------
set(QFITS_SRC)
foreach(f anqfits qfits_card qfits_convert qfits_error qfits_header qfits_image
    qfits_md5 qfits_table qfits_time qfits_tools qfits_byteswap qfits_memory
    qfits_rw qfits_float md5)
  list(APPEND QFITS_SRC ${ROOT}/qfits-an/${f}.c)
endforeach()
add_library(qfits STATIC ${QFITS_SRC})

# ---- libkd ----------------------------------------------------------------
set(KD_SRC)
foreach(f kdint_ddd kdint_fff kdint_lll kdint_ddu kdint_duu kdint_dds kdint_dss
    kdtree kdtree_dim kdtree_mem kdtree_fits_io
    dualtree dualtree_rangesearch dualtree_nearestneighbour)
  list(APPEND KD_SRC ${ROOT}/libkd/${f}.c)
endforeach()
add_library(kd STATIC ${KD_SRC})
target_include_directories(kd PRIVATE ${ROOT}/libkd)

# ---- util: anbase, anutils, anfiles --------------------------------------
set(UTIL_SRC)
foreach(f
    # ANBASE_OBJ
    starutil mathutil bl-sort bl bt healpix-utils healpix permutedsort ioutils
    fileutils md5 an-endian errors an-opts tic log datalog sparsematrix coadd
    convolve-image resample intmap histogram histogram2d
    # ANUTILS_OBJ
    sip-utils fit-wcs sip anwcs wcs-resample gslutils wcs-pv2sip matchobj
    fitsioutils sip_qfits fitstable fitsbin fitsfile
    # SIMPLEXY_OBJ
    dallpeaks dcen3x3 dfind dmedsmooth dobjects dpeaks dselip dsigma dsmooth
    image2xy simplexy ctmf
    # ANFILES_OBJ
    multiindex index indexset codekd starkd rdlist xylist starxy qidxfile
    quadfile scamp scamp-catalog tabsort wcs-xy2rd wcs-rd2xy matchfile)
  list(APPEND UTIL_SRC ${ROOT}/util/${f}.c)
endforeach()
add_library(anutil STATIC ${UTIL_SRC})
# These two files define functions as `inline void f() {...}` that their
# headers declare without `inline`. C99 makes that an external definition,
# but Clang for the MSVC target follows the Microsoft rule: it emits an
# `inline` function only in a file that calls it, and the callers in other
# files fail to link (`v32_ntoh`, `ra2hms` and others). Compiling the two
# files with `inline` defined away gives the external definitions that C99
# specifies.
set_source_files_properties(${ROOT}/util/an-endian.c ${ROOT}/util/starutil.c
  PROPERTIES COMPILE_DEFINITIONS "inline=")

# ---- solver: ENGINE_OBJS --------------------------------------------------
set(ENGINE_SRC)
foreach(f
    engine solverutils onefield solver quad-utils solvedfile tweak2 verify tweak
    new-wcs fits-guess-scale cut-table resort-xylist
    build-index uniformize-catalog startree hpquads quad-builder codefile
    codetree unpermute-stars unpermute-quads merge-index)
  list(APPEND ENGINE_SRC ${ROOT}/solver/${f}.c)
endforeach()
add_library(anengine STATIC ${ENGINE_SRC})
target_include_directories(anengine PRIVATE ${ROOT}/solver)

set(AN_LIBS anengine anutil kd qfits gsl-an ancompat)

# ---- upstream programs ----------------------------------------------------
# Each one is built from its upstream source file, unedited. `image2xy`
# needs cfitsio, `solve-field` and `augment-xylist` start other programs
# through a shell, and the plot programs need cairo and netpbm: none of them
# is built.
function(an_program name src)
  add_executable(${name} ${ROOT}/${src})
  target_include_directories(${name} PRIVATE ${ROOT}/solver)
  target_link_libraries(${name} ${AN_LIBS})
endfunction()

an_program(astrometry-engine solver/engine-main.c)
an_program(wcsinfo util/wcsinfo.c)
an_program(an-pnmtofits util/an-pnmtofits.c)
an_program(downsample-fits util/downsample-fits.c)
an_program(fits-column-merge util/fits-column-merge.c)
an_program(fits-flip-endian util/fits-flip-endian.c)
an_program(fitsgetext util/fitsgetext.c)
an_program(get-healpix util/get-healpix.c)
an_program(hpsplit util/hpsplit.c)
an_program(pad-file util/pad-file.c)
an_program(subtable util/subtable.c)
an_program(tabsort util/tabsort-main.c)
an_program(wcs-match util/wcs-match.c)
an_program(wcs-rd2xy util/wcs-rd2xy-main.c)
an_program(wcs-to-tan util/wcs-to-tan.c)
an_program(wcs-xy2rd util/wcs-xy2rd-main.c)
an_program(fits-guess-scale solver/fits-guess-scale-main.c)
an_program(new-wcs solver/new-wcs-main.c)
an_program(resort-xylist solver/resort-xylist-main.c)
an_program(startree solver/startree-main.c)
