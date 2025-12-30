#include <emscripten/bind.h>
#include "plaintext.hpp"

using namespace emscripten;

EMSCRIPTEN_BINDINGS(my_module) {
    class_<PlainText>("PlainText")
        .constructor<>()
        .function("size", &PlainText::size)
        .function("byteSize", &PlainText::byteSize)
        .function("empty", &PlainText::empty)
        .function("toString", &PlainText::toString)
        .function("slice", &PlainText::slice)
        .function("insert", &PlainText::insert)
        .function("del", &PlainText::del)
        .function("undo", &PlainText::undo)
        .function("redo", &PlainText::redo)
        .function("canUndo", &PlainText::canUndo)
        .function("canRedo", &PlainText::canRedo)
        ;
}
