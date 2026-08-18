use crate::id::OperationId;
use crate::types::{PieceInfo, StatePtr, TagStatus};

/// 内部文本坐标：指向某 segment 内某字符前/后。
/// pos > 0：第 pos 个字符之后（前闭）；pos < 0：倒数第 -pos 个字符之前（前开）。
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct StoredAnchor {
    pub seg: OperationId,
    pub pos: i32,
}

impl StoredAnchor {
    pub fn is_null(&self) -> bool {
        self.pos == 0
    }
    pub fn is_reversed(&self) -> bool {
        self.pos < 0
    }
    /// 到 segment 起始的字符距离。
    pub fn seg_pos(&self, seg_len: u32) -> u32 {
        if self.pos < 0 {
            (seg_len as i64 + self.pos as i64) as u32
        } else {
            self.pos as u32
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum StoredOperationKind {
    Insert,
    Delete,
    Undo,
    Redo,
}

/// 一条已落库的操作。Insert 携带 Segment 数据，Delete 携带区间，Undo/Redo 携带 target。
pub enum StoredOperation {
    Insert(Segment),
    Delete(StoredDeletion),
    Undo { target: OperationId },
    Redo { target: OperationId },
}

impl StoredOperation {
    pub fn kind(&self) -> StoredOperationKind {
        match self {
            StoredOperation::Insert(_) => StoredOperationKind::Insert,
            StoredOperation::Delete(_) => StoredOperationKind::Delete,
            StoredOperation::Undo { .. } => StoredOperationKind::Undo,
            StoredOperation::Redo { .. } => StoredOperationKind::Redo,
        }
    }
}

/// 一次插入操作。文本以字节存储（data），len 为字符数。
pub struct Segment {
    pub undoredo: Option<OperationId>,
    pub anchor: StoredAnchor,
    pub len: u32,
    pub data: Box<[u8]>,
    pub child: Vec<OperationId>,
    pub split_piece: Vec<PieceId>,
    pub undo_op: Option<StoredDeletion>,
}

pub type PieceId = usize;
pub type TagId = usize;

/// piece：一次插入操作的文本被其他插入划分后的一段。由 PieceTree 的 cell 拥有。
#[derive(Clone, Copy, Debug)]
pub struct Piece {
    pub seg: OperationId,
    pub byte_off: u32,
    pub len: u32,
    pub seg_pos: u32,
    pub tombstone: Option<OperationId>,
}

impl Piece {
    pub fn is_removed(&self) -> bool {
        self.tombstone.is_some()
    }
    pub fn info(&self) -> PieceInfo {
        let visible = if self.is_removed() { 0 } else { self.len as u64 };
        PieceInfo::new(self.len as u64, visible)
    }
}

/// 区间操作的端点。
#[derive(Clone, Debug)]
pub struct RangeTag {
    pub is_left: bool,
    pub status: TagStatus,
    pub anchor: StoredAnchor,
    pub cur: OperationId,
    pub old: StatePtr,
}

/// 删除操作（目前唯一的区间操作）。
pub struct StoredDeletion {
    pub undoredo: Option<OperationId>,
    pub left: TagId,
    pub right: TagId,
}
