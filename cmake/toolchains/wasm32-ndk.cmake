# wasm32 toolchain built on the Android NDK's clang + wasm-ld.
#
# The NDK ships a wasm-capable clang and wasm-ld, so freestanding pime plugins
# can be built on machines that have no wasi-sdk. No libc or WASI sysroot is
# used: the plugins only include <stdint.h>/<stddef.h> and import host
# functions from the core.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/wasm32-ndk.cmake \
#         -DANDROID_NDK_HOME=/path/to/ndk ...

if(NOT ANDROID_NDK_HOME)
    if(DEFINED ENV{ANDROID_NDK_HOME})
        set(ANDROID_NDK_HOME "$ENV{ANDROID_NDK_HOME}")
    elseif(DEFINED ENV{ANDROID_NDK_ROOT})
        set(ANDROID_NDK_HOME "$ENV{ANDROID_NDK_ROOT}")
    else()
        message(FATAL_ERROR
            "wasm32-ndk: pass -DANDROID_NDK_HOME=<ndk> or set ANDROID_NDK_HOME")
    endif()
endif()
get_filename_component(ANDROID_NDK_HOME "${ANDROID_NDK_HOME}" ABSOLUTE)
set(ANDROID_NDK_HOME "${ANDROID_NDK_HOME}" CACHE PATH "Android NDK used as wasm toolchain")
# Forward the NDK path into try_compile probe projects.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ANDROID_NDK_HOME)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(_pime_host_exe ".exe")
else()
    set(_pime_host_exe "")
endif()
string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" _pime_host_os)

set(_pime_ndk_bin
    "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/${_pime_host_os}-x86_64/bin")
if(NOT EXISTS "${_pime_ndk_bin}/clang${_pime_host_exe}")
    message(FATAL_ERROR "wasm32-ndk: clang not found under ${_pime_ndk_bin}")
endif()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

set(CMAKE_C_COMPILER "${_pime_ndk_bin}/clang${_pime_host_exe}")
set(CMAKE_CXX_COMPILER "${_pime_ndk_bin}/clang++${_pime_host_exe}")
set(CMAKE_AR "${_pime_ndk_bin}/llvm-ar${_pime_host_exe}")
set(CMAKE_RANLIB "${_pime_ndk_bin}/llvm-ranlib${_pime_host_exe}")

set(CMAKE_C_COMPILER_TARGET wasm32)
set(CMAKE_CXX_COMPILER_TARGET wasm32)

# Freestanding: the plugin ABI uses compiler-provided integer typedefs only.
set(CMAKE_C_FLAGS_INIT "-ffreestanding")
set(CMAKE_CXX_FLAGS_INIT "-ffreestanding")

# Compiler checks must not try to link an executable.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# The Generic platform disables shared/module targets; this include (run right
# after project()) restores them so plugins link as wasm modules.
set(CMAKE_PROJECT_INCLUDE
    "${CMAKE_CURRENT_LIST_DIR}/wasm32-ndk-project.cmake")
