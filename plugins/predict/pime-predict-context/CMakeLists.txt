pime_add_cpp_plugin(pime_predict_context wasm
    SOURCES src/plugin.cpp
    ASSETS assets/continuation.tsv assets/collocations.tsv)
