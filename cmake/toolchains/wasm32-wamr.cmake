# wasi-sdk toolchain for pime WASM plugins.
#
#   cmake -S . -B build-wasm \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/wasm32-wamr.cmake \
#         -DWASI_SDK_PATH=/path/to/wasi-sdk
#
# Produces freestanding wasm32 modules consumed by WAMR AOT.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

if(NOT WASI_SDK_PATH)
    message(FATAL_ERROR "WASI_SDK_PATH is required for the wasm32 toolchain")
endif()

set(CMAKE_C_COMPILER "${WASI_SDK_PATH}/bin/clang")
set(CMAKE_CXX_COMPILER "${WASI_SDK_PATH}/bin/clang++")
set(CMAKE_AR "${WASI_SDK_PATH}/bin/llvm-ar")
set(CMAKE_RANLIB "${WASI_SDK_PATH}/bin/llvm-ranlib")

set(_pime_wasm_flags "--target=wasm32 -O2 -fno-exceptions -fno-rtti")
string(APPEND CMAKE_C_FLAGS " ${_pime_wasm_flags}")
string(APPEND CMAKE_CXX_FLAGS " ${_pime_wasm_flags}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# No implicit sysroot: plugins are freestanding (no WASI imports).
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)
