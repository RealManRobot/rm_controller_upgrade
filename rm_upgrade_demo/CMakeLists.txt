cmake_minimum_required(VERSION 3.14)

project(rm_upgrade_demo
    VERSION 1.0.0
    DESCRIPTION "Independent demo that links the upgrade_client library"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Locate the upgrade_client library from the sibling rm_upgrade project.
# This demo consumes the prebuilt library + its public header directly; build
# rm_upgrade first (see README). All three locations can be overridden.
# ---------------------------------------------------------------------------
set(RM_UPGRADE_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/lib/linux-x86_64"
    CACHE PATH "Directory holding the prebuilt libupgrade_client")

# NO_CMAKE_FIND_ROOT_PATH: the library lives in the host-side sibling project,
# not the target sysroot, so don't re-root these lookups when cross-compiling.
find_path(UPGRADE_CLIENT_INCLUDE_DIR upgrade_client.h
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/include"
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH)

# Prefer the static archive (so the demo binary has no runtime .so dependency);
# fall back to the shared library and the Windows static name.
set(_demo_saved_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib ${_demo_saved_suffixes})
find_library(UPGRADE_CLIENT_LIB
    NAMES upgrade_client_static upgrade_client   # static name first (Windows)
    HINTS "${RM_UPGRADE_BUILD}"
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH)
set(CMAKE_FIND_LIBRARY_SUFFIXES "${_demo_saved_suffixes}")

if(NOT UPGRADE_CLIENT_INCLUDE_DIR OR NOT UPGRADE_CLIENT_LIB)
    message(FATAL_ERROR
        "upgrade_client not found.\n"
        "Expected libupgrade_client in: ${RM_UPGRADE_BUILD}\n"
        "Populate lib/<platform>/ with the prebuilt library, or point\n"
        "-DRM_UPGRADE_BUILD at a directory that contains it.")
endif()
message(STATUS "rm_upgrade_demo: header  = ${UPGRADE_CLIENT_INCLUDE_DIR}")
message(STATUS "rm_upgrade_demo: library = ${UPGRADE_CLIENT_LIB}")

# ---------------------------------------------------------------------------
# The demo executable — same application as rm_upgrade/app, built standalone.
# ---------------------------------------------------------------------------
add_executable(rm_upgrade_demo app/main.cpp)
target_include_directories(rm_upgrade_demo PRIVATE ${UPGRADE_CLIENT_INCLUDE_DIR})
target_link_libraries(rm_upgrade_demo PRIVATE ${UPGRADE_CLIENT_LIB})

# When linking the static archive we must also satisfy its dependencies
# (Threads always; OpenSSL when rm_upgrade was built with HTTPS enabled).
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(rm_upgrade_demo PRIVATE Threads::Threads)

find_package(OpenSSL)
if(OpenSSL_FOUND)
    target_link_libraries(rm_upgrade_demo PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

if(WIN32)
    target_link_libraries(rm_upgrade_demo PRIVATE ws2_32)
    if(OpenSSL_FOUND)
        target_link_libraries(rm_upgrade_demo PRIVATE crypt32)
    endif()
endif()

if(MSVC)
    target_compile_options(rm_upgrade_demo PRIVATE /utf-8 /W3)
    target_compile_definitions(rm_upgrade_demo PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(rm_upgrade_demo PRIVATE -Wall -Wextra)
endif()

install(TARGETS rm_upgrade_demo RUNTIME DESTINATION bin)
