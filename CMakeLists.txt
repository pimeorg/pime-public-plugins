cmake_minimum_required(VERSION 3.25)

# 聚合构建入口：只发现插件、转发 SDK，不承载任何业务。
project(pime_public_plugin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PIME_BUILD_TESTS "Build the minimal host and test drivers" OFF)
option(PIME_BUILD_TRUSTED_NATIVE "Build trusted native plugins" OFF)
option(PIME_PACKAGE_LUA "Assemble Lua plugin packages" ON)

set(PIME_CORE_DIR "" CACHE PATH "pime-core source tree (default ../pime-core)")
set(PIME_SDK_DIR "" CACHE PATH "pime plugin SDK (default <core>/sdk)")
set(PIME_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH "Package output")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(PimePluginSdk)
include(PimePluginWasm)
include(PimePlugins)

add_subdirectory(shared)

pime_discover_plugins()

if(PIME_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "pime-public-plugin: core ${PIME_CORE_VERSION} (ABI ${PIME_ABI_MAJOR}.${PIME_ABI_MINOR})")
message(STATUS "pime-public-plugin: ${PIME_PLUGIN_COUNT} plugin(s) selected, SDK ${PIME_SDK_DIR}")
