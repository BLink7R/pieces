# Pieces

**Pieces** is a novel data structure implementation designed to address a specific challenge in Conflict-free Replicated Data Types (CRDTs) for text editing: supporting undo/redo operations for non-binary range formatting (e.g., changing font color, font size).

Current CRDT implementations for text editing often share a common shortcoming， they do not support undo/redo([Peritext](https://www.inkandswitch.com/peritext/)，[colla](https://github.com/nomad/cola)), or only support undo/redo for binary formatting([zed](https://zed.dev/blog/crdts)), like deletion (as a hidden format), bold, italic. Specifically, handling overlapping range operations and their history in a distributed environment is complex.

`Pieces` introduces a new approach to handle these operations efficiently:

- **Tag-based Range Operations**: Range operations are stored using left and right tags.
- **History Tracking**: Each tag records its belonging operation and references the newest operation that is older than it.
- **Performance**: The complexity of range formatting, as well as undo/redo operations, is linear.

The project is in early state, the demo will be coming soon.