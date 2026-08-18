use std::cmp::Ordering;
use std::collections::BTreeMap;

use crate::id::{generate_replica_id, OperationId, ReplicaId, Stamp};
use crate::op::{Anchor, Operation, OperationKind};
use crate::piecetree::PieceTree;
use crate::store::{Piece, PieceId, Segment, StoredAnchor, StoredOperation};

/// 文本 CRDT：segment 树 + piece 树，所有权集中，交叉引用均为 ID。
pub struct PieceCRDT {
    lamport_stamp: u32,
    local_id: ReplicaId,
    origin_id: ReplicaId,
    ops: BTreeMap<ReplicaId, BTreeMap<Stamp, StoredOperation>>,
    piece_tree: PieceTree,
}

const EOF_STAMP: Stamp = 1;

impl PieceCRDT {
    pub fn new(origin: ReplicaId) -> Self {
        let local_id = generate_replica_id();
        let origin_id = if origin.is_nil() { local_id } else { origin };

        let eof_replica = ReplicaId::nil();
        let eof_id = OperationId::new(eof_replica, EOF_STAMP);
        let eof = Segment {
            undoredo: None,
            anchor: StoredAnchor {
                seg: eof_id,
                pos: 0,
            },
            len: 1,
            data: vec![0u8].into_boxed_slice(),
            child: Vec::new(),
            split_piece: Vec::new(),
            undo_op: None,
        };
        let piece = Piece {
            seg: eof_id,
            byte_off: 0,
            len: 1,
            seg_pos: 0,
            tombstone: None,
        };
        let piece_tree = PieceTree::new(piece);
        let eof_cell = piece_tree.begin();

        let mut ops = BTreeMap::new();
        ops.entry(eof_replica)
            .or_insert_with(BTreeMap::new)
            .insert(EOF_STAMP, StoredOperation::Insert(eof));
        if let Some(StoredOperation::Insert(seg)) = ops
            .get_mut(&eof_replica)
            .and_then(|r| r.get_mut(&EOF_STAMP))
        {
            seg.split_piece.push(eof_cell);
        }

        PieceCRDT {
            // EOF 占用 stamp 1，用户操作从 2 开始，保证 EOF 恒比真实操作“旧”
            lamport_stamp: EOF_STAMP + 1,
            local_id,
            origin_id,
            ops,
            piece_tree,
        }
    }

    pub fn id(&self) -> ReplicaId {
        self.local_id
    }

    pub fn origin(&self) -> ReplicaId {
        self.origin_id
    }

    pub fn stamp(&self) -> Stamp {
        self.lamport_stamp
    }

    pub fn size(&self) -> usize {
        // 总可见字符数 = 根汇总（去掉 EOF 的 1 个字符）
        self.piece_tree.visible_total().saturating_sub(1) as usize
    }

    pub fn to_string(&self) -> String {
        let mut res = String::new();
        let last = self.piece_tree.last_cell();
        let mut cell = Some(self.piece_tree.begin());
        while let Some(c) = cell {
            if c == last {
                break;
            }
            let piece = self.piece_tree.piece(c);
            if !piece.is_removed() {
                res.push_str(std::str::from_utf8(self.piece_bytes(&piece)).unwrap());
            }
            cell = self.piece_tree.next_cell(c);
        }
        res
    }

    pub fn apply(&mut self, op: &Operation) -> bool {
        match &op.kind {
            OperationKind::Insert { anchor, text } => self.insert(op.id, *anchor, text.clone()),
            _ => false,
        }
    }

    fn insert(&mut self, id: OperationId, anchor: Anchor, text: String) -> bool {
        if text.is_empty() {
            return false;
        }
        let stored = match self.to_stored(anchor) {
            Some(s) => s,
            None => return false,
        };
        if !self.store_segment(id, text, stored) {
            return false;
        }
        self.insert_segment(id);
        true
    }

    fn store_segment(&mut self, id: OperationId, text: String, anchor: StoredAnchor) -> bool {
        if let Some(rep) = self.ops.get(&id.replica) {
            if rep.contains_key(&id.stamp) {
                return false;
            }
        }
        let len = text.chars().count() as u32;
        let data: Box<[u8]> = text.into_bytes().into_boxed_slice();
        self.lamport_stamp = self.lamport_stamp.max(id.stamp + 1);
        let seg = Segment {
            undoredo: None,
            anchor,
            len,
            data,
            child: Vec::new(),
            split_piece: Vec::new(),
            undo_op: None,
        };
        self.ops
            .entry(id.replica)
            .or_default()
            .insert(id.stamp, StoredOperation::Insert(seg));
        true
    }

    fn insert_segment(&mut self, id: OperationId) {
        let anchor = self.anchor_of(id);
        let parent_id = anchor.seg;
        let parent_len = self.segment_len(parent_id);
        let split_piece = self.split_piece_list(parent_id);

        let piece_id = match self.piece_at(parent_len, &split_piece, anchor.pos) {
            Some(p) => p,
            None => return,
        };
        let piece = self.piece_tree.piece(piece_id);
        let segpos = anchor.seg_pos(parent_len);
        let pos_in_piece = segpos as i64 - piece.seg_pos as i64;
        debug_assert!(pos_in_piece >= 0 && pos_in_piece <= piece.len as i64);

        let child = self.child_list(parent_id);
        let idx = self.child_insert_index(parent_len, &child, anchor, id);

        let new_len = self.segment_len(id);
        let new_piece = Piece {
            seg: id,
            byte_off: 0,
            len: new_len,
            seg_pos: 0,
            tombstone: None,
        };

        if pos_in_piece == 0 {
            let mut target = piece_id;
            if let Some(&after_id) = child.get(idx) {
                if self.anchor_pos_of(after_id) == anchor.pos {
                    target = self.leftmost_descendant(after_id);
                }
            }
            let cell = self.piece_tree.insert_before(Some(target), new_piece);
            self.add_piece(id, cell);
        } else if pos_in_piece == piece.len as i64 {
            let mut target = piece_id;
            if idx > 0 {
                if let Some(&before_id) = child.get(idx - 1) {
                    if self.anchor_pos_of(before_id) == anchor.pos {
                        target = self.rightmost_descendant(before_id);
                    }
                }
            }
            let cell = self.piece_tree.insert_after(target, new_piece);
            self.add_piece(id, cell);
        } else {
            let byte_split = self.piece_byte_offset_after_chars(&piece, pos_in_piece as u32);
            let right = self.piece_tree.split(piece_id, pos_in_piece as u32, byte_split);
            self.add_split_piece(parent_id, right);
            let cell = self.piece_tree.insert_before(Some(right), new_piece);
            self.add_piece(id, cell);
        }

        self.insert_child(parent_id, id, idx);
    }

    fn leftmost_descendant(&self, id: OperationId) -> PieceId {
        let mut cur = id;
        loop {
            let child = self.child_list(cur);
            if child.is_empty() {
                break;
            }
            let first = child[0];
            if self.anchor_pos_of(first) != 0 {
                break;
            }
            cur = first;
        }
        self.first_piece_of(cur)
    }

    fn rightmost_descendant(&self, id: OperationId) -> PieceId {
        let mut cur = id;
        loop {
            let child = self.child_list(cur);
            if child.is_empty() {
                break;
            }
            let last = *child.last().unwrap();
            if self.anchor_pos_of(last) != self.segment_len(cur) as i32 {
                break;
            }
            cur = last;
        }
        self.last_piece_of(cur)
    }

    fn piece_at(&self, parent_len: u32, split_piece: &[PieceId], pos: i32) -> Option<PieceId> {
        if pos == 0 || (pos.unsigned_abs() > parent_len) {
            return None;
        }
        let idx = if pos > 0 {
            let position = pos as u32;
            split_piece.partition_point(|&p| self.piece_tree.piece(p).seg_pos + self.piece_tree.piece(p).len < position)
        } else {
            let position = (parent_len as i64 + pos as i64) as u32;
            split_piece.partition_point(|&p| self.piece_tree.piece(p).seg_pos + self.piece_tree.piece(p).len <= position)
        };
        split_piece.get(idx).copied()
    }

    fn to_stored(&self, anchor: Anchor) -> Option<StoredAnchor> {
        let id = OperationId::new(anchor.replica, anchor.stamp);
        let seg = self.segment_of(id)?;
        if anchor.pos == 0 || anchor.pos.unsigned_abs() > seg.len {
            return None;
        }
        Some(StoredAnchor { seg: id, pos: anchor.pos })
    }

    pub fn pos(&self, anchor: Anchor) -> Option<usize> {
        let stored = self.to_stored(anchor)?;
        let parent_len = self.segment_len(stored.seg);
        let split_piece = self.split_piece_list(stored.seg);
        let piece_id = self.piece_at(parent_len, &split_piece, stored.pos)?;
        let piece = self.piece_tree.piece(piece_id);
        if piece.is_removed() {
            return Some(self.piece_tree.offset(piece_id).visible as usize);
        }
        let segpos = stored.seg_pos(parent_len);
        Some((self.piece_tree.offset(piece_id).visible + (segpos - piece.seg_pos) as u64) as usize)
    }

    pub fn anchor(&self, pos: usize) -> Anchor {
        if pos == 0 {
            return Anchor::default();
        }
        let (piece_id, prefix) = match self.piece_tree.find_lower_bound(pos as u64) {
            Some(x) => x,
            None => return Anchor::default(),
        };
        let piece = self.piece_tree.piece(piece_id);
        Anchor {
            replica: piece.seg.replica,
            stamp: piece.seg.stamp,
            pos: (pos as u64 - prefix.visible + piece.seg_pos as u64) as i32,
        }
    }

    pub fn reversed_anchor(&self, pos: usize) -> Anchor {
        let (piece_id, prefix) = match self.piece_tree.find_upper_bound(pos as u64) {
            Some(x) => x,
            None => return Anchor::default(),
        };
        let piece = self.piece_tree.piece(piece_id);
        let seg_len = self.segment_len(piece.seg);
        Anchor {
            replica: piece.seg.replica,
            stamp: piece.seg.stamp,
            pos: (pos as i64 - prefix.visible as i64 + piece.seg_pos as i64 - seg_len as i64) as i32,
        }
    }

    /// Fugue 规则：选择插入位置两侧更新的 piece 作为锚点，避免 interleaving。
    pub fn insert_anchor(&self, pos: usize) -> Anchor {
        let (piece_id, prefix) = match self.piece_tree.find_upper_bound(pos as u64) {
            Some(x) => x,
            None => return Anchor::default(),
        };
        let piece = self.piece_tree.piece(piece_id);
        if pos > 0 && pos as u64 == prefix.visible {
            // 恰在某 piece 起始处：比较左右 segment 的新旧
            let seg_right = piece.seg;
            let seg_right_len = self.segment_len(seg_right);
            let left_piece = self.piece_tree.prev_cell(piece_id).map(|c| self.piece_tree.piece(c));
            let use_right = match left_piece {
                None => true,
                Some(lp) => lp.seg == seg_right || lp.seg.cmp(&seg_right) != Ordering::Greater,
            };
            if use_right {
                Anchor {
                    replica: seg_right.replica,
                    stamp: seg_right.stamp,
                    pos: piece.seg_pos as i32 - seg_right_len as i32,
                }
            } else {
                let lp = left_piece.unwrap();
                Anchor {
                    replica: lp.seg.replica,
                    stamp: lp.seg.stamp,
                    pos: (lp.seg_pos + lp.len) as i32,
                }
            }
        } else {
            let seg_len = self.segment_len(piece.seg);
            Anchor {
                replica: piece.seg.replica,
                stamp: piece.seg.stamp,
                pos: (pos as i64 - prefix.visible as i64 + piece.seg_pos as i64 - seg_len as i64) as i32,
            }
        }
    }

    fn piece_byte_offset_after_chars(&self, piece: &Piece, chars: u32) -> u32 {
        let data = self.segment_data(piece.seg);
        let from = piece.byte_off as usize;
        let s = std::str::from_utf8(&data[from..]).unwrap();
        let byte = s
            .char_indices()
            .nth(chars as usize)
            .map(|(i, _)| i)
            .unwrap_or(s.len());
        (from + byte) as u32
    }

    fn piece_bytes(&self, piece: &Piece) -> &[u8] {
        let data = self.segment_data(piece.seg);
        let from = piece.byte_off as usize;
        let s = std::str::from_utf8(&data[from..]).unwrap();
        let byte_len = s
            .char_indices()
            .nth(piece.len as usize)
            .map(|(i, _)| i)
            .unwrap_or(s.len());
        &data[from..from + byte_len]
    }

    // —— 内部访问器 ——

    fn segment_of(&self, id: OperationId) -> Option<&Segment> {
        self.ops.get(&id.replica)?.get(&id.stamp).and_then(|op| match op {
            StoredOperation::Insert(seg) => Some(seg),
            _ => None,
        })
    }

    fn segment_of_mut(&mut self, id: OperationId) -> Option<&mut Segment> {
        self.ops
            .get_mut(&id.replica)?
            .get_mut(&id.stamp)
            .and_then(|op| match op {
                StoredOperation::Insert(seg) => Some(seg),
                _ => None,
            })
    }

    fn segment_len(&self, id: OperationId) -> u32 {
        self.segment_of(id).map(|s| s.len).unwrap_or(0)
    }

    fn segment_data(&self, id: OperationId) -> &[u8] {
        self.segment_of(id).map(|s| s.data.as_ref()).unwrap_or(&[])
    }

    fn anchor_of(&self, id: OperationId) -> StoredAnchor {
        self.segment_of(id)
            .map(|s| s.anchor)
            .unwrap_or(StoredAnchor { seg: id, pos: 0 })
    }

    fn anchor_pos_of(&self, id: OperationId) -> i32 {
        self.anchor_of(id).pos
    }

    fn child_list(&self, id: OperationId) -> Vec<OperationId> {
        self.segment_of(id).map(|s| s.child.clone()).unwrap_or_default()
    }

    fn split_piece_list(&self, id: OperationId) -> Vec<PieceId> {
        self.segment_of(id)
            .map(|s| s.split_piece.clone())
            .unwrap_or_default()
    }

    fn first_piece_of(&self, id: OperationId) -> PieceId {
        self.segment_of(id)
            .and_then(|s| s.split_piece.first().copied())
            .unwrap_or(0)
    }

    fn last_piece_of(&self, id: OperationId) -> PieceId {
        self.segment_of(id)
            .and_then(|s| s.split_piece.last().copied())
            .unwrap_or(0)
    }

    fn add_piece(&mut self, id: OperationId, cell: PieceId) {
        if let Some(seg) = self.segment_of_mut(id) {
            seg.split_piece.push(cell);
        }
    }

    fn add_split_piece(&mut self, id: OperationId, cell: PieceId) {
        let segpos = self.piece_tree.piece(cell).seg_pos;
        let split_piece = self.split_piece_list(id);
        let idx = split_piece
            .partition_point(|&p| self.piece_tree.piece(p).seg_pos < segpos);
        if let Some(seg) = self.segment_of_mut(id) {
            seg.split_piece.insert(idx, cell);
        }
    }

    fn child_insert_index(
        &self,
        parent_len: u32,
        child: &[OperationId],
        anchor: StoredAnchor,
        id: OperationId,
    ) -> usize {
        child.partition_point(|&c| {
            let ca = self.anchor_of(c);
            child_cmp(parent_len, ca, c, anchor, id) == Ordering::Less
        })
    }

    fn insert_child(&mut self, parent_id: OperationId, id: OperationId, idx: usize) {
        if let Some(seg) = self.segment_of_mut(parent_id) {
            seg.child.insert(idx, id);
        }
    }
}

fn child_cmp(
    parent_len: u32,
    a: StoredAnchor,
    a_id: OperationId,
    b: StoredAnchor,
    b_id: OperationId,
) -> Ordering {
    let a_sp = a.seg_pos(parent_len);
    let b_sp = b.seg_pos(parent_len);
    if a_sp != b_sp {
        return a_sp.cmp(&b_sp);
    }
    if a.pos != b.pos {
        return b.pos.cmp(&a.pos);
    }
    if a.pos > 0 {
        a_id.cmp(&b_id)
    } else {
        b_id.cmp(&a_id)
    }
}
