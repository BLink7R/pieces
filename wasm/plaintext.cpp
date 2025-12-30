#include "plaintext.hpp"

PlainText::PlainText() = default;

size_t PlainText::size() const { return doc.size(); }
size_t PlainText::byteSize() const { return doc.size(); }
bool PlainText::empty() const { return doc.size() == 0; }

std::string PlainText::toString() const { return doc.toString(); }

std::string PlainText::slice(size_t begin, size_t end) const {
    std::string s = doc.toString();
    if (begin >= s.size()) return "";
    if (end > s.size()) end = s.size();
    return s.substr(begin, end - begin);
}

size_t PlainText::insert(size_t pos, const std::string &text) {
    Anchor anchor = doc.anchor(pos);
    Insertion op(doc.id(), ++local_stamp, anchor, text);
    doc.insert(op);
    undo_stack.push({doc.id(), local_stamp});
    while(!redo_stack.empty()) redo_stack.pop();
    return local_stamp;
}

size_t PlainText::del(size_t begin, size_t end) {
    Anchor anchorBegin = doc.anchor(begin);
    Anchor anchorEnd = doc.anchor(end);
    Deletion op(doc.id(), ++local_stamp, anchorBegin, anchorEnd);
    doc.del(op);
    undo_stack.push({doc.id(), local_stamp});
    while(!redo_stack.empty()) redo_stack.pop();
    return local_stamp;
}

bool PlainText::canUndo() const { return !undo_stack.empty(); }
bool PlainText::canRedo() const { return !redo_stack.empty(); }

void PlainText::undo() {
    if (undo_stack.empty()) return;
    OperationID target = undo_stack.top();
    undo_stack.pop();
    
    UndoOperation op(doc.id(), ++local_stamp, target);
    doc.undo(op);
    redo_stack.push(target);
}

void PlainText::redo() {
    if (redo_stack.empty()) return;
    OperationID target = redo_stack.top();
    redo_stack.pop();
    
    RedoOperation op(doc.id(), ++local_stamp, target);
    doc.redo(op);
    undo_stack.push(target);
}
