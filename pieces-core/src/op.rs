use crate::id::{OperationId, ReplicaId, Stamp};

/// 线上格式的操作（不含内部指针，可序列化/同步）。
/// 对应 C++ `crdt.hpp` 的 Operation 层次。
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct Anchor {
    pub replica: ReplicaId,
    pub stamp: Stamp,
    pub pos: i32,
}

impl Anchor {
    pub fn is_null(&self) -> bool {
        self.pos == 0
    }
    pub fn is_reversed(&self) -> bool {
        self.pos < 0
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct OpenedRange {
    pub begin: Anchor,
    pub end: Anchor,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct ClosedRange {
    pub begin: Anchor,
    pub end: Anchor,
}

#[derive(Clone, Debug)]
pub enum OperationKind {
    Insert { anchor: Anchor, text: String },
    Delete { range: ClosedRange },
    Undo { target: OperationId },
    Redo { target: OperationId },
}

#[derive(Clone, Debug)]
pub struct Operation {
    pub id: OperationId,
    pub kind: OperationKind,
}

impl Operation {
    pub fn insert(replica: ReplicaId, stamp: Stamp, anchor: Anchor, text: String) -> Self {
        Operation {
            id: OperationId::new(replica, stamp),
            kind: OperationKind::Insert { anchor, text },
        }
    }
    pub fn delete(replica: ReplicaId, stamp: Stamp, range: ClosedRange) -> Self {
        Operation {
            id: OperationId::new(replica, stamp),
            kind: OperationKind::Delete { range },
        }
    }
    pub fn undo(replica: ReplicaId, stamp: Stamp, target: OperationId) -> Self {
        Operation {
            id: OperationId::new(replica, stamp),
            kind: OperationKind::Undo { target },
        }
    }
    pub fn redo(replica: ReplicaId, stamp: Stamp, target: OperationId) -> Self {
        Operation {
            id: OperationId::new(replica, stamp),
            kind: OperationKind::Redo { target },
        }
    }
}
