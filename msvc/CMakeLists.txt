# Windows build of the Astrometry.net solver core with Clang for the MSVC
# target (`clang-cl`, or `clang` with an `x86_64-pc-windows-msvc` target).
#
# This step builds only the compatibility layer, `ancompat`.
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

set(COMPAT ${CMAKE_CURRENT_SOURCE_DIR}/compat)

add_compile_definitions(
  _CRT_SECURE_NO_WARNINGS
  _CRT_NONSTDC_NO_WARNINGS
  _USE_MATH_DEFINES
  WIN32_LEAN_AND_MEAN
  NOMINMAX
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
# where the Windows SDK has none.
include_directories(BEFORE ${COMPAT})

# ---- compatibility layer -------------------------------------------------
add_library(ancompat STATIC
  ${COMPAT}/an_msvc_compat.c
  ${COMPAT}/an_msvc_regex.cpp
)
# `binmode.obj` ships with the C runtime and makes binary mode the default for
# `fopen` and `open`. The sources open FITS files with "r", "w" and "a", which
# is binary on POSIX but would translate line endings on Windows.
target_link_options(ancompat INTERFACE binmode.obj)
target_link_libraries(ancompat INTERFACE psapi)
