# Rust 迁移方案（方案 A：arena + index）

对应 C++ 源码 `src/`（`textcrdt.hpp`、`piecetree.hpp`、`gb+tree.hpp`、`rangetree.hpp`、`storedop.hpp`、`crdt.hpp`、`text.hpp/cpp`）。设计文档见 `design.md`。

> 状态：**进行中**。workspace 已建立，`pieces-core` 已实现第 1–4 步（基础类型、PieceTree、插入路径），见第 9 节进度标记与第 10 节实现修正。

## 1. 目标与原则

- 所有权集中到单一 arena，交叉引用一律用稳定 ID/句柄，消除裸指针环。
- 全程 safe Rust，不引入 `unsafe`、不引入 `Rc<RefCell<T>>` 这类运行时 borrow 方案。
- 保持 grow-only 语义：B+ 树与 arena 只增不删，因此索引作为句柄永不失效。
- 严格区分字节偏移与字符偏移（`design.md` 命名约定：`offset`/`size` 为字节，`pos`/`length` 为字符），修复 C++ 里 `Piece::len`（字符）被 `toString` 当字节用的隐患（见第 8 节）。

## 2. 现状要点（C++ 怎么解决）

1. **稳定地址 + 集中所有权**：对象堆分配在 `Replica::operations`（`storedop.hpp:25`），`resize` 只移动 `unique_ptr` 不移动对象；B+ 树叶子 cell 独立堆分配（`PinnedCell`，`gb+tree.hpp:204`），分裂时搬 cell 指针不搬 cell，故 `Piece*`（=`&cell->value`）永不失效。
2. **回溯父节点 + 迭代器缓存**：每个 `Node` 带 `parent`/`index`（`gb+tree.hpp:19-20`），`Iterator::update()`（`:434-443`）自叶向根累加左兄弟汇总得到前缀 offset 并缓存；`find()`（`:490-511`）下探累加，`++/--` 增量维护。

## 3. 总体方案

一个 `Doc` 拥有所有数据，交叉引用改为句柄：

- `Segment`/操作 → `OperationId { replica, stamp }`（`Anchor` 已如此定位 segment，`textcrdt.hpp:715-734`）。
- `Piece`/`RangeTag` → 其所在 B+ 树 cell 的稳定索引（`PieceId`/`TagId`）。
- 所有权环（`Segment.split_piece ↔ Piece.seg`、`Segment.child ↔ anchor.seg`、`RangeTag.cur ↔ RangeOp.left/right`）全部打断为单向 ID 边。

## 4. 数据模型

### 4.1 基础类型与 ID

```rust
type ReplicaId = uuid::Uuid;      // 原 ReplicaID
type Stamp = u32;                 // 原 stamp_size_t

// 注意：Ord 必须手写为 (stamp, replica)，与 C++ `StoredOperation::operator<` 一致，
// 不能用 derive（derive 按字段序 = replica 在前，错误）。相等性仍为逐字段相等。
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
struct OperationId { replica: ReplicaId, stamp: Stamp }
impl Ord for OperationId { /* stamp.cmp 再 replica.cmp */ }

// 文本坐标：pos>0 表示“某字符之后”（前闭），pos<0 表示“某字符之前”（前开），0 非法
struct StoredAnchor { seg: OperationId, pos: i32 }

// 原 StatedPtr<T>（nullptr / good / bad）—— bad 表示“未确定，待重建天际线时填”
enum StatePtr { Null, Op(OperationId), Bad }
```

### 4.2 文档与所有权

```rust
struct PieceCRDT {
    // 原 Replica::operations；稀疏存储以容忍乱序到达（见第 8 节）
    ops: BTreeMap<ReplicaId, BTreeMap<Stamp, StoredOperation>>,
    piece_tree: PieceTree,   // 拥有全部 Piece（cell 索引即 PieceId）
    range_tree: RangeTree,   // 拥有全部 RangeTag（cell 索引即 TagId）—— 未实现
    lamport_stamp: u32,
    local_id: ReplicaId,
    origin_id: ReplicaId,
}

// 原 StoredOperation 继承体系 + 虚函数 type() —— 收拢为 enum
enum StoredOperation {
    Insert(Segment),
    Delete(StoredDeletion),          // 目前唯一的区间操作
    Undo { target: OperationId },    // 原 StoredUndo
    Redo  { target: OperationId },   // 原 StoredRedo
}
```

说明：`replicas` 集合省略，`ops` 的键即为副本集合。EOF 哨兵段以 `ReplicaId::nil()` + `stamp = 1` 存储，用户操作 stamp 从 2 起（见第 10 节）。

### 4.3 Segment / Piece

```rust
struct Segment {
    undoredo: Option<OperationId>,  // 原 UndoRedoableOp::undoredo（LWW）
    anchor: StoredAnchor,
    len: u32,                       // 字符数（原 piece_size_t len）
    data: Box<[u8]>,                // 原 unique_ptr<const char[]>；拥有字节
    line_breaks: Vec<u32>,          // 换行符的字符偏移
    child: Vec<OperationId>,        // 段树孩子（原 Vec<Segment*>）
    split_piece: Vec<PieceId>,      // 本 segment 的 piece，按 seg_pos 排序（原 Vec<Piece*>）
    undo_op: Option<StoredDeletion>,// 撤销插入时创建的隐藏删除
}

struct Piece {
    seg: OperationId,               // 原 Segment*
    byte_off: u32,                  // 原 const char* data —— 改为相对 seg.data 的字节偏移
    len: u32,                       // 字符数
    seg_pos: u32,                   // 在 segment 内的字符偏移
    tombstone: Option<OperationId>, // 原 StoredRangeOp*，覆盖此 piece 的最新删除
}

#[derive(Clone, Copy, Default, PartialEq, Eq)]
struct PieceInfo { total: u64, visible: u64 }  // 字符数汇总；total 含墓碑、visible 不含
impl std::ops::Add for PieceInfo { /* total、visible 各自相加 */ }
```

### 4.4 区间操作 / RangeTag

```rust
#[derive(Clone, Copy, PartialEq, Eq)]
enum TagStatus { Active, Undone, UnUsed }

struct RangeTag {
    is_left: bool,
    status: TagStatus,
    anchor: StoredAnchor,
    cur: OperationId,          // 所属 Delete 操作
    old: StatePtr,             // 天际线的“上一层”（null=初始，bad=待重建）
}

struct StoredDeletion {
    undoredo: Option<OperationId>,
    left: TagId,               // 原 RangeTag*
    right: TagId,
}
```

## 5. 核心问题一：segments ↔ pieces 相互引用

### 5.1 为什么安全 Rust 不行

`Segment.split_piece: Vec<&Piece>` 与 `Piece.seg: &Segment` 构成所有权环，borrow checker 拒绝；即使换 `Rc` 也是环导致泄漏。因此必须打断所有权、以 ID 为边。

### 5.2 候选对比

| 方案 | 结论 |
| --- | --- |
| A. arena + index | 推荐。无 unsafe、无运行时 borrow、句柄可序列化、可独立校验 |
| B. `Rc<RefCell<T>>`/`Weak` | 内部可变性泛滥、运行时 borrow panic、环需 `Weak`、性能差 |
| C. 裸指针 + `unsafe` | 照搬 C++，但须手满足更严的别名规则，全部可变字段套 `UnsafeCell`，成本高 |

### 5.3 方案 A 细节

- `Segment*` → `OperationId`：segments 用 `(replica, stamp)` 定位，`Anchor`/`toStored` 已采用。
- `Piece*` → `PieceId`：piece 是 `PieceTree` 叶子 cell 里的 value，用 cell 索引作句柄。树 grow-only，索引永不失效，等价复刻 `PinnedCell` 的稳定地址语义，且不需要 `cellOf` 的指针算术。
- `const char* data` → `(seg, byte_off)`：读文本时 `ops[seg].data[byte_off..]`。`len`/`seg_pos` 是字符、`byte_off` 是字节，UTF-8 换算用 `str::char_indices`（对应 C++ `utf8cpp::advance`）。
- 排序的 `std::lower_bound`（`child`/`split_piece`/`pieceAt`）→ `slice::partition_point`。

## 6. 核心问题二：从 piece 计算全局 offset

### 6.1 B+ 树 arena 化

```rust
struct PieceTree {
    nodes: Vec<Node>,   // grow-only，NodeId = usize
    cells: Vec<Cell>,   // grow-only，CellId = usize（= PieceId）
    root: usize,
    first: usize,       // 首叶子 NodeId
    last: usize,        // 末叶子 NodeId
}

struct Node {
    parent: Option<usize>,
    index: usize,           // 原 Node::index（在父 children 中的位置）
    is_leaf: bool,
    keys: Vec<PieceInfo>,   // 汇总键（每个 child/子树一个）
    children: Vec<usize>,   // 叶子存 CellId，内部存 NodeId
    prev: Option<usize>,    // 叶子链表（无哨兵，末尾用 None 表示）
    next: Option<usize>,
}

struct Cell { node: usize, index: usize, value: Piece }
```

注：实现未沿用 C++ 的 `TaggedPtr<Cell, Sentinel>`，直接以 `Option<usize>` 表示链表终点；也未做独立泛型层，`PieceTree` 直接专用化（RangeTree 后续再抽共性）。

### 6.2 offset()

等价于 `Iterator::update()`（`gb+tree.hpp:434-443`），自叶向根累加左侧兄弟汇总：

```rust
impl PieceTree {
    fn offset(&self, cell: CellId) -> PieceInfo {
        let mut off = PieceInfo::default();
        let mut index = self.cells[cell].index as usize;
        let mut node = self.cells[cell].node;
        loop {
            off += self.nodes[node].keys[..index].iter().sum();
            match self.nodes[node].parent {
                None => break,
                Some(p) => { index = self.nodes[node].index as usize; node = p; }
            }
        }
        off
    }
}
```

- `find(pos)`：下探累加前缀（对应 `gb+tree.hpp:490-511`），返回 `(cell, 前缀)`。
- 迭代器缓存：`struct Iter { cell: CellId, offset: PieceInfo }`，`next()/prev()` 增量维护（对应 `:454-477`）。
- 两种语义：`findHistory` 用 `total`、`upper/lower_bound` 用 `visible`（`piecetree.hpp:30-54`）→ 给 `find` 传不同比较闭包。

### 6.3 RangeTree 与跨树借用

`RangeTree::addTag`（`rangetree.hpp:39-68`）的排序键依赖 `piece_tree.historyPos`，且会 `piece_tree.split` 修改 piece 树。Rust 里两棵树是 `Doc` 的独立字段，方法须把另一棵树作为显式参数传入，靠字段级借用分离编译通过：

```rust
// doc.del 内：
let (left, right) = doc.range_tree.apply(left_tag, right_tag, &mut doc.piece_tree);
```

`apply` 签名 `fn apply(&mut self, left: RangeTag, right: RangeTag, piece_tree: &mut PieceTree)`。闭包捕获的是参数 `piece_tree` 而非 `self`，避免同时可变借用 `self.range_tree` 与 `self.piece_tree` 冲突。

## 7. 迁移点对照表

| C++ | Rust |
| --- | --- |
| 继承 + 虚函数 `type()` | `enum StoredOperation` + `match` |
| `TaggedPtr`（指针打 tag） | `enum CellRef { Cell(usize), End }` |
| `StatedPtr`（good/bad/null） | `enum StatePtr { Null, Op(OperationId), Bad }` |
| `PinnedCell::cellOf` 指针算术 | 直接传 `CellId` |
| `mutable` 成员（const 方法改字段） | 方法取 `&mut self` |
| `unique_ptr<const char[]>` | `Box<[u8]>` / `String` |
| `utf8cpp::advance/distance` | `str::char_indices` / 手写字节↔字符换算 |
| `std::lower_bound` | `partition_point` |
| `reinterpret_cast` / 裸指针 | 无（全部索引化） |
| `std::unique_ptr` / 手动 `new` | arena `Vec` + 索引，天然所有权 |

## 8. 需一并处理的隐患（迁移时修复）

1. **稠密 stamp 假设**：`storeOp`（`textcrdt.hpp:747-752`）用 `stamp < maxStamp()` 拒绝 + `resize(stamp+1)`，`diff` 从 stamp=2 起遍历。同 replica 乱序到达会被拒/留空。方案 A 已用 `BTreeMap<Stamp, _>` 稀疏存储，但需在 `apply` 里显式处理“先到后 stamp 后到前 stamp”的缓冲。
2. **字节/字符混用**：`Piece::len` 是字符数，但 `size()`（`storedop.hpp:321-325`）与 `toString`（`textcrdt.hpp:92` `append(data, len)`）按字节使用，多字节 UTF-8 下错误。Rust 侧汇总键按需同时记录 `total/visible` 的字节与字符（扩展 `PieceInfo`），导出层严格用字节切片。
3. **`PieceTree::split` 为 O(piece len)**（`storedop.hpp:300` TODO 自认），迁移时可评估 piece 长度上限。

## 9. 落地顺序与进度

- [x] 1. 基础类型 + ID + 序列化 `enum Operation`（`id.rs`/`types.rs`/`op.rs`/`store.rs`）。
- [x] 2. B+ 树 arena 化：`PieceTree`，含 `find/offset/insert/split`（`piecetree.rs`）。
- [x] 3. `PieceCRDT` 所有权 + `store_segment/to_stored`（`textcrdt.rs`）。
- [x] 4. 插入路径：`insert` + `insert_anchor`（Fugue 规则）+ `split_piece` 维护。
- [ ] 5. `RangeTree` + 区间操作：`redoDel/undoDel/redoRangeOp/undoRangeOp`。
- [ ] 6. 撤销/重做：`undo/redo` 分发 + `undoInsertion/redoInsertion`。
- [ ] 7. `PlainText` 层：栈、`beginGroup/endGroup` 哨兵。
- [ ] 8. `diff/apply/frontline` 同步。
- [x] 9. 测试：B+ 树 offset、插入/toString、anchor/pos 往返、多字节 UTF-8（`piecetree.rs` 单测 + `tests/basic.rs`）。

## 10. 实现中的修正与决策

1. **`OperationId` 的 `Ord` 必须手写为 `(stamp, replica)`**：`derive` 按字段序（replica 在前）会与 C++ `StoredOperation::operator<`（stamp 在前）不符，导致并发插入的确定性排序错误。相等性仍 `derive`（逐字段）。
2. **EOF 段占 `stamp = 1`，用户操作从 `stamp = 2` 起**：C++ 构造函数 `storeOp<Segment>(nil, 1, "\0")` 会把 `lamport_stamp` 抬到 2。若 Rust 侧 `lamport_stamp` 从 0 起步，首个用户段 stamp 0 < EOF 的 stamp 1，`insert_anchor` 末尾插入会误选“右（EOF）”，把文本插到开头。修复：`lamport_stamp = EOF_STAMP + 1`。
3. **`insert_anchor` 的方向**：C++ 条件 `seg_left == seg_right || *seg_left < *seg_right` 意为“右更新/相等则用右（reversed anchor），否则左更新用左（normal anchor）”，对应 Rust `lp.seg.cmp(&seg_right) != Ordering::Greater`。曾写反为 `seg_right < lp.seg`，导致 `sequential_insert` 失败。
4. **`split` 的字节偏移在 CRDT 层计算**：`Piece::data` 指针改为 `(seg, byte_off)` 后，切分需字符→字节换算，须读 segment 数据。为避免借用冲突，`PieceTree::split(cell, char_pos, byte_split)` 由调用方（`PieceCRDT`）先算好 `byte_split` 再传入，树自身不碰 segment。
5. **`Piece::len` 语义**：字符数。`size()` 汇总、`find` 比较均按字符；`to_string` 导出时用 `char_indices` 把字符数转字节长度切片（避开 C++ `append(data, len)` 把字符当字节的隐患，见第 8 节）。
6. **`find_history` 暂标记 `#[allow(dead_code)]`**：语义（按 `total`）已实现，供 `RangeTree::addTag` 的 `historyPos` 落地后启用。
7. **`Cell` 内部字段（`node`/`index`）未公开**：`PieceTree` 提供 `piece()`/`next_cell()`/`prev_cell()`/`offset()` 等访问器，避免外部直接依赖索引布局。
