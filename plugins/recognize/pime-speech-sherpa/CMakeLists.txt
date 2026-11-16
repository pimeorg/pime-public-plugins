cmake_minimum_required(VERSION 3.25)

# Native plugin: Android (and Windows for local smoke tests) gets a real
# sherpa-onnx recognizer.  Hosts build this as a shared library, then package
# it as `plugin.so` next to the manifest.
project(pime_speech_sherpa LANGUAGES CXX)

set(PIME_PLUGIN_SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/cpp/include"
    CACHE PATH "pime plugin shared C++ headers")

add_library(pime_speech_sherpa SHARED src/plugin.cpp)
target_include_directories(pime_speech_sherpa PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PIME_CORE_DIR}/include"
    "${PIME_CORE_DIR}/sdk/native/include"
    "${PIME_PLUGIN_SHARED_DIR}")
target_compile_features(pime_speech_sherpa PRIVATE cxx_std_20)
target_compile_options(pime_speech_sherpa PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -fvisibility=hidden>)

if(ANDROID OR UNIX)
    target_link_libraries(pime_speech_sherpa PRIVATE dl log)
endif()
