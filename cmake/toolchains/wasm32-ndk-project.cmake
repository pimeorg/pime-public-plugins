# Runs after project() in the wasm32-ndk toolchain.
#
# CMake's Generic platform marks TARGET_SUPPORTS_SHARED_LIBS as FALSE, which
# turns every add_library(MODULE) into a static archive. WASM modules link
# through wasm-ld, so restore module/shared linking for this build only.
#
# Plugins are standalone modules, not wasm "shared libraries": they must own
# their linear memory and __stack_pointer, so -shared is wrong (it turns those
# into imports of env). Undefined host functions stay imports via
# --allow-undefined; the core registers them under "env" at instantiation.

set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)

set(CMAKE_SHARED_MODULE_CREATE_C_FLAGS "-Wl,--allow-undefined")
set(CMAKE_SHARED_MODULE_CREATE_CXX_FLAGS "-Wl,--allow-undefined")
set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "-Wl,--allow-undefined")
set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "-Wl,--allow-undefined")

set(CMAKE_SHARED_MODULE_PREFIX "")
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_SHARED_MODULE_SUFFIX ".wasm")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".wasm")
