# Cross-compile from an x86_64 host to an ARM64 (aarch64) Linux target using the
# Arm GNU toolchain installed under /opt.
#
# Usage:
#   cmake -S . -B build-arm64 \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-arm64 -j
#
# Toolchain root and triple can be overridden on the command line, e.g.
#   -DTOOLCHAIN_ROOT=/opt/other-toolchain -DCROSS_TRIPLE=aarch64-linux-gnu
#
# For an HTTPS build you need ARM64 OpenSSL inside the toolchain's sysroot (or a
# separate sysroot via -DCMAKE_SYSROOT=...); otherwise build with
# -DUPGRADE_ENABLE_HTTPS=OFF (HTTP-only, this tool's default runtime mode).

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Installed Arm GNU toolchain.
set(TOOLCHAIN_ROOT "/opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu"
    CACHE PATH "Root of the aarch64 cross toolchain")
set(CROSS_TRIPLE "aarch64-none-linux-gnu"
    CACHE STRING "Cross toolchain triple")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_ROOT}/bin/${CROSS_TRIPLE}-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/${CROSS_TRIPLE}-g++")

# Sysroot shipped with the Arm GNU toolchain; override with -DCMAKE_SYSROOT=...
# if you use a separate target rootfs.
if(NOT CMAKE_SYSROOT)
    set(CMAKE_SYSROOT "${TOOLCHAIN_ROOT}/${CROSS_TRIPLE}/libc")
endif()

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")

# Find host-side build tools normally, but headers/libraries/packages only in
# the target sysroot so we never pick up x86_64 artifacts.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
