# WASM plugin toolchain helpers.
#
# WASM plugins are freestanding: no WASI filesystem/network, no C runtime.
# The only memory they need is the scratch region declared in the manifest.

function(pime_wasm_require_toolchain)
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm32")
        message(FATAL_ERROR "pime_wasm_require_toolchain: not a wasm32 toolchain")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "pime WASM plugins require clang (wasi-sdk)")
    endif()
endfunction()

# Exports every WASM plugin must provide. PimePlugin.cmake builds the module;
# these link options keep it freestanding and deterministic.
function(pime_wasm_configure_target target)
    target_compile_options(${target} PRIVATE
        -O2 -fno-exceptions -fno-rtti -fvisibility=hidden
        -Wall -Wextra)
    target_link_options(${target} PRIVATE
        -nostdlib
        -Wl,--no-entry
        -Wl,--export=pime_init
        -Wl,--export=pime_process
        -Wl,--export=pime_on_event
        -Wl,--export=pime_shutdown
        -Wl,--export=pime_scratch_ptr
        -Wl,--export=pime_scratch_cap
        -Wl,--strip-all)
    set_target_properties(${target} PROPERTIES SUFFIX ".wasm")
endfunction()
