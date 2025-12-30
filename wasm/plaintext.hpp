#pragma once
#include <string>
#include <stack>
#include "piecetree.hpp"

class PlainText {
private:
    PieceCRDT doc;
    uint32_t local_stamp = 0;
    std::stack<OperationID> undo_stack;
    std::stack<OperationID> redo_stack;

public:
    PlainText();
    size_t size() const;
    size_t byteSize() const;
    bool empty() const;
    std::string toString() const;
    std::string slice(size_t begin, size_t end) const;
    size_t insert(size_t pos, const std::string &text);
    size_t del(size_t begin, size_t end);
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
};
