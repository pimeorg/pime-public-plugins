# Discovers plugins/<role>/<id>/ and builds each as an independent package.
#
# A plugin directory is selected when its manifest runtime is buildable in the
# current configuration:
#   lua    -> always (package assembly only)
#   wasm   -> only under a wasm32 toolchain
#   native -> only with PIME_BUILD_TRUSTED_NATIVE=ON

set(PIME_KNOWN_ROLES preprocess parse translate rank format recognize learn predict service)

set(PIME_PLUGIN_TARGETS "" CACHE INTERNAL "registered plugin build targets")
set(PIME_PLUGIN_COUNT 0 CACHE INTERNAL "number of selected plugins")

function(pime_manifest_field manifest key out)
    file(STRINGS "${manifest}" _line REGEX "^${key}:")
    if(_line)
        string(REGEX REPLACE "^${key}:[ \t]*" "" _value "${_line}")
        string(STRIP "${_value}" _value)
    else()
        set(_value "")
    endif()
    set(${out} "${_value}" PARENT_SCOPE)
endfunction()

function(pime_discover_plugins)
    file(GLOB _manifests RELATIVE "${PIME_PLUGIN_ROOT}/plugins"
         "${PIME_PLUGIN_ROOT}/plugins/*/*/manifest.yaml")

    set(_count 0)
    foreach(_rel IN LISTS _manifests)
        string(REPLACE "\\" "/" _rel "${_rel}")
        string(REPLACE "/" ";" _parts "${_rel}")
        list(GET _parts 0 _role)
        list(GET _parts 1 _id)
        if(_role MATCHES "^_" OR _id MATCHES "^_")
            continue()
        endif()
        if(NOT _role IN_LIST PIME_KNOWN_ROLES)
            message(WARNING "plugin ${_rel}: unknown role directory '${_role}'")
            continue()
        endif()

        set(_manifest "${PIME_PLUGIN_ROOT}/plugins/${_rel}")
        pime_manifest_field("${_manifest}" runtime _runtime)
        pime_manifest_field("${_manifest}" role _manifest_role)

        if(NOT _manifest_role STREQUAL _role)
            message(WARNING "plugin ${_rel}: role '${_manifest_role}' != directory '${_role}'")
        endif()

        if(_runtime STREQUAL "wasm")
            if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm32")
                message(STATUS "skip plugin ${_id}: wasm32 toolchain not selected")
                continue()
            endif()
            pime_wasm_require_toolchain()
        elseif(_runtime STREQUAL "native")
            if(NOT PIME_BUILD_TRUSTED_NATIVE)
                message(STATUS "skip plugin ${_id}: trusted native disabled")
                continue()
            endif()
        elseif(_runtime STREQUAL "lua")
            if(NOT PIME_PACKAGE_LUA)
                continue()
            endif()
        else()
            message(WARNING "plugin ${_rel}: unknown runtime '${_runtime}'")
            continue()
        endif()

        add_subdirectory("${PIME_PLUGIN_ROOT}/plugins/${_role}/${_id}"
                         "${CMAKE_CURRENT_BINARY_DIR}/plugins/${_role}/${_id}")
        math(EXPR _count "${_count} + 1")
    endforeach()

    set(PIME_PLUGIN_COUNT ${_count} CACHE INTERNAL "number of selected plugins" FORCE)

    # Aggregate target: assembling every selected package in one build step.
    get_property(_targets GLOBAL PROPERTY PIME_PLUGIN_TARGETS)
    if(_targets)
        set(_deps "")
        foreach(_target IN LISTS _targets)
            list(APPEND _deps "${_target}_package")
        endforeach()
        add_custom_target(plugins_packages ALL DEPENDS ${_deps})
    endif()
endfunction()

# Called from a plugin CMakeLists after pime_add_plugin().
function(pime_register_plugin target)
    set_property(GLOBAL APPEND PROPERTY PIME_PLUGIN_TARGETS "${target}")
endfunction()

# Shared CMakeLists boilerplate for C++ (wasm / trusted native) plugins.
function(pime_add_cpp_plugin target runtime)
    cmake_parse_arguments(P "" "" "SOURCES;ASSETS;MODELS" ${ARGN})
    pime_add_plugin(${target}
        RUNTIME ${runtime}
        MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/manifest.yaml"
        SOURCES ${P_SOURCES}
        ASSETS ${P_ASSETS}
        MODELS ${P_MODELS}
        PACKAGE_DIR "${CMAKE_BINARY_DIR}/package")
    target_link_libraries(${target} PRIVATE pimeplug)
    # The SDK adds sdk/wasm/include; abi.h lives in the core include tree.
    target_include_directories(${target} PRIVATE "${PIME_CORE_DIR}/include")
    if(runtime STREQUAL "wasm")
        pime_wasm_configure_target(${target})
    endif()
    pime_register_plugin(${target})
endfunction()

# Lua plugins are single-file packages. Shared modules from shared/lua can be
# merged ahead of packaging with tools/luabundle, but the package itself must
# contain exactly one plugin.lua.
function(pime_add_lua_plugin target)
    pime_add_plugin(${target}
        RUNTIME lua
        MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/manifest.yaml"
        SOURCES plugin.lua
        PACKAGE_DIR "${CMAKE_BINARY_DIR}/package")
    pime_register_plugin(${target})
endfunction()
