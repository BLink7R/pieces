use pieces_core::{Anchor, Operation, PieceCRDT, ReplicaId};

fn insert_at(doc: &mut PieceCRDT, pos: usize, text: &str) {
    let anchor = doc.insert_anchor(pos);
    let op = Operation::insert(doc.id(), doc.stamp(), anchor, text.to_string());
    assert!(doc.apply(&op), "insert at {pos} failed");
}

#[test]
fn sequential_insert() {
    let mut doc = PieceCRDT::new(ReplicaId::nil());
    insert_at(&mut doc, 0, "hello");
    assert_eq!(doc.to_string(), "hello");
    assert_eq!(doc.size(), 5);

    insert_at(&mut doc, 5, " world");
    assert_eq!(doc.to_string(), "hello world");

    insert_at(&mut doc, 11, "!");
    assert_eq!(doc.to_string(), "hello world!");
    assert_eq!(doc.size(), 12);
}

#[test]
fn middle_insert_splits_piece() {
    let mut doc = PieceCRDT::new(ReplicaId::nil());
    insert_at(&mut doc, 0, "abcdef");
    insert_at(&mut doc, 3, "XY");
    assert_eq!(doc.to_string(), "abcXYdef");
}

#[test]
fn anchor_pos_roundtrip() {
    let mut doc = PieceCRDT::new(ReplicaId::nil());
    insert_at(&mut doc, 0, "hello");
    insert_at(&mut doc, 2, "-");
    let text = doc.to_string();

    // 位置 0 用 reversed anchor；其余用 normal anchor
    assert!(doc.anchor(0).is_null());
    for pos in 1..=text.len() {
        let anchor: Anchor = doc.anchor(pos);
        let back = doc.pos(anchor).unwrap();
        assert_eq!(back, pos, "normal roundtrip failed at pos {pos}");
    }
    for pos in 0..text.len() {
        let anchor: Anchor = doc.reversed_anchor(pos);
        let back = doc.pos(anchor).unwrap();
        assert_eq!(back, pos, "reversed roundtrip failed at pos {pos}");
    }
}

#[test]
fn utf8_multibyte() {
    let mut doc = PieceCRDT::new(ReplicaId::nil());
    insert_at(&mut doc, 0, "你好世界");
    assert_eq!(doc.to_string(), "你好世界");
    assert_eq!(doc.size(), 4);

    insert_at(&mut doc, 2, "X");
    assert_eq!(doc.to_string(), "你好X世界");
    assert_eq!(doc.size(), 5);
}
