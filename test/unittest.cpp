#include <gtest/gtest.h>

#include "simpletext.hpp"
#include "text.hpp"

TEST(PlainTextTest, InsertAndToString)
{
	PlainText<> text;
	text.insert(0, "hello");
	text.insert(5, " world");
	EXPECT_EQ(text.toString(), "hello world");
}

TEST(PlainTextTest, DeleteRange)
{
	PlainText<> text;
	text.insert(0, "abcdef");
	// delete "cd"
	text.del(2, 4);
	EXPECT_EQ(text.toString(), "abef");
}

TEST(PlainTextTest, UndoRedoInsertion)
{
	PlainText<> text;
	text.insert(0, "12345");
	PlainText<> text2(text.replicaID());
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
	PlainText<> text;
	text.insert(0, "aaa");
	text.insert(0, "bbb");
	Anchor anchor = text.toAnchor(3);
	text.undo();
	text.undo();

	text.apply(Insertion<char>{generateReplicaID(), 10, anchor, "ccc"});
	EXPECT_EQ(text.toString(), "ccc");

	text.redo();
	EXPECT_EQ(text.toString(), "cccaaa");
}

TEST(PlainTextTest, InsertUndoOnEOF)
{
	PlainText<> doc;
	doc.insert(0, "12345678901234567890");
	doc.del(0, 20);
	doc.insert(0, "aaa");

	PlainText<> doc2(doc.origin());
	doc2.apply(doc.diff(doc2.frontline()));
	doc2.insert(3, "bbb");

	auto anchor = doc.toAnchor(3);
	doc.apply(doc2.diff(doc.frontline()));
	EXPECT_EQ(doc.toOffset(anchor), 3);
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

TEST(RichTextTest, Undo)
{
	RichText<TestFormatProvider> doc;
	doc.formatProvider().addStyle("intStyle", -1);
	std::string text = "12345678901234567890";
	doc.insert(0, text);

	doc.apply(Formatting<OpenedRange, int>(doc.replicaID(), 8, "intStyle", doc.toOpenedRange(6, 8), 2));
	doc.apply(Formatting<OpenedRange, int>(doc.replicaID(), 9, "intStyle", doc.toOpenedRange(12, 14), 3));
	doc.apply(Formatting<OpenedRange, int>(doc.replicaID(), 10, "intStyle", doc.toOpenedRange(5, 15), 4));

	// covered op
	doc.apply(Formatting<OpenedRange, int>(generateReplicaID(), 7, "intStyle", doc.toOpenedRange(7, 13), 1));

	doc.undoSpecific(OperationID(doc.replicaID(), 10));

	EXPECT_EQ(doc.style<int>("intStyle", 5), -1);
	EXPECT_EQ(doc.style<int>("intStyle", 6), 2);
	EXPECT_EQ(doc.style<int>("intStyle", 7), 2);
	EXPECT_EQ(doc.style<int>("intStyle", 8), 1);
	EXPECT_EQ(doc.style<int>("intStyle", 9), 1);
	EXPECT_EQ(doc.style<int>("intStyle", 10), 1);
	EXPECT_EQ(doc.style<int>("intStyle", 11), 1);
	EXPECT_EQ(doc.style<int>("intStyle", 12), 3);
	EXPECT_EQ(doc.style<int>("intStyle", 13), 3);
	EXPECT_EQ(doc.style<int>("intStyle", 14), -1);
}

TEST(RichTextTest, CoveredOp)
{
	RichText<TestFormatProvider> doc;
	doc.formatProvider().addStyle("intStyle", -1);
	std::string text = "12345678901234567890";
	doc.insert(0, text);

	doc.apply(Formatting<OpenedRange, int>(doc.replicaID(), 10, "intStyle", doc.toOpenedRange(5, 15), 3));

	// covered op
	doc.apply(Formatting<OpenedRange, int>(generateReplicaID(), 5, "intStyle", doc.toOpenedRange(7, 13), 1));

	doc.undoSpecific(OperationID(doc.replicaID(), 10));

	EXPECT_EQ(doc.style<int>("intStyle", 5), -1);
	EXPECT_EQ(doc.style<int>("intStyle", 6), -1);
	EXPECT_EQ(doc.style<int>("intStyle", 7), 1);
	EXPECT_EQ(doc.style<int>("intStyle", 12), 1);
	EXPECT_EQ(doc.style<int>("intStyle", 13), -1);
	EXPECT_EQ(doc.style<int>("intStyle", 14), -1);
}

TEST(RichTextTest, InlineObject)
{
	const std::string obj_marker = "\xFF";
	RichText<TestFormatProvider> text;
	text.insert(0, "hello");
	text.insertObject(5, "obj1");
	text.insert(6, " world");
	EXPECT_EQ(text.toString(), "hello" + obj_marker + " world");

	// undo removes the object insertion, redo restores it
	OperationID object_op(text.replicaID(), 3);
	text.undoSpecific(object_op);
	EXPECT_EQ(text.toString(), "hello world");
	text.redoSpecific(object_op);
	EXPECT_EQ(text.toString(), "hello" + obj_marker + " world");

	// delete the object
	text.del(5, 6);
	EXPECT_EQ(text.toString(), "hello world");
}

TEST(RichTextTest, InlineObjectSync)
{
	const std::string obj_marker = "\xFF";
	RichText<TestFormatProvider> a;
	a.insert(0, "hello");
	a.insertObject(5, "img1");

	RichText<TestFormatProvider> b(a.origin());
	b.apply(a.diff(b.frontline()));
	EXPECT_EQ(b.toString(), "hello" + obj_marker);

	// concurrent edit after the object converges
	b.insert(6, "X");
	a.apply(b.diff(a.frontline()));
	EXPECT_EQ(a.toString(), "hello" + obj_marker + "X");
	EXPECT_EQ(b.toString(), "hello" + obj_marker + "X");

	// undo of the object insertion converges across replicas
	OperationID object_op(a.replicaID(), 3);
	b.undoSpecific(object_op);
	EXPECT_EQ(b.toString(), "helloX");
	a.apply(b.diff(a.frontline()));
	EXPECT_EQ(a.toString(), "helloX");
}

TEST(PlainTextTest, Utf16)
{
	PlainText<char16_t> text;
	text.insert(0, u"hello");
	text.insert(5, u" world");
	EXPECT_EQ(text.toString(), u"hello world");
	EXPECT_EQ(text.size(), 11);

	// emoji is one code point but two char16 code units
	text.insert(0, u"\U0001F600");
	EXPECT_EQ(text.size(), 13);
	EXPECT_EQ(text.toString(), std::u16string(u"\U0001F600") + u"hello world");

	// delete the surrogate pair
	text.del(0, 2);
	EXPECT_EQ(text.toString(), u"hello world");
}

TEST(PlainTextTest, Utf16Sync)
{
	PlainText<char16_t> a;
	a.insert(0, u"hello");

	PlainText<char16_t> b(a.origin());
	b.apply(a.diff(b.frontline()));
	EXPECT_EQ(b.toString(), u"hello");

	b.insert(5, u"\U0001F600");
	a.apply(b.diff(a.frontline()));
	EXPECT_EQ(a.toString(), std::u16string(u"hello") + u"\U0001F600");
	EXPECT_EQ(b.toString(), std::u16string(u"hello") + u"\U0001F600");
}

TEST(RichTextTest, ParagraphHead)
{
	const std::string nl = "\n";
	RichText<TestFormatProvider> doc;
	doc.insert(0, "first paragraph");
	// insert a paragraph head at the start of the document
	doc.insertParagraphHead(0);
	EXPECT_EQ(doc.toString(), nl + "first paragraph");

	// plain text only supports plain text insertion
	PlainText<> plain;
	plain.insert(0, "hello");
	EXPECT_EQ(plain.toString(), "hello");
}

TEST(RichTextTest, ParagraphHeadSync)
{
	const std::string nl = "\n";
	RichText<TestFormatProvider> a;
	a.insert(0, "para text");
	a.insertParagraphHead(0);

	RichText<TestFormatProvider> b(a.origin());
	b.apply(a.diff(b.frontline()));
	EXPECT_EQ(b.toString(), nl + "para text");

	// undo the paragraph head insertion converges across replicas
	OperationID head_op(a.replicaID(), 3);
	b.undoSpecific(head_op);
	EXPECT_EQ(b.toString(), "para text");
	a.apply(b.diff(a.frontline()));
	EXPECT_EQ(a.toString(), "para text");
}