# Test scaffolding: native simulator for the WASM plugin logic plus the
# conformance/chain drivers that need a real core build.
#
# The simulator compiles each wasm plugin's sources natively and links them
# against host stubs. It exercises the plugin's CBOR handling and business
# logic; it deliberately does not emulate the core.

file(GLOB _plugin_manifests
     RELATIVE "${PIME_PLUGIN_ROOT}/plugins"
     "${PIME_PLUGIN_ROOT}/plugins/*/*/manifest.yaml")

set(PIME_SIM_TARGETS "")
foreach(_rel IN LISTS _plugin_manifests)
    string(REPLACE "\\" "/" _rel "${_rel}")
    string(REPLACE "/" ";" _parts "${_rel}")
    list(GET _parts 0 _role)
    list(GET _parts 1 _id)
    if(_role MATCHES "^_" OR _id MATCHES "^_")
        continue()
    endif()
    set(_manifest "${PIME_PLUGIN_ROOT}/plugins/${_rel}")
    pime_manifest_field("${_manifest}" runtime _runtime)
    if(NOT _runtime STREQUAL "wasm")
        continue()
    endif()
    string(REPLACE "-" "_" _target "sim_${_id}")
    add_executable(${_target}
        "${PIME_PLUGIN_ROOT}/plugins/${_role}/${_id}/src/plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/host/sim_main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/host/sim_stubs.cpp")
    target_include_directories(${_target} PRIVATE
        "${PIME_PLUGIN_ROOT}/shared/cpp/include"
        "${PIME_SDK_DIR}/wasm/include"
        "${PIME_CORE_DIR}/include")
    set_target_properties(${_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sim")
    if(MINGW)
        # Avoid picking up a different libstdc++ from PATH at test time.
        target_link_options(${_target} PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    list(APPEND PIME_SIM_TARGETS ${_target})
endforeach()

if(PIME_SIM_TARGETS)
    add_custom_target(pime_sim ALL DEPENDS ${PIME_SIM_TARGETS})
    add_test(NAME plugin-cases
        COMMAND python "${PIME_PLUGIN_ROOT}/tests/run_cases.py"
                --root "${PIME_PLUGIN_ROOT}"
                --sim-dir "${CMAKE_BINARY_DIR}/sim")
endif()

# Minimal core host; only built when a pime::core package is available.
find_package(pime_core CONFIG QUIET)
if(pime_core_FOUND)
    add_executable(pime_case_host host/main.cpp)
    target_link_libraries(pime_case_host PRIVATE pime::core)
    add_test(NAME conformance
        COMMAND python "${PIME_PLUGIN_ROOT}/tests/conformance/run.py"
                --root "${PIME_PLUGIN_ROOT}")
endif()
