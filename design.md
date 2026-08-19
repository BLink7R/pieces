## CRDT定义

CRDT（Conflict-free Replicated Data Type，无冲突复制数据类型）是一种支持多副本并发编辑的数据结构：各副本独立接收并应用操作，不依赖中央协调或共识，最终收敛到一致状态。

CRDT的收敛性依赖操作满足以下性质：

- **交换性（commutativity）**：对于没有依赖关系的操作，任意改变顺序，应用后结果不变
- **结合性（associativity）**：对操作集合的任意分组，应用后结果不变
- **幂等性（idempotence）**：同一操作重复应用，结果不变

**依赖关系**：一个操作引用另一个操作的产物时，两者存在依赖关系，例如一次插入以另一次插入的文字为坐标，则插入依赖后者。有依赖的操作必须保持相对顺序，其一致性由依赖传播保证；无依赖（并发）的操作则通过确定性排序规则（时钟编号+协作者id）统一各副本的应用顺序，保证任意到达顺序下合并结果一致。

## 已有方法

### text coordinate

核心思路：

1. 每个插入操作一个唯一id

2. 每个插入操作记录其插入时前/后的文字属于哪个插入操作

   通过上面方法将插入操作形成一颗树状结构。

   例:

- 原文字`hello world`

- A在`he`之后插入`lll`变成`helllllo world`

- B同时`hello `后插入`! `变成`hello ! world`

  此时要合并A和B的操作,如果通过文字在文档中的绝对位置,则必须进行操作变换

  而通过text coordinate记录,则两个操作能够正确合并

### tombstone

删除文字时不在记录中删除，而是在文字上标记删除操作（墓碑）。墓碑遵循last write wins的规则。

### 基于标签的区间操作

![图片](http://www.kdocs.cn/api/v3/office/copy/V1J0blRocDFWT2dWYjVwLy9hM291ZElrRnYwOTY5THhhOHJ2NDZ0YUNxbWFxYmNvRmpZbll3bk83ZlRGeXYrWldjMjRmbmJFODNMTzJaU3N5QkdtanRUSHdkMFcwOG41cFlmRGpuMk40d1dWcmlxVFZIOFFKYk9HZTZXcXgwbFNOTUJseW5XUlc3TnBzZy9XYnh2a0cxV29pZXJwVXVMVmRUSVR1ckdQZHNwTGtKWWNFNktOZFJZQURxU1VLTDUzbjVBOEhzY0YxaVBmZDNnaXNndW9Xa242SFgvMExZb2ROMEJkTkxyOGcrTjhvdzNHbWdvZFFKZjBYbkk0aE5YbFNJYTM2aHdpQkdvPQ==/attach/object/ERHQAUJIABAFW?&kso_type=image&kso_extra=eyJ0eXBlIjoiaW1hZ2UiLCJpZCI6IkVSSFFBVUpJQUJBRlciLCJvd25lciI6IjUzNjEyNzE0NTA3NiIsInJvdGF0ZSI6MCwic3RvcmFnZSI6ImJhc2UiLCJ3aWR0aCI6MTMxNSwiaGVpZ2h0Ijo0NDR9)

peritext中提出的通过位置标签来表示文字style。每个文字有前/后两个位置记录当前生效的mark集合。

基于节点的标记方法是无状态的，易于添加和重做。

### Fugue

两个协作者在同一个位置逐字插入，此时没有做packing的话就会导致合并后两个人的插入逐字交替。[Fugue](https://arxiv.org/abs/2305.00583)是专门为消除interleaving problem提出的列表/文本CRDT：

- 每个元素有唯一id（lamport时钟 + 协作者id）；插入操作携带leftOrigin（左邻元素id）与可选的rightOrigin（右邻元素id），无依赖的并发元素按id确定性排序
- 核心是origin的选择策略（non-interleaving origin）：插入时leftOrigin不取"光标紧邻的前一个元素"，而是取插入点左侧**与本次插入不并发（因果上早于它）的最靠右元素**
- 效果：单个用户连续逐字输入时，每个新字符的origin是刚输入的前一个字符（因果可知），形成连续链；并发用户插入的字符对当前用户不可见，被排除在origin选择之外。合并后两个用户的连续串各自保持连续，按id确定性排列为`abcxyz`而非`axbycz`，从而消除interleaving
- 代价：确定origin需越过插入点左侧所有并发元素（需要扫描），并依赖"哪些元素已被该用户看到"的因果可见性
- 论文将RGA/YATA视为采用不同origin策略的Fugue特例，并证明Fugue在最小化interleaving的意义下是最优的

### Yjs

Yjs的序列CRDT基于YATA（Yet Another Transformation Approach）算法：

- 每次插入是一个item，包含唯一id（client id + 单调递增的clock），以及origin（左邻item id）和originRight（右邻item id）
- 并发插入到同一位置（origin相同）时，按origin/creator/clock的确定性规则排序，保证各副本合并结果一致
- 删除通过标记deleted实现（墓碑），支持gc清除不再需要的item
- 富文本格式与文本结构分离：格式（bold等）以Format item存储。格式区间不是区间对象，而是item链上的开关item——区间起点一个`(key,value)`项，终点一个`(key,null)`取反项（覆盖旧值时终点插入旧值），某位置的生效属性是从链头走到该位置的累积结果
- 撤销由应用层UndoManager实现，机制见下文

#### Yjs的格式撤销机制

- 捕获：应用格式时被覆盖/切分删除的旧item直接记入事务delete set，undo栈保存这批具体item（keepItem防GC）。undo时无需扫描推导"区间内现在应生效哪个操作"，天际线信息等价于被删item集合本身
- undo：删除本次事务新增的item，对捕获的旧item按原left/right邻居锚点克隆（分配新id）放回；邻居被并发修改或重做时沿redone链追溯。同一item多次undo/redo由followRedone复用同一克隆；捕获窗口内既创建又被删除的item不恢复
- 语义：对格式item没有"发现远端新覆盖就中止"的检查（该逻辑仅存在于map条目分支，对应ignoreRemoteAttributeChanges选项）。恢复的旧item与远端并发item在链上按YATA交错，生效值按链序合成——undo不保证回到"撤销前所见"的文档状态，只保证确定性收敛
- 性能：撤销本身成本低，但任何触及格式item的事务（含undo/redo）后，cleanupYTextAfterTransaction会对整个YText全量遍历清理冗余格式项（O(n)，源码标注experimental）

与本文天际线方案的对比：Yjs用"捕获时存储被删item"换取撤销时零推导，实现简单，但undo语义近似（不与远端新覆盖协调）且付出全量清理扫描；本文用old标签维护天际线，使撤销成本局部化（只扫区间内端点），并能精确推导"撤销后该区间应生效的操作"。

### Automerge

Automerge的文本CRDT基于RGA（Replicated Growable Array）：

- 每个字符是一个元素，拥有全局唯一opId（lamport时间戳 + actor id）；插入操作指定前驱元素，将新元素插入其后
- 多个插入指向同一前驱时，按opId排序；并发在相同位置追加时存在interleaving问题，通过（counter，actor）排序缓解
- 删除同样使用墓碑；1.x保留完整操作历史，2.0改为列式存储并支持历史压缩
- 富文本格式以span/mark表示：mark op关联start/end的opId，2.0中文本由text op + mark name/value构成
- 无内置撤销，需在应用层实现

### 方法比较

|  | peritext标签 | Yjs（YATA） | Automerge（RGA） |
| --- | --- | --- | --- |
| 插入冲突解决 | 无明确排序规则 | origin+creator+clock确定性排序 | 前驱+opId排序 |
| 并发右插入interleaving | - | originRight约束避免 | 存在，靠排序缓解 |
| 插入粒度 | 逐字符 | item可含多字符 | 逐字符元素 |
| 格式表示 | 每字符前后标签记录mark集合 | 开关item链（起点+取反终点） | span / mark op |
| 删除/历史 | 墓碑 | 墓碑，支持gc | 墓碑，支持历史压缩 |
| 撤销 | 未提供 | UndoManager捕获被删item，克隆放回 | 无内置 |

三者共同点：都将插入组织为树/链，通过确定性排序解决并发插入；格式区间均依赖不随文本变化而变化的坐标（标签/item id/opId）。共同缺点：插入细粒度、逐字记录CRDT信息存储消耗大（见下文），且均不提供支持撤销重做的领域级格式区间模型，这正是本文方法要解决的问题。

### 已有方法的缺点

标签方法的撤销后重建style需要n\*k，k是当前活动区间数，n是文本长度

除Yjs（item可含多字符）外，插入操作细粒度都是逐字符的，没有聚合能力。逐字记录CRDT信息，存储消耗大



## 方法

命名约定：

segment：一次插入操作

piece：一次插入操作的文本被其他插入操作划分后的一段

offset：字节偏移（segment/piece/global）

size：字节长度

pos：字符偏移（segment/piece/global）

length：字符长度

coordinate：操作不变的文本坐标

### segments and pieces

将一次插入操作聚合成一段，对于ai参与的编辑（匹配-替换）来说，天然可以减少存储消耗。对于人编辑的场景，则可以将连续插入的情况合并成一次操作，以减少存储消耗。

#### segment聚合后的文本坐标：

```plaintext
{
    "segment_id": "xxx",
    "offset": k
}
```

文本坐标需要能够描述在某个字符的前或后，这点非常重要。因为如果文本坐标是统一的某个字符的前或者后，会有interleaving problem（前面已经提到）。

为了能够表达在某个字符的前或后，规定如下：对于一个长度为n的segment，k在[1,n]表示在第k个字符的后面，k在[-n,-1]表示在倒数第-k个字符的前面。即-n是在segment的最前面，n是在segment的最后面。

#### 树结构：

将所有的pieces按在文本中的绝对位置组织成一棵二叉树。由于B+树的所有数据在叶子节点，可以避免RBTree或B-Tree的数据在所有节点，在统计或更新一些数据的时候还要额外考虑数据节点的父子关系。因此选择B+树作为文本容器，思路更清晰。

文本容器B+树中每个叶子节点是一个piece，非叶节点记录其子树中所有子节点的文本长度/字节长度/行数等信息。支持给定绝对pos/offset，查找对应piece。

要给定text coordinate找对应piece则需要在每个segment中记录pieces，并进行二分查找。

给定piece查找全局offset，需要支持piece到B+树叶子节点的对应，然后从叶节点向上求和前缀offset。这里最好的是直接存树到迭代器，并保证迭代器不失效。

#### 仅考虑插入操作的CRDT正确性：

将segment，即插入操作按依赖关系形成树。非常明显依赖关系、插入的相对位置是跟随同步不发生变化的。唯一问题是当两个不同协作者各自在同一位置插入内容后再进行同步，此时需要一个规则保证不管谁的操作先到达，合并后状态一致。

1. 按时钟编号排序

2. 按协作者uuid排序

将插入位置相同的所有segment排序。由于插入顺序可以在一个piece的左侧或右侧，因此在插入segment的时候，需要计算其左侧冲突的segment的右子树的总和和右侧segment的左子树总和。

实现：要不就是处理冲突的时候递归计算子树范围，要不每个segment记录左右子树范围。暂定处理冲突时递归计算总子树

### 区间操作

两个文本坐标中间包含的所有文本添加属性。先不考虑插入/删除进行建模。

#### 开闭规则

一般富文本编辑器如word都是前闭后开，即在区域前插入文本不继承区域内属性，在区域后插入文本会继承。但插入操作又是一个前后均闭的区域操作，因此规定左端必须为闭，右端可以开闭。开闭是通过使用字符前或字符后文本坐标来实现的。

#### 维护天际线

类似于peritext的仅记录左右端点的区间操作维护方式有一个主要问题是撤销操作后，难以仅扫描该区间内部的端点来获得该操作区间内早于该操作的最晚生效操作。举例：t=1时设置[1,5]的值为1，t=2时设置[3,4]的值为2，撤销t=2的操作时，必须扫描所有端点才能确定撤销后[3,4]范围内生效的操作是t=1的操作。

想象区间操作是在一片区域内“刷漆”，新的一次刷漆会覆盖在旧的层之上，形成类似于阶梯的形状。避免在撤销时从文本的一端开始遍历的关键就是恢复这个状态。我们称在某个区间内早于某操作的最晚生效操作为“**天际线（skyline）**”，天际线不仅仅包含单一的操作集合，还有操作间的覆盖关系。

不考虑分布式协作，假设操作都依序到来，可以发现维护天际线的关键是记录区间操作在应用前两个端点处生效的区间操作。

**引入old标签维护天际线：**每个区间操作的左右端点添加一个old标签，记录区间覆盖该端点的且早于该区间操作的最晚生效的操作。

**边界条件：**对于当前绝对位置相同的端点，文本坐标在字符后的端点（紧贴左边字符）排在字符前的端点之前。这两类中，均是右边端点（结束端点）排在左边端点（开始端点）前。对于右边端点，操作时间越早就排在越前面，左边端点则相反。这是为了防止端点位置相同的两个操作会有一方的old tag引用到另一方，使得撤销重做后引发边界问题。

**区分类型：**不同类型的区间属性应当分别维护。

#### 区间操作的应用或重做：切割天际线

考虑到分布式协作时操作的到来不按顺序，一个区间操作在应用时和重做时实际上是相同的逻辑，都需要考虑是否会有比其更晚的操作已经应用。

首先从左到右扫描待应用区间内所有未被撤销的端点，检查是否存在切割天际线的情况。切割天际线就是对于某个端点，待应用的操作比该端点的old更新，但比端点所属操作更老。从天际线的定义上来说，切割是不可能出现的，因此一旦发现至少切割了一个端点，就需要重建天际线。

**若发现切割**，重建天际线的方法如下：

1. 记录被切割的端点为A

2. 选择A中最左侧的端点L，记录天际线操作为L的old标签所指的区间操作

3. 继续向左侧扫描端点，直到到达待应用操作的左端点，对该范围内的所有端点操作如下：

   1. 过滤所有所属操作早于天际线操作或old标签晚于待应用操作的端点。

   2. 如果是右端点且old标签等于天际线操作，则设置天际线操作为当前端点所属的区间操作

   3. 如果是左端点且是天际线操作，则设置天际线操作为当前端点的old标签所指向的区间操作

   4. 其他情况都是不可能出现的

4. 设置待应用区间操作左端点的old标签指向天际线操作

5. 选择A中最右侧的端点R，向右扫描直到待应用操作的右端点，重建天际线的方法和向左扫描时是对称的

6. 设置待应用区间操作右端点的old标签指向天际线操作

7. 修改A中所有端点的old标签为待应用的区间操作

8. 修改区间中所有pieces的属性

**若未发现切割**，查看区间内当前生效操作：若生效操作早于待应用操作，说明待应用操作是最新操作，直接应用即可；否则说明存在一个比其晚且完全覆盖它的操作，将本次重做推迟到该覆盖操作撤销后再执行，此时将待应用操作标记为未应用。

撤销操作在更新完天际线后，同样要检查区间内是否有被其覆盖的未应用操作，若有则再做redo。

例子：
当前有4个操作：
t=1,[4,6]
t=2,[7,9]
t=3,[5,8]
t=5,[2,3]
此时新来一个操作：t=4,[1,10]

当前状态：
t1：{l_current=1,l_old=0,r_current=1,r_old=0}
t2：{l_current=2,l_old=0,r_current=2,r_old=0}
t3：{l_current=3,l_old=1,r_current=3,r_old=2}
t5：{l_current=5,l_old=0,r_current=5,r_old=0}
这里用0来表示初始状态。

插入t4, 切断了t5的左右端点（待应用的时间为4，t5的左右端点都是当前时间戳晚于4，而old时间戳早于4）
从t5左端点向左，天际线为0，到达待应用操作的左端点，确认其old为0
从t5右端点向右：
天际线操作为0（初始状态）
到4，发现t1左端点old等于当前天际线，向上走，天际线操作变为1
到5，发现t3左端点old等于当前天际线，向上走，天际线操作变为3
6/7均跳过，因为其current和old都早于天际线，被覆盖了，不需要考虑
到8，发现t3右端点current等于当前天际线，向下走，天际线操作变为2
到9，发现t2右端点current等于当前天际线，向下走，天际线操作变为0
到10，到达待应用操作的右端点，确认其old为0
随后将t5的两个端点的old标签设为4即可。

伪代码（对应src/textcrdt.hpp的redoDel/redoRangeOp，应用与重做共用同一逻辑）：

```plaintext
redoDel(op):
    # 1. 尝试从边界piece的tombstone直接取得端点处的天际线，避免不必要的扫描
    left_piece = findPiece(op.left.anchor)
    p = left_piece.tombStone
    if p == null:
        op.left.old = null
    elif p.left.anchor != op.left.anchor and p < op:
        op.left.old = p
    elif p.left.anchor == op.left.anchor and (p.left.old == null or p.left.old < op):
        op.left.old = p.left.old
    # 右端点对称处理（right_piece/p.right/op.right），无法确定时old保持bad，留待重建

    redoRangeOp(op)

redoRangeOp(op):
    # 2. 修改区间内所有piece的属性（右端为闭时末尾piece也包含）
    for piece in pieces(op.left.anchor, op.right.anchor):
        if piece.tombStone == null or piece.tombStone < op:
            piece.tombStone = op

    # 3. 扫描区间内端点，找被切割的端点A：op比端点old新、比端点所属操作老
    A = []
    for tag in tags(op.left, op.right):
        if tag.status == Active and (tag.old == null or tag.old < op) and op < tag.cur:
            A.append(tag)

    if A为空:
        if op.left.old有效 and op.right.old有效:
            op.left.status = op.right.status = Active   # 最新操作，直接应用
        else:
            op.left.status = op.right.status = UnUsed   # 被更晚且完全覆盖的操作覆盖，推迟重做
        return

    # 4. 发现切割，重建天际线
    op.left.status = op.right.status = Active
    first, last = A中最左端点, A中最右端点
    # 向左扫描（仅当op.left.old未知时）：从first左侧到op.left
    newest = first.old
    for tag in tags(first左侧, op.left):
        if tag.status != Active: continue
        if tag.is_left and tag.cur == newest:
            newest = tag.old                      # 向上走
        elif tag.is_right and (newest == null or newest < tag.cur) and tag.cur < op:
            assert(tag.old == newest)
            newest = tag.cur                      # 向下走
    op.left.old = newest
    # 向右扫描（对称）：从last右侧到op.right
    newest = last.old
    for tag in tags(last右侧, op.right):
        if tag.status != Active: continue
        if tag.is_right and tag.cur == newest:
            newest = tag.old
        elif tag.is_left and tag.cur < op and (newest == null or newest < tag.cur):
            assert(tag.old == newest)
            newest = tag.cur
    op.right.old = newest

    # 5. 被切割端点的old指向待应用操作
    first.old = last.old = op
```

注意：实现中第5步仅修改A中最左、最右两个端点的old（first_across/last_across），与上文文字描述第7步"修改A中所有端点的old标签"略有出入，以代码为准。

#### 区间操作的撤销

操作撤销后会标记被撤销，并删除出天际线。因此需要做的操作是修改操作区间内所有old tag指向他的端点的old tag。方法如下：

1. 记录当前天际线操作为待撤销操作的左端点的old标签指向的区间操作

2. 从待撤销操作的左端点开始向右遍历端点，直到待撤销操作的右端点

   1. 过滤所有所属操作早于天际线操作或old标签晚于待撤销操作的端点

   2. 如果old标签等于待撤销操作，则设置old标签指向天际线操作

   3. 如果是左端点且old标签等于天际线操作，则设置天际线操作为当前端点所属的区间操作

   4. 如果是右端点且是天际线操作，则设置天际线操作为当前端点的old标签所指向的区间操作

   5. 其他情况都是不可能出现的

伪代码（对应src/textcrdt.hpp的undoDel/undoRangeOp）：

```plaintext
undoDel(op):
    covered = undoRangeOp(op)          # 返回被覆盖的未应用操作，按新到旧排序
    for c in covered:                  # 撤销后重做被覆盖的未应用操作
        redoRangeOp(c)                 # 复用上面的redoRangeOp

undoRangeOp(op):
    L, R = op.left, op.right
    if L.status == UnUsed or R.status == UnUsed:   # 从未生效的操作（被完全覆盖），无后续影响
        L.status = R.status = Undone
        return []

    L.status = R.status = Undone
    newest = L.old                     # 当前天际线操作
    covered = []
    unused = {}
    piece_cursor = findPiece(L.anchor)
    for tag in tags(L右侧, R]:         # 从左到右遍历区间内端点
        # 1. 将piece_cursor到tag锚点之间的piece恢复为天际线操作
        for piece in pieces(piece_cursor, tag.anchor):
            if piece.tombStone == op:
                piece.tombStone = newest
        if tag == R: break

        # 2. 更新端点
        if tag.status == Undone: continue
        if tag.status == UnUsed and op < tag.cur: continue
        if tag.status == Active and tag.old != null and op < tag.old: continue   # 过滤
        if tag.old == op:
            tag.old = newest
        elif tag.is_left:
            if tag.status == UnUsed:
                unused.add(tag.cur)
                tag.old = newest if (newest == null or newest < tag.cur) else bad
            elif newest == null or newest < tag.cur:
                assert(tag.old == newest)
                newest = tag.cur       # 向上走
        else:
            if tag.status == UnUsed and tag.cur in unused:
                covered.append(tag.cur)
                tag.old = newest if (newest == null or newest < tag.cur) else bad
            elif tag.cur == newest:
                newest = tag.old       # 向下走

    sort covered 从新到旧              # 先重做更晚的操作，避免影响较早操作的old
    return covered
```

#### 考虑插入

前面的模型没有考虑插入操作，现在引入插入操作。由于文本坐标不受插入操作影响，区间操作也不会随插入操作改变。唯一需要做的是插入时需要正确计算当前生效的区间操作并设置文本属性，就可以保证插入和区间操作的CRDT一致性。

### 删除即区域操作

将删除视为设置隐藏的一种区间操作。需要注意的是不像加粗/斜体这种bool状态，设置隐藏没有对应的设置显示这种反状态，只能通过撤销区间操作来达到。因为设置显示会导致区间内被删除的文本全部显示，是不符合操作者所见的。

两种触发删除的情况:

1. 撤销插入操作

2. 删除区间

   因此每一个piece需要记录两个状态：一个tombstone记录最新的删除操作，一个undo\_del记录撤销对应的删除操作。在撤销插入操作时，如果其undo\_del为空，则创建一个删除操作，若不为空，则该删除操作应该是撤销状态，将其重做。在重做插入操作时，则其undo\_del操作应当不为空且状态不为撤销状态，将其撤销即可。

   正常删除区间只要按区间操作来做即可。

伪代码（对应src/textcrdt.hpp的undo/redo分发与undoInsertion/redoInsertion）：

```plaintext
undo(op, targetID):                      # redo对称
    target = find(targetID)
    if target是StoredUndo:
        return redo(op', target.target)  # 撤销撤销 = 重做其目标操作
    if target是StoredRedo:
        target = target.target           # 撤销重做 = 撤销其目标操作
    u = store StoredUndo(op)，u.target = target
    undoOp(u)

undoOp(u):                               # redoOp对称（对应redoInsertion/redoDel）
    target = u.target
    if target.undoredo != null and u < target.undoredo:
        return                           # LWW：已有更新的undo/redo作用于该操作
    switch target.type:
        Insert: undoInsertion(segment)
        Delete: undoDel(deletion)
    target.undoredo = u                  # 记录最新作用于该操作的undo/redo

undoInsertion(seg):                      # 撤销插入 = 用删除隐藏整个segment
    if seg.undo_op == null:              # 首次撤销时创建覆盖整个segment的删除操作
        seg.undo_op = new StoredDeletion(
            begin = anchor(seg, -seg.len),   # reversed，segment起始处
            end   = anchor(seg,  seg.len))   # normal，segment末尾处（前后均闭）
    redoDel(seg.undo_op)

redoInsertion(seg):                      # 重做插入 = 撤销隐藏它的删除
    if seg.undo_op != null:
        undoDel(seg.undo_op)
```

说明：

- seg.undo_op使用与原segment相同的(replica, stamp)作为操作身份，且不存入replica的操作表中——保证各副本撤销同一插入时生成相同身份的删除操作，LWW判定一致
- PlainText层（src/text.cpp）用undo_stack/redo_stack存本地操作stamp，beginGroup/endGroup以哨兵值分组；undo()按栈顶元素（组或单个操作）逐个生成新的UndoOperation执行，撤销产物不重新入栈；undoSpecific/redoSpecific可指向任意协作者的操作，且其产物会记录入栈