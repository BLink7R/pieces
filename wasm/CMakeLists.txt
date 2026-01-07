add_executable(pieces-wasm
    wasm_bindings.cpp
    ../src/text.cpp
)

target_include_directories(pieces-wasm PRIVATE ../src)

find_package(stduuid CONFIG REQUIRED)
find_package(utf8cpp CONFIG REQUIRED)
target_link_libraries(pieces-wasm PRIVATE stduuid utf8cpp::utf8cpp)

# Emscripten specific settings
target_link_options(pieces-wasm PRIVATE
    "-sWASM=1"
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sEXPORT_NAME='createPiecesModule'"
    "-sALLOW_MEMORY_GROWTH=1"
    "--bind"
)

set_target_properties(pieces-wasm PROPERTIES SUFFIX ".mjs")

# export em++ flags to compile commands for clangd
execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --cflags
    OUTPUT_VARIABLE EM_CFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REPLACE "'" "\"" EM_CFLAGS "${EM_CFLAGS}")
message(STATUS "EM_CFLAGS: ${EM_CFLAGS}")
set_target_properties(pieces-wasm PROPERTIES COMPILE_FLAGS "${EM_CFLAGS}")