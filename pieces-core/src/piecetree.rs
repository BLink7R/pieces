use crate::store::Piece;
use crate::types::PieceInfo;

const ORDER: usize = 8;

/// B+ 树节点。parent/index 提供自叶向根的回溯，用于计算全局 offset。
struct Node {
    parent: Option<usize>,
    index: usize,
    is_leaf: bool,
    keys: Vec<PieceInfo>,
    children: Vec<usize>, // 叶子存 CellId，内部存 NodeId
    prev: Option<usize>,  // 叶子链表
    next: Option<usize>,
}

/// 叶子单元格：value 内联存储，索引稳定（grow-only）。
pub struct Cell {
    node: usize,
    index: usize,
    value: Piece,
}

/// grow-only B+ 树（序列版，汇总键为 PieceInfo）。
pub struct PieceTree {
    nodes: Vec<Node>,
    cells: Vec<Cell>,
    root: usize,
    first: usize,
    last: usize,
}

impl PieceTree {
    pub fn new(initial: Piece) -> Self {
        let info = initial.info();
        let cells = vec![Cell {
            node: 0,
            index: 0,
            value: initial,
        }];
        let nodes = vec![Node {
            parent: None,
            index: 0,
            is_leaf: true,
            keys: vec![info],
            children: vec![0],
            prev: None,
            next: None,
        }];
        PieceTree {
            nodes,
            cells,
            root: 0,
            first: 0,
            last: 0,
        }
    }

    pub fn begin(&self) -> usize {
        self.nodes[self.first].children[0]
    }

    pub fn last_cell(&self) -> usize {
        let leaf = &self.nodes[self.last];
        *leaf.children.last().unwrap()
    }

    pub fn piece(&self, cell: usize) -> Piece {
        self.cells[cell].value
    }

    pub fn next_cell(&self, cell: usize) -> Option<usize> {
        let c = &self.cells[cell];
        let node = &self.nodes[c.node];
        if c.index + 1 < node.children.len() {
            Some(node.children[c.index + 1])
        } else {
            let next = node.next?;
            Some(self.nodes[next].children[0])
        }
    }

    pub fn prev_cell(&self, cell: usize) -> Option<usize> {
        let c = &self.cells[cell];
        let node = &self.nodes[c.node];
        if c.index > 0 {
            Some(node.children[c.index - 1])
        } else {
            let prev = node.prev?;
            let p = &self.nodes[prev];
            Some(*p.children.last().unwrap())
        }
    }

    pub fn visible_total(&self) -> u64 {
        self.nodes[self.root].keys.iter().map(|k| k.visible).sum()
    }

    /// 计算某 cell 的全局前缀 offset（等价于 C++ Iterator::update）。
    pub fn offset(&self, cell: usize) -> PieceInfo {
        let mut off = PieceInfo::default();
        let mut index = self.cells[cell].index;
        let mut node = self.cells[cell].node;
        loop {
            off += self.nodes[node].keys[..index].iter().copied().sum::<PieceInfo>();
            match self.nodes[node].parent {
                None => break,
                Some(p) => {
                    index = self.nodes[node].index;
                    node = p;
                }
            }
        }
        off
    }

    fn find_generic(
        &self,
        pos: u64,
        metric: fn(&PieceInfo) -> u64,
        loose: bool,
    ) -> Option<(usize, PieceInfo)> {
        let mut node = self.root;
        let mut acc = PieceInfo::default();
        let mut acc_m = 0u64;
        loop {
            let n = &self.nodes[node];
            let mut idx = 0;
            while idx < n.keys.len() {
                let m = metric(&n.keys[idx]);
                let stop = if loose { pos <= acc_m + m } else { pos < acc_m + m };
                if stop {
                    break;
                }
                acc += n.keys[idx];
                acc_m += m;
                idx += 1;
            }
            if n.is_leaf {
                if idx >= n.keys.len() {
                    return None;
                }
                return Some((n.children[idx], acc));
            }
            node = n.children[idx];
        }
    }

    pub fn find_upper_bound(&self, pos: u64) -> Option<(usize, PieceInfo)> {
        self.find_generic(pos, |p| p.visible, false)
    }

    pub fn find_lower_bound(&self, pos: u64) -> Option<(usize, PieceInfo)> {
        self.find_generic(pos, |p| p.visible, true)
    }

    #[allow(dead_code)] // 供 RangeTree 的 historyPos 使用（区间操作落地后启用）
    pub fn find_history(&self, pos: u64) -> Option<(usize, PieceInfo)> {
        self.find_generic(pos, |p| p.total, false)
    }

    /// 在 pos 之前插入：Some(cell) 表示插在该 cell 前，None 表示插到末尾。
    pub fn insert_before(&mut self, pos: Option<usize>, value: Piece) -> usize {
        let cell_id = self.cells.len();
        self.cells.push(Cell {
            node: 0,
            index: 0,
            value,
        });
        let (leaf, idx) = match pos {
            None => {
                let leaf = self.last;
                let idx = self.nodes[leaf].keys.len();
                (leaf, idx)
            }
            Some(c) => {
                let cell = &self.cells[c];
                (cell.node, cell.index)
            }
        };
        self.insert_cell_into_leaf(leaf, idx, cell_id);
        cell_id
    }

    pub fn insert_after(&mut self, cell: usize, value: Piece) -> usize {
        let next = self.next_cell(cell);
        self.insert_before(next, value)
    }

    /// 将 piece 在 char_pos 个字符处切分为两段，返回右侧新 cell。
    /// byte_split 为切分点在 segment 数据中的字节偏移（绝对）。
    pub fn split(&mut self, cell: usize, char_pos: u32, byte_split: u32) -> usize {
        let piece = self.cells[cell].value;
        debug_assert!(char_pos > 0 && char_pos < piece.len);
        self.cells[cell].value.len = char_pos;
        let (node, index) = (self.cells[cell].node, self.cells[cell].index);
        let left_info = self.cells[cell].value.info();
        self.nodes[node].keys[index] = left_info;
        self.update_ancestors(node);

        let mut right = piece;
        right.byte_off = byte_split;
        right.len -= char_pos;
        right.seg_pos += char_pos;
        self.insert_after(cell, right)
    }

    fn insert_cell_into_leaf(&mut self, leaf: usize, idx: usize, cell_id: usize) {
        let info = self.cells[cell_id].value.info();
        {
            let n = &mut self.nodes[leaf];
            n.keys.insert(idx, info);
            n.children.insert(idx, cell_id);
        }
        let children = self.nodes[leaf].children.clone();
        for (i, &c) in children.iter().enumerate() {
            self.cells[c].node = leaf;
            self.cells[c].index = i;
        }
        if self.nodes[leaf].keys.len() > ORDER {
            self.split_node(leaf);
        } else {
            self.update_ancestors(leaf);
        }
    }

    fn summarize(&self, node: usize) -> PieceInfo {
        self.nodes[node].keys.iter().copied().sum()
    }

    fn update_ancestors(&mut self, mut node: usize) {
        loop {
            let (parent, index) = (self.nodes[node].parent, self.nodes[node].index);
            let p = match parent {
                Some(p) => p,
                None => break,
            };
            let new_key = self.summarize(node);
            if self.nodes[p].keys[index] != new_key {
                self.nodes[p].keys[index] = new_key;
                node = p;
            } else {
                break;
            }
        }
    }

    fn split_node(&mut self, node: usize) {
        let mid = ORDER / 2;
        let is_leaf = self.nodes[node].is_leaf;
        let parent = self.nodes[node].parent;
        let index = self.nodes[node].index;
        let (right_keys, right_children) = {
            let n = &mut self.nodes[node];
            (n.keys.split_off(mid), n.children.split_off(mid))
        };
        let new_node = self.nodes.len();
        self.nodes.push(Node {
            parent: None,
            index: 0,
            is_leaf,
            keys: right_keys,
            children: right_children,
            prev: None,
            next: None,
        });

        if is_leaf {
            let children = self.nodes[new_node].children.clone();
            for (i, &c) in children.iter().enumerate() {
                self.cells[c].node = new_node;
                self.cells[c].index = i;
            }
            let old_next = self.nodes[node].next;
            self.nodes[new_node].prev = Some(node);
            self.nodes[new_node].next = old_next;
            self.nodes[node].next = Some(new_node);
            if let Some(nxt) = old_next {
                self.nodes[nxt].prev = Some(new_node);
            } else {
                self.last = new_node;
            }
        } else {
            let children = self.nodes[new_node].children.clone();
            for (i, &c) in children.iter().enumerate() {
                self.nodes[c].parent = Some(new_node);
                self.nodes[c].index = i;
            }
        }

        let left_summary = self.summarize(node);
        let right_summary = self.summarize(new_node);
        match parent {
            None => {
                let new_root = self.nodes.len();
                self.nodes.push(Node {
                    parent: None,
                    index: 0,
                    is_leaf: false,
                    keys: vec![left_summary, right_summary],
                    children: vec![node, new_node],
                    prev: None,
                    next: None,
                });
                self.nodes[node].parent = Some(new_root);
                self.nodes[node].index = 0;
                self.nodes[new_node].parent = Some(new_root);
                self.nodes[new_node].index = 1;
                self.root = new_root;
            }
            Some(p) => {
                let p_index = index;
                {
                    let pn = &mut self.nodes[p];
                    pn.keys[p_index] = left_summary;
                    pn.keys.insert(p_index + 1, right_summary);
                    pn.children.insert(p_index + 1, new_node);
                }
                self.nodes[new_node].parent = Some(p);
                self.nodes[new_node].index = p_index + 1;
                let children = self.nodes[p].children.clone();
                for (i, &c) in children.iter().enumerate() {
                    self.nodes[c].index = i;
                }
                if self.nodes[p].keys.len() > ORDER {
                    self.split_node(p);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::id::OperationId;
    use uuid::Uuid;

    fn piece(seg: u8, byte_off: u32, len: u32, seg_pos: u32) -> Piece {
        Piece {
            seg: OperationId::new(Uuid::from_u128(seg as u128), 1),
            byte_off,
            len,
            seg_pos,
            tombstone: None,
        }
    }

    #[test]
    fn offset_after_splits() {
        let mut tree = PieceTree::new(piece(1, 0, 10, 0));
        let c0 = tree.begin();
        assert_eq!(tree.offset(c0).total, 0);
        assert_eq!(tree.offset(c0).visible, 0);

        // split 10 -> 4 + 6
        let c1 = tree.split(c0, 4, 4);
        assert_eq!(tree.piece(c0).len, 4);
        assert_eq!(tree.piece(c1).len, 6);
        assert_eq!(tree.offset(c0).total, 0);
        assert_eq!(tree.offset(c1).total, 4);

        // insert a new piece before c1
        let c2 = tree.insert_before(Some(c1), piece(2, 0, 3, 0));
        assert_eq!(tree.offset(c2).total, 4);
        assert_eq!(tree.offset(c1).total, 7);

        // many inserts to force leaf splits
        let mut last = c1;
        for i in 0..40 {
            last = tree.insert_after(last, piece(3, 0, 1, i));
        }
        // verify prefix offsets are monotonic and contiguous
        let mut prev = 0u64;
        let mut cell = Some(tree.begin());
        let mut n = 0;
        while let Some(c) = cell {
            let off = tree.offset(c).total;
            assert_eq!(off, prev, "cell {c} prefix mismatch");
            prev += tree.piece(c).len as u64;
            cell = tree.next_cell(c);
            n += 1;
        }
        assert_eq!(n, 43); // 3 + 40
        assert_eq!(prev, 10 + 3 + 40);
    }
}
