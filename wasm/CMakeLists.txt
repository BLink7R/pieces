add_executable(pieces-wasm
    wasm_bindings.cpp
    plaintext.cpp
)

find_package(stduuid CONFIG REQUIRED)
find_package(utf8cpp CONFIG REQUIRED)

target_include_directories(pieces-wasm PRIVATE ../src)
target_link_libraries(pieces-wasm PRIVATE stduuid utf8cpp::utf8cpp)

# Emscripten specific settings
target_link_options(pieces-wasm PRIVATE
    "-sWASM=1"
    "-sMODULARIZE=1"
    "-sEXPORT_NAME='createPiecesModule'"
    "-sALLOW_MEMORY_GROWTH=1"
    "--bind"
)

set_target_properties(pieces-wasm PROPERTIES SUFFIX ".js")
