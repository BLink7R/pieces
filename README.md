# Pieces

**Pieces** is a novel data structure implementation designed to address a specific challenge in Conflict-free Replicated Data Types (CRDTs) for text editing: supporting undo/redo operations for range formatting.

Current CRDT implementations for text editing often share a common shortcoming, they do not support undo/redo well. For example, [Peritext](https://www.inkandswitch.com/peritext/) and [colla](https://github.com/nomad/cola) do not support undo/redo; [Loro]() and [Yjs]() only support undo/redo when there is no incoming operations from other peers; [zed](https://zed.dev/blog/crdts) only supports undo/redo for plain text. Specifically, handling overlapping range operations and their history in a distributed environment is complex.

`Pieces` introduces a new approach to handle these operations efficiently:

- **Tag-based Range Operations**: Range operations are stored using left and right tags.
- **History Tracking**: Each tag records its belonging operation and references the newest operation that is older than it.
- **Performance**: The complexity of range formatting, as well as undo/redo operations, is linear.

The project is in early state, the demo will be coming soon.