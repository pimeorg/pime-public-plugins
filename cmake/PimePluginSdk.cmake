# Locates the pime-core plugin SDK and reads the ABI / version it provides.
#
# Inputs:  PIME_CORE_DIR, PIME_SDK_DIR
# Outputs: PIME_SDK_DIR, PIME_CORE_DIR, PIME_CORE_VERSION,
#          PIME_ABI_MAJOR, PIME_ABI_MINOR, PIME_PLUGIN_ROOT

get_filename_component(PIME_PLUGIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT PIME_CORE_DIR)
    set(PIME_CORE_DIR "${PIME_PLUGIN_ROOT}/../pime-core")
endif()
get_filename_component(PIME_CORE_DIR "${PIME_CORE_DIR}" ABSOLUTE)

if(NOT PIME_SDK_DIR)
    set(PIME_SDK_DIR "${PIME_CORE_DIR}/sdk")
endif()
get_filename_component(PIME_SDK_DIR "${PIME_SDK_DIR}" ABSOLUTE)

if(NOT EXISTS "${PIME_SDK_DIR}/cmake/PimePlugin.cmake")
    message(FATAL_ERROR
        "pime plugin SDK not found at ${PIME_SDK_DIR}. "
        "Pass -DPIME_CORE_DIR=<pime-core> or -DPIME_SDK_DIR=<sdk>.")
endif()
include("${PIME_SDK_DIR}/cmake/PimePlugin.cmake")

set(PIME_CORE_VERSION "0.0.0")
if(EXISTS "${PIME_CORE_DIR}/CMakeLists.txt")
    file(STRINGS "${PIME_CORE_DIR}/CMakeLists.txt" _project_line
         REGEX "^project\\(pime_core[ \t]+VERSION[ \t]+[0-9]+\\.[0-9]+\\.[0-9]+")
    if(_project_line)
        string(REGEX REPLACE ".*VERSION[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1"
               PIME_CORE_VERSION "${_project_line}")
    endif()
endif()

set(PIME_ABI_MAJOR 1)
set(PIME_ABI_MINOR 0)
if(EXISTS "${PIME_CORE_DIR}/schema/abi.txt")
    file(STRINGS "${PIME_CORE_DIR}/schema/abi.txt" _abi_lines REGEX "^PIME_ABI_")
    foreach(_line IN LISTS _abi_lines)
        if(_line MATCHES "^PIME_ABI_MAJOR=([0-9]+)")
            set(PIME_ABI_MAJOR "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "^PIME_ABI_MINOR=([0-9]+)")
            set(PIME_ABI_MINOR "${CMAKE_MATCH_1}")
        endif()
    endforeach()
endif()

set(PIME_SDK_DIR "${PIME_SDK_DIR}" CACHE PATH "pime plugin SDK" FORCE)
