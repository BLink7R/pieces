use std::cmp::Ordering;

use uuid::Uuid;

pub type ReplicaId = Uuid;
pub type Stamp = u32;

/// 操作唯一标识。注意：Ord 采用 (stamp, replica) 顺序，与 C++ `StoredOperation::operator<`
/// 一致（先时钟号、后副本 id），用于 CRDT 确定性排序；相等性仍为逐字段相等。
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub struct OperationId {
    pub replica: ReplicaId,
    pub stamp: Stamp,
}

impl OperationId {
    pub fn new(replica: ReplicaId, stamp: Stamp) -> Self {
        Self { replica, stamp }
    }
}

impl PartialOrd for OperationId {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for OperationId {
    fn cmp(&self, other: &Self) -> Ordering {
        self.stamp.cmp(&other.stamp).then(self.replica.cmp(&other.replica))
    }
}

pub fn generate_replica_id() -> ReplicaId {
    Uuid::new_v4()
}
