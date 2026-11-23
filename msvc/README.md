# Astrometry.net solver on Windows

This directory builds the solver core of Astrometry.net as native Windows
programs, with Clang that targets the MSVC ABI and against the Microsoft C
runtime. It holds a compatibility layer for the POSIX headers that the
sources include, a CMake build and tests. Two upstream files get a `_WIN32`
conditional (see the end).

## Requirements

- Clang for the MSVC target: `clang-cl`, or `clang` with the
  `x86_64-pc-windows-msvc` target
- The Windows SDK and the MSVC runtime (Visual Studio Build Tools)
- CMake 3.20 or later and Ninja

## Build and test

From an "x64 Native Tools Command Prompt":

    cmake -G Ninja -S msvc -B msvc/build -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
    cmake --build msvc/build
    msvc\build\test_compat.exe

With `clang`, use `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`.
Extra flags go through `CMAKE_C_FLAGS`. `test_compat.exe` prints "all
compatibility checks passed". `cl` cannot compile these sources.

## What is built

The libraries of `util`, `libkd`, `qfits-an`, `gsl-an` and `solver`, and
`astrometry-engine`, `wcsinfo`, `an-pnmtofits`, `downsample-fits`,
`fits-column-merge`, `fits-flip-endian`, `fitsgetext`, `get-healpix`,
`hpsplit`, `pad-file`, `subtable`, `tabsort`, `wcs-match`, `wcs-rd2xy`,
`wcs-to-tan`, `wcs-xy2rd`, `fits-guess-scale`, `new-wcs`, `resort-xylist`
and `startree`, each from its upstream source.

Not built: `image2xy` (needs cfitsio), `solve-field` and `augment-xylist`
(start other programs through a shell), `an-fitstopnm`, everything that
needs netpbm or cairo, the index building programs, and the Python modules.

## Compatibility layer

`msvc/compat` comes first on the include path. It holds headers with POSIX
names (`unistd.h`, `sys/mman.h`, `dirent.h`, `getopt.h`, `pthread.h`,
`regex.h` and others), implemented in `an_msvc_compat.c` and
`an_msvc_regex.cpp` over the Win32 API and `std::regex`.
`an_msvc_prelude.h` is included ahead of every C file: a 64-bit `off_t`,
`stat` as `_stat64`, POSIX functions that the C runtime declares under
another name, and the `Inline*` macros that `keywords.h` leaves undefined
without `__GNUC__`. `msvc/config` holds the two headers that the Makefile
build generates, and `msvc/test/test_compat.c` checks the layer.

The two conditionals: `qfits-an/qfits_time.c` includes `<pwd.h>` without
using it, and `run_command_get_outputs()` in `util/ioutils.c` (`fork`,
`pipe`, `select`, `waitpid`) returns an error on Windows.
