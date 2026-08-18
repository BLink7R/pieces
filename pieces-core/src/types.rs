use crate::id::OperationId;

/// 子树汇总键：字符数。total 含墓碑，visible 不含。
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct PieceInfo {
    pub total: u64,
    pub visible: u64,
}

impl PieceInfo {
    pub fn new(total: u64, visible: u64) -> Self {
        Self { total, visible }
    }
}

impl std::ops::Add for PieceInfo {
    type Output = PieceInfo;
    fn add(self, rhs: PieceInfo) -> PieceInfo {
        PieceInfo {
            total: self.total + rhs.total,
            visible: self.visible + rhs.visible,
        }
    }
}

impl std::ops::AddAssign for PieceInfo {
    fn add_assign(&mut self, rhs: PieceInfo) {
        self.total += rhs.total;
        self.visible += rhs.visible;
    }
}

impl std::ops::Sub for PieceInfo {
    type Output = PieceInfo;
    fn sub(self, rhs: PieceInfo) -> PieceInfo {
        PieceInfo {
            total: self.total - rhs.total,
            visible: self.visible - rhs.visible,
        }
    }
}

impl std::iter::Sum for PieceInfo {
    fn sum<I: Iterator<Item = PieceInfo>>(iter: I) -> Self {
        iter.fold(PieceInfo::default(), |a, b| a + b)
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TagStatus {
    Active,
    Undone,
    UnUsed,
}

/// 对应 C++ 的 StatedPtr：null=初始状态，Op=指向某操作，Bad=待重建天际线时填充。
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum StatePtr {
    Null,
    Op(OperationId),
    Bad,
}

impl StatePtr {
    pub fn is_good(&self) -> bool {
        matches!(self, StatePtr::Op(_))
    }
    pub fn is_bad(&self) -> bool {
        matches!(self, StatePtr::Bad)
    }
    pub fn is_null(&self) -> bool {
        matches!(self, StatePtr::Null)
    }
    pub fn op(&self) -> Option<OperationId> {
        match self {
            StatePtr::Op(id) => Some(*id),
            _ => None,
        }
    }
}
