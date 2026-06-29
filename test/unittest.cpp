#include <gtest/gtest.h>

#include "simpletext.hpp"
#include "text.hpp"

TEST(PlainTextTest, InsertAndToString)
{
	PlainText text;
	text.insert(0, "hello");
	text.insert(5, " world");
	EXPECT_EQ(text.toString(), "hello world");
}

TEST(PlainTextTest, DeleteRange)
{
	PlainText text;
	text.insert(0, "abcdef");
	// delete "cd"
	text.del(2, 4);
	EXPECT_EQ(text.toString(), "abef");
}

TEST(PlainTextTest, UndoRedoInsertion)
{
	PlainText text;
	text.insert(0, "12345");
	PlainText text2(text.replicaID());
	text2.apply(text.diff());

	text2.del(0, 5);
	text.apply(text2.diff(text.frontline()));
	EXPECT_EQ(text.toString(), "");

	text.insert(0, "aaa");
	text.undo();
	text2.apply(text.diff(text2.frontline()));
	EXPECT_EQ(text2.toString(), "");

	text2.insert(0, "bbb");
	text2.undo();
	EXPECT_EQ(text2.toString(), "");
	text2.redo();
	EXPECT_EQ(text2.toString(), "bbb");
}

TEST(PlainTextTest, InsertOnEOF)
{
	PlainText text;
	text.insert(0, "aaa");
	text.insert(0, "bbb");
	Anchor anchor = text.toAnchor(3);
	text.undo();
	text.undo();

	text.apply(Insertion{generateReplicaID(), 10, anchor, "ccc"});
	EXPECT_EQ(text.toString(), "ccc");

	text.redo();
	EXPECT_EQ(text.toString(), "cccaaa");
}

TEST(PlainTextTest, InsertUndoOnEOF)
{
	PlainText doc;
	doc.insert(0, "12345678901234567890");
	doc.del(0, 20);
	doc.insert(0, "aaa");

	PlainText doc2(doc.origin());
	doc2.apply(doc.diff(doc2.frontline()));
	doc2.insert(3, "bbb");

	auto anchor = doc.toAnchor(3);
	doc.apply(doc2.diff(doc.frontline()));
	EXPECT_EQ(doc.toPos(anchor), 3);
	EXPECT_EQ(doc.toString(), "aaabbb");

	// only undo the last operation made by self
	doc.undo();
	EXPECT_EQ(doc.toString(), "bbb");
}

TEST(PlainTextTest, OldTag)
{
	PieceCRDTValidator doc;
	uint32_t op_stamp = 2;

	std::string initial("Hello, this is a test string for old tag testing.");
	Anchor init_anchor = doc.insertAnchor(0);
	Insertion ins(doc.id(), op_stamp++, init_anchor, initial);
	doc.insert(ins);

	auto id1 = op_stamp;
	{
		ClosedRange range = doc.historyRange(0, 20);
		Deletion del1(doc.id(), op_stamp++, range.begin, range.end);
		doc.del(del1);
	}

	auto id2 = op_stamp;
	{
		ClosedRange range = doc.historyRange(5, 15);
		Deletion del1(doc.id(), op_stamp++, range.begin, range.end);
		doc.del(del1);
	}

	UndoOperation uop(doc.id(), op_stamp++, OperationID{doc.id(), id2});
	doc.undo(uop);
	EXPECT_EQ(doc.toString(), "t string for old tag testing.");
	EXPECT_TRUE(doc.validate());
	UndoOperation uop2(doc.id(), op_stamp++, OperationID{doc.id(), id1});
	doc.undo(uop2);
	EXPECT_EQ(doc.toString(), "Hello, this is a test string for old tag testing.");
	EXPECT_TRUE(doc.validate());
	RedoOperation rop(doc.id(), op_stamp++, OperationID{doc.id(), id2});
	doc.redo(rop);
	EXPECT_EQ(doc.toString(), "Helloa test string for old tag testing.");
	EXPECT_TRUE(doc.validate());
}