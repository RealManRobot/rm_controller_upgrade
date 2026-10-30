# rm_upgrade_demo

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# binary: build/rm_upgrade_demo
```

The CMake configure step locates the library automatically from the sibling
`../rm_upgrade_demo`. Override the locations if your layout differs:

```bash
cmake -S . -B build \
      -DRM_UPGRADE_ROOT=/path/to/rm_upgrade_demo \
      -DRM_UPGRADE_BUILD=/path/to/rm_upgrade_demo/build
```

> Note: the demo links the **static** `upgrade_client` archive by default, so it
> also links `Threads` and (when `rm_upgrade_demo` was built with HTTPS) `OpenSSL`,
> which the archive depends on.

## Cross-platform builds

The demo supports the same three targets as `rm_upgrade_demo`. The one rule: point
`RM_UPGRADE_BUILD` at the **matching-architecture** build of `rm_upgrade_demo` — you
can't link an x86 library into an ARM64 binary. Build the corresponding
`rm_upgrade_demo` flavor first, then the demo.

**Linux x86_64** (native):

```bash
cmake -S ../rm_upgrade_demo -B ../rm_upgrade_demo/build && cmake --build ../rm_upgrade_demo/build -j
cmake -S . -B build -DRM_UPGRADE_BUILD=../rm_upgrade_demo/build
cmake --build build -j
```

**Linux ARM64** (cross, via the Arm GNU toolchain — see
`cmake/toolchains/aarch64-linux-gnu.cmake`):

```bash
# 1. rm_upgrade_demo for arm64 (HTTP-only keeps it dependency-free):
cmake -S ../rm_upgrade_demo -B ../rm_upgrade_demo/build-arm64 \
      -DCMAKE_TOOLCHAIN_FILE=../rm_upgrade_demo/cmake/toolchains/aarch64-linux-gnu.cmake \
      -DUPGRADE_ENABLE_HTTPS=OFF
cmake --build ../rm_upgrade_demo/build-arm64 -j
# 2. the demo, linking that arm64 library:
cmake -S . -B build-arm64 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
      -DRM_UPGRADE_BUILD=../rm_upgrade_demo/build-arm64
cmake --build build-arm64 -j
# verify: file build-arm64/rm_upgrade_demo  -> ARM aarch64
```

**Windows x64** (Visual Studio, no vcpkg):

```bat
:: 1. rm_upgrade_demo for Windows:
cmake -S ..\rm_upgrade_demo -B ..\rm_upgrade_demo\build -A x64 -DUPGRADE_ENABLE_HTTPS=OFF
cmake --build ..\rm_upgrade_demo\build --config Release
:: 2. the demo, linking that library:
cmake -S . -B build -A x64 -DRM_UPGRADE_BUILD=..\rm_upgrade_demo\build
cmake --build build --config Release
```

If `rm_upgrade_demo` was built with HTTPS on, the demo needs OpenSSL for the same
target too (it's auto-linked when CMake finds it; on Windows pass
`-DOPENSSL_ROOT_DIR=...`).

## Usage

Identical to the `rm_upgrade_demo` CLI — it drives the same upgrade flow
(`uploadUpgradeFile` → `getExtractList` → `startUpgrade` → `getUpgradeRate`):

```bash
./build/rm_upgrade_demo <host> <package_file> [options]
./build/rm_upgrade_demo 192.168.1.100 ./upgrade_pkg.tar
```

Connects to `http://<host>:8090` (the controller's default endpoint). Run with
`-h` for the full option list.
