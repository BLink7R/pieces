#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>


#include "crdt.hpp"
#include "piecetree.hpp"
#include "simpletext.hpp"
#include "text.hpp"

std::string generateTestString(int index)
{
	return "test_" + std::to_string(index);
}

std::string generateRandomString(std::mt19937 &gen, int minLen, int maxLen)
{
	static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	std::uniform_int_distribution<> len_dist(minLen, maxLen);
	std::uniform_int_distribution<> char_dist(0, sizeof(charset) - 2);

	int length = len_dist(gen);
	std::string result;
	result.reserve(length);
	for (int i = 0; i < length; ++i)
	{
		result += charset[char_dist(gen)];
	}
	return result;
}

void runInsertTest(int numInsertions, int minLen = 1, int maxLen = 20)
{
	// 初始化随机数生成器
	std::random_device rd;
	std::mt19937 gen(rd());

	// 创建两个数据结构实例
	PlainText doc;
	SimpleDeferredText validator;
	std::set<std::string> test_set;
	size_t tot_len = 0;
	uint32_t operation_stamp = 3;

	// 记录时间
	auto start = std::chrono::high_resolution_clock::now();

	// 执行随机插入
	for (int i = 0; i < numInsertions; ++i)
	{
		// 生成随机字符串
		std::string str = generateRandomString(gen, minLen, maxLen);

		// 选择随机插入位置
		std::uniform_int_distribution<size_t> pos_dist(0, tot_len);
		size_t insert_pos = pos_dist(gen);

		// 在两个数据结构中插入
		validator.insert(insert_pos, str);

		// test_set.insert(str);
		doc.insert(insert_pos, str);
		tot_len += str.size();

		if ((i + 1) % 50 == 0 && tot_len > 0)
		{
			std::string tree_content = doc.toString();
			// 验证内容
			bool content_match = (tree_content == validator.toString());
			std::cout << "Content " << (content_match ? "matches" : "differs") << std::endl;

			if (!content_match)
			{
				std::cout << "Test failed at iteration " << i << std::endl;
				std::cout << "Expect: " << validator.toString() << std::endl;
				std::cout << "Actual: " << tree_content << std::endl;
			}
		}
	}

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

	std::cout << "Time taken: " << duration.count() << "ms\n";
	std::cout << "Number of pieces in PieceTree: " << doc.size() << "\n";
	std::cout << "Average time per insertion: " << duration.count() / (double)numInsertions << "ms\n";
}

void runInsertDeleteTest(int numOps, int minLen = 1, int maxLen = 20)
{
	std::cout << "Running insert+delete mixed test...\n";
	std::random_device rd;
	std::mt19937 gen(rd());

	PlainText doc;
	SimpleText validator;
	size_t tot_len = 0;
	uint32_t operation_stamp = 1;

	for (int i = 0; i < numOps; ++i)
	{
		// 插入
		std::string str = generateRandomString(gen, minLen, maxLen);
		std::uniform_int_distribution<size_t> pos_dist(0, tot_len);
		size_t insert_pos = pos_dist(gen);

		validator.insert(insert_pos, str);
		doc.insert(insert_pos, str);
		std::cout << "I " << str << " " << insert_pos << "\n";
		tot_len += str.size();

		// 每 10 次做一次删除
		if ((i + 1) % 2 == 0 && tot_len > 0)
		{
			std::uniform_int_distribution<> len_dist(10, 20);
			size_t len = len_dist(gen);
			if (len > tot_len)
				len = tot_len;
			if (len == 0)
				continue;

			std::uniform_int_distribution<size_t> del_pos_dist(0, tot_len - len);
			size_t del_pos = del_pos_dist(gen);

			std::cout << "D " << del_pos << " " << del_pos + len << "\n";
			doc.del(del_pos, del_pos + len);
			validator.erase(del_pos, len);
			tot_len -= len;
		}

		// 构建 PieceCRDT 内容并验证
		std::string tree_content = doc.toString();
		std::string expect = validator.toString();
		bool match = (tree_content == expect);
		std::cout << "Insert+Delete Test Content " << (match ? "matches" : "differs") << std::endl;
		if (!match)
		{
			std::cout << "Doc size: " << doc.size() << ", Validator size: " << expect.size() << "\n";
		}
	}
}

void readTest(const std::string &filename)
{
	std::cout << "Running read test from " << filename << "...\n";
	std::ifstream file(filename);
	if (!file.is_open())
	{
		std::cerr << "Failed to open file: " << filename << "\n";
		return;
	}

	PlainText doc;
	SimpleText validator;
	std::string line;

	while (std::getline(file, line))
	{
		std::stringstream ss(line);
		std::string type;
		ss >> type;

		if (type == "I")
		{
			std::string text;
			size_t pos;
			ss >> text >> pos;
			std::cout << "I " << text << " " << pos << "\n";
			doc.insert(pos, text);
			validator.insert(pos, text);
		}
		else if (type == "D")
		{
			size_t start, end;
			ss >> start >> end;
			size_t len = end - start;
			std::cout << "D " << start << " " << end << "\n";
			doc.del(start, end);
			validator.erase(start, len);
		}

		std::string tree_content = doc.toString();
		std::string expect = validator.toString();
		bool match = (tree_content == expect);

		std::cout << "Content " << (match ? "matches" : "differs") << " (len: " << tree_content.length() << ")\n";
		if (!match)
		{
			std::cout << "Test failed!" << std::endl;
			std::cout << "Doc size: " << tree_content.length() << ", Validator size: " << expect.length() << "\n";
			std::cout << "Actual: " << tree_content << "\n";
			std::cout << "Expect: " << expect << "\n";
			break;
		}
		std::cout << "Actual: " << tree_content << "\n";
		std::cout << "Expect: " << expect << "\n";
	}
}

void runDeleteUndoRedoTest(int numOps = 200, int start_len = 5000)
{
	std::cout << "Running delete-undo-redo test...\n";

	std::random_device rd;
	std::mt19937 gen(rd());

	PieceCRDT doc;
	SimpleDeferredText validator;
	validator.insert(0, "");
	uint32_t op_stamp = 2;

	// 1. 插入长度为 5000 的初始文本
	std::string initial = generateRandomString(gen, start_len, start_len);
	Anchor init_anchor = doc.insertAnchor(0);
	Insertion ins(doc.id(), op_stamp++, init_anchor, initial);
	doc.insert(ins);
	validator.insert(0, initial);

	// 记录所有删除操作的 OperationID，便于后面 undo/redo
	std::vector<OperationID> deletions;
	deletions.reserve(numOps);

	// 2. 随机进行 200 次删除（每次长度 10-20）
	for (int i = 0; i < numOps; ++i)
	{
		size_t current_size = validator.size();
		if (current_size == 0)
			break;

		std::uniform_int_distribution<> len_dist(10, 20);
		size_t len = len_dist(gen);
		if (len > current_size)
			len = current_size;
		if (len == 0)
			continue;

		std::uniform_int_distribution<size_t> pos_dist(0, current_size - len);
		size_t pos = pos_dist(gen);

		// 在 PieceCRDT 上执行删除
		Anchor begin = doc.reversedAnchor(pos);
		Anchor end = doc.anchor(pos + len);
		Deletion del(doc.id(), op_stamp, begin, end);
		doc.del(del);

		// 记录这次删除的 OperationID
		deletions.push_back(OperationID{doc.id(), op_stamp});

		// 在验证器上执行相同删除
		validator.erase(pos, len);

		++op_stamp;
	}

	auto check_equal = [&](const char *phase)
	{
		std::string doc_str = doc.toString();
		std::string val_str = validator.toString();
		bool match = (doc_str == val_str);
		std::cout << phase << " content " << (match ? "matches" : "differs") << "\n";
		std::cout << "Doc size: " << doc.size()
				  << ", Validator size: " << val_str.size() << "\n";
	};

	// 删除完成后先验证一次
	check_equal("After deletions");

	// 3. 将 200 次删除操作打乱后 undo
	std::shuffle(deletions.begin(), deletions.end(), gen);
	for (auto &opid : deletions)
	{
		UndoOperation uop(doc.id(), op_stamp++, opid);
		doc.undo(uop);
		validator.undo(opid.stamp);
	}
	check_equal("After undos");

	// 4. 将 200 次删除操作打乱后 redo
	std::shuffle(deletions.begin(), deletions.end(), gen);
	for (auto &opid : deletions)
	{
		RedoOperation rop(doc.id(), op_stamp++, opid);
		doc.redo(rop);
		validator.redo(opid.stamp);
	}
	check_equal("After redos");
}

void runHistoryDeleteUndoRedoTest(int numOps = 200, int start_len = 5000)
{
	std::cout << "Running delete-undo-redo test...\n";

	std::random_device rd;
	std::mt19937 gen(rd());

	PieceCRDTValidator doc;
	std::cout << "doc id: " << doc.id() << "\n";
	uint32_t op_stamp = 2;

	// 1. 插入长度为 5000 的初始文本
	std::string initial = generateRandomString(gen, start_len, start_len);
	Anchor init_anchor = doc.insertAnchor(0);
	Insertion ins(doc.id(), op_stamp++, init_anchor, initial);
	doc.insert(ins);

	std::vector<std::pair<ReplicaID, uint32_t>> deletion_ids;
	deletion_ids.reserve(numOps);
	for (int i = 0; i < numOps; ++i)
		deletion_ids.emplace_back(uuids::uuid_random_generator(gen)(), op_stamp++);
	std::shuffle(deletion_ids.begin(), deletion_ids.end(), gen);

	// 2. 随机进行 200 次删除（每次长度 10-20）
	for (int i = 0; i < numOps; ++i)
	{
		size_t current_size = start_len;

		std::uniform_int_distribution<> len_dist(10, 40);
		size_t len = len_dist(gen);
		if (len > current_size)
			len = current_size;
		if (len == 0)
			continue;

		std::uniform_int_distribution<size_t> pos_dist(0, current_size - len);
		size_t pos = pos_dist(gen);

		// 在 PieceCRDT 上执行删除
		std::cout << "Deleting at pos " << pos << " length " << len << " id " << deletion_ids[i].second << "\n";
		ClosedRange range = doc.historyRange(pos, pos + len);
		Deletion del(deletion_ids[i].first, deletion_ids[i].second, range.begin, range.end);
		doc.del(del);

		if (!doc.validate())
		{
			std::cout << "Validation failed after deletion " << i << "\n";
			return;
		}
	}

	// 3. 将 200 次删除操作打乱后 undo
	std::shuffle(deletion_ids.begin(), deletion_ids.end(), gen);
	for (auto &opid : deletion_ids)
	{
		std::cout << "Undoing operation stamp " << opid.second << "\n";
		UndoOperation uop(doc.id(), op_stamp++, OperationID{opid.first, opid.second});
		doc.undo(uop);
		doc.validate();
	}

	// 4. 将 200 次删除操作打乱后 redo
	std::shuffle(deletion_ids.begin(), deletion_ids.end(), gen);
	for (auto &opid : deletion_ids)
	{
		std::cout << "Redoing operation stamp " << opid.second << "\n";
		RedoOperation rop(doc.id(), op_stamp++, OperationID{opid.first, opid.second});
		doc.redo(rop);
		doc.validate();
	}
}

// void coverTest()
// {
// 	PieceCRDTValidator doc;

// 	std::string initial("012345678901234567890123456789");
// 	Anchor init_anchor = doc.anchor(0);
// 	Insertion ins(doc.id(), 1, init_anchor, initial);
// 	doc.insert(ins);

// 	Anchor begin = doc.historyAnchor(5);
// 	Anchor end = doc.historyAnchor(25);
// 	Deletion del1(doc.id(), 3, begin, end);
// 	doc.del(del1);

// 	begin = doc.historyAnchor(10);
// 	end = doc.historyAnchor(20);
// 	Deletion del2(doc.id(), 2, begin, end);
// 	doc.del(del2);

// 	UndoOperation uop(doc.id(), 4, OperationID{doc.id(), del1.stamp});
// 	doc.undo(uop);

// 	doc.validate();
// }

// void speedTest(int numInsertions, int minLen = 1, int maxLen = 20)
// {
// 	std::random_device rd;
// 	std::mt19937 gen(rd());
// 	PieceCRDT doc;
// 	size_t tot_len = 0;
// 	uint32_t operation_stamp = 3;
// 	auto start = std::chrono::high_resolution_clock::now();
// 	for (int i = 0; i < numInsertions; ++i)
// 	{
// 		std::string str = generateRandomString(gen, minLen, maxLen);
// 		std::uniform_int_distribution<size_t> pos_dist(0, tot_len);
// 		size_t insert_pos = pos_dist(gen);
// 		Anchor anchor = doc.anchor(insert_pos);
// 		Insertion insertion(doc.id(), operation_stamp++, anchor, str);
// 		doc.insert(insertion);
// 		tot_len += str.size();
// 		// if ((i + 1) % 10 == 0 && tot_len > 0) {
// 		//     std::uniform_int_distribution<> delete_len_dist(5, 10);
// 		//     size_t delete_length = delete_len_dist(gen);
// 		//     if (delete_length > tot_len) delete_length = tot_len;
// 		//     std::uniform_int_distribution<size_t> delete_pos_dist(0, tot_len - delete_length);
// 		//     size_t delete_pos = delete_pos_dist(gen);
// 		//     Anchor begin_anchor = tree.anchor(delete_pos);
// 		//     Anchor end_anchor = tree.anchor(delete_pos + delete_length);
// 		//     Deletion deletion(ReplicaID{0,0}, operation_stamp++, begin_anchor, end_anchor);
// 		//     tree.remove(deletion);
// 		//     tot_len -= delete_length;
// 		// }
// 	}
// 	auto end = std::chrono::high_resolution_clock::now();
// 	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
// 	std::cout << "\nSpeed test completed!\n";
// 	std::cout << "Time taken: " << duration.count() << "ms\n";
// 	std::cout << "Number of pieces in PieceTree: " << doc.size() << "\n";
// 	std::cout << "Average time per insertion: " << duration.count() / (double)numInsertions << "ms\n";
// }

void runHistoryDeleteUndoRedoTestFromFile(const std::string &filename, int start_len = 5000)
{
	std::cout << "Running delete-undo-redo test from file: " << filename << "...\n";

	std::ifstream file(filename);
	if (!file.is_open())
	{
		std::cerr << "Failed to open file: " << filename << "\n";
		return;
	}

	struct FileOp
	{
		char type;
		size_t pos;
		size_t len;
		int stamp;
	};

	std::vector<FileOp> operations;
	std::string line;
	int max_file_stamp = 0;

	while (std::getline(file, line))
	{
		std::stringstream ss(line);
		std::string segment;
		std::vector<std::string> parts;
		while (std::getline(ss, segment, ' '))
		{
			parts.push_back(segment);
		}
		if (parts.empty())
			continue;

		char type = parts[0][0];
		if (type == 'D' && parts.size() == 4)
		{
			int stamp = std::stoi(parts[3]);
			if (stamp > max_file_stamp)
				max_file_stamp = stamp;
			operations.push_back({'D', std::stoul(parts[1]), std::stoul(parts[2]), stamp});
		}
		else if (type == 'U' && parts.size() == 2)
		{
			operations.push_back({'U', 0, 0, std::stoi(parts[1])});
		}
		else if (type == 'R' && parts.size() == 2)
		{
			operations.push_back({'R', 0, 0, std::stoi(parts[1])});
		}
	}

	std::random_device rd;
	std::mt19937 gen(rd());

	PieceCRDTValidator doc;
	uint32_t op_stamp = max_file_stamp + 1;
	std::map<int, ReplicaID> deletion_ids;

	// 1. 插入长度为 5000 的初始文本
	std::string initial = generateRandomString(gen, start_len, start_len);
	Anchor init_anchor = doc.insertAnchor(0);
	Insertion ins(doc.id(), 2, init_anchor, initial);
	doc.insert(ins);

	// 2. 执行文件中的操作
	for (size_t i = 0; i < operations.size(); ++i)
	{
		const auto &op = operations[i];

		if (op.type == 'D')
		{
			std::cout << "Deleting at pos " << op.pos << " length " << op.len << " stamp " << op.stamp << "\n";

			ClosedRange range = doc.historyRange(op.pos, op.pos + op.len);
			Deletion del(generateReplicaID(), op.stamp, range.begin, range.end);
			doc.del(del);
			deletion_ids[op.stamp] = del.replica;

			if (!doc.validate())
			{
				std::cout << "Validation failed after deletion " << i << "\n";
				return;
			}
		}
		else if (op.type == 'U')
		{
			std::cout << "Undoing operation stamp " << op.stamp << "\n";
			UndoOperation uop(doc.id(), op_stamp++, OperationID{deletion_ids[op.stamp], static_cast<uint32_t>(op.stamp)});
 			doc.undo(uop);

			if (!doc.validate())
			{
				std::cout << "Validation failed after undo " << i << "\n";
				// return;
			}
		}
		else if (op.type == 'R')
		{
			std::cout << "Redoing operation stamp " << op.stamp << "\n";
			RedoOperation rop(doc.id(), op_stamp++, OperationID{deletion_ids[op.stamp], static_cast<uint32_t>(op.stamp)});
			doc.redo(rop);

			if (!doc.validate())
			{
				std::cout << "Validation failed after redo " << i << "\n";
				// return;
			}
		}
	}
}

// void oldtagTest()
// {
// 	PieceCRDTValidator doc;
// 	std::cout << "doc id: " << doc.id() << "\n";
// 	uint32_t op_stamp = 2;

// 	std::string initial("Hello, this is a test string for old tag testing.");
// 	Anchor init_anchor = doc.anchor(0);
// 	Insertion ins(doc.id(), op_stamp++, init_anchor, initial);
// 	doc.insert(ins);

// 	auto id1 = op_stamp;
// 	{
// 		Anchor begin = doc.historyAnchor(0);
// 		Anchor end = doc.historyAnchor(20);
// 		Deletion del1(doc.id(), op_stamp++, begin, end);
// 		doc.del(del1);
// 	}

// 	auto id2 = op_stamp;
// 	{
// 		Anchor begin = doc.historyAnchor(5);
// 		Anchor end = doc.historyAnchor(15);
// 		Deletion del1(doc.id(), op_stamp++, begin, end);
// 		doc.del(del1);
// 	}

// 	UndoOperation uop(doc.id(), op_stamp++, OperationID{doc.id(), id2});
// 	doc.undo(uop);
// 	doc.validate();
// 	UndoOperation uop2(doc.id(), op_stamp++, OperationID{doc.id(), id1});
// 	doc.undo(uop2);
// 	doc.validate();
// 	RedoOperation rop(doc.id(), op_stamp++, OperationID{doc.id(), id2});
// 	doc.redo(rop);
// 	doc.validate();
// }

// void insertUndoTest()
// {
// 	PlainText text;
// 	text.insert(0, "12345");
// 	PlainText text2(text.replicaID());
// 	text2.apply(text.diff());

// 	text2.del(0, 5);
// 	text.apply(text2.diff(text.frontline()));
// 	std::cout << "Text after sync: " << text.toString() << "\n";

// 	text.insert(0, "aaa");
// 	text.undo();
// 	text2.apply(text.diff(text2.frontline()));
// 	std::cout << "Text after sync: " << text2.toString() << "\n";

// 	text2.insert(0, "bbb");
// 	text2.undo();
// 	text2.redo();
// 	std::cout << "Text2 after undo: " << text2.toString() << "\n";
// }

// void insertOnEdgeTest()
// {
// 	PlainText text;
// 	text.insert(0, "aaa");
// 	text.insert(0, "bbb");
// 	Anchor anchor = text.toAnchor(3);
// 	text.undo();
// 	text.undo();
// 	text.apply(Insertion{generateReplicaID(), 10, anchor, "ccc"});
// 	std::cout << "Text after insert on edge: " << text.toString() << "\n";
// }

void selectionTest()
{
	PlainText doc;
	doc.insert(0, "Hello CRDT! Editor 1");
	doc.del(0, 20);
	doc.insert(0, "aaa");

	PlainText doc2(doc.origin());
	doc2.apply(doc.diff(doc2.frontline()));
	doc2.insert(3, "bbb");

	std::cout << doc.toPos(doc.toAnchor(3)) << "\n";
	doc.apply(doc2.diff(doc.frontline()));
	std::cout << doc.toString() << '\n'; // aaabbb
	doc.undo();
	std::cout << doc.toString() << '\n'; // aaabbb
}

int main(int argn, char **argv)
{
	// selectionTest();
	// insertOnEdgeTest();
	// oldtagTest();
	// insertUndoTest();
	// text.redo();
	// std::cout << text.toString() << "\n";
	// text.diff();
	// coverTest();
	// runInsertDeleteTest(1000, 30, 40);
	// readTest("../../../test4.txt");
	// runDeleteUndoRedoTest(200, 5000);
	runHistoryDeleteUndoRedoTest(200, 5000);
	// runHistoryDeleteUndoRedoTestFromFile("../../../test6.txt", 5000);
	// int numInsertions = 5000; // 默认插入次数
	// if (argn > 1)
	// {
	// 	numInsertions = std::atoi(argv[1]);
	// }

	// std::cout << "Running random test with " << numInsertions << " insertions...\n";
	// runRandomTest(numInsertions);

	// numInsertions = 1000000; // 速度测试插入次数
	// std::cout << "Running speed test with " << numInsertions << " insertions...\n";
	// speedTest(numInsertions);

	return 0;
}
