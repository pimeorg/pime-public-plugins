# Header-only shared code compiled into each plugin. Nothing here becomes a
# runtime shared library; plugins copy what they use at build time.
add_library(pimeplug INTERFACE)
target_include_directories(pimeplug INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/cpp/include")
target_compile_features(pimeplug INTERFACE cxx_std_20)
