#include <gtest/gtest.h>

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
	std::random_device rd;
	std::mt19937 gen(rd());

	PlainText doc;
	SimpleDeferredText validator;
	std::set<std::string> test_set;
	size_t tot_len = 0;
	uint32_t operation_stamp = 3;

	for (int i = 0; i < numInsertions; ++i)
	{
		std::string str = generateRandomString(gen, minLen, maxLen);

		std::uniform_int_distribution<size_t> pos_dist(0, tot_len);
		size_t insert_pos = pos_dist(gen);

		validator.insert(insert_pos, str);
		doc.insert(insert_pos, str);
		tot_len += str.size();

		if ((i + 1) % 50 == 0 && tot_len > 0)
		{
			std::string tree_content = doc.toString();
			bool content_match = (tree_content == validator.toString());
			EXPECT_TRUE(content_match);
		}
	}
}

void runInsertDeleteTest(int numOps, int minLen = 1, int maxLen = 20)
{
	std::random_device rd;
	std::mt19937 gen(rd());

	PlainText doc;
	SimpleText validator;
	size_t tot_len = 0;

	for (int i = 0; i < numOps; ++i)
	{
		std::string str = generateRandomString(gen, minLen, maxLen);
		std::uniform_int_distribution<size_t> pos_dist(0, tot_len);
		size_t insert_pos = pos_dist(gen);

		validator.insert(insert_pos, str);
		doc.insert(insert_pos, str);
		// std::cout << "I " << str << " " << insert_pos << "\n";
		tot_len += str.size();

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

			// std::cout << "D " << del_pos << " " << del_pos + len << "\n";
			doc.del(del_pos, del_pos + len);
			validator.erase(del_pos, len);
			tot_len -= len;
		}

		std::string tree_content = doc.toString();
		std::string expect = validator.toString();
		bool match = (tree_content == expect);
		EXPECT_TRUE(match);
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
		if (!match)
		{
			std::cout << "Content differs (len: " << tree_content.length() << ")\n";
			std::cout << "Test failed!" << std::endl;
			std::cout << "Doc size: " << tree_content.length() << ", Validator size: " << expect.length() << "\n";
			break;
		}
		EXPECT_TRUE(match);
	}
}

void runDeleteUndoRedoTest(int numOps = 200, int start_len = 5000)
{
	std::random_device rd;
	std::mt19937 gen(rd());

	PieceCRDT doc;
	SimpleDeferredText validator;
	validator.insert(0, "");
	uint32_t op_stamp = 2;

	// 1. initial text
	std::string initial = generateRandomString(gen, start_len, start_len);
	Anchor init_anchor = doc.insertAnchor(0);
	Insertion ins(doc.id(), op_stamp++, init_anchor, initial);
	doc.insert(ins);
	validator.insert(0, initial);

	std::vector<OperationID> deletions;
	deletions.reserve(numOps);

	// 2. 200 random deletions
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

		Anchor begin = doc.reversedAnchor(pos);
		Anchor end = doc.anchor(pos + len);
		Deletion del(doc.id(), op_stamp, begin, end);
		doc.del(del);

		deletions.push_back(OperationID{doc.id(), op_stamp});

		validator.erase(pos, len);

		++op_stamp;
	}

	auto check_equal = [&](const char *phase)
	{
		std::string doc_str = doc.toString();
		std::string val_str = validator.toString();
		bool match = (doc_str == val_str);
		if (!match)
		{
			std::cout << phase << " content " << (match ? "matches" : "differs") << "\n";
			std::cout << "Doc size: " << doc.size()
					  << ", Validator size: " << val_str.size() << "\n";
		}
		EXPECT_TRUE(match);
	};
	check_equal("After deletions");

	// 3. Shuffle the 200 deletions and undo
	std::shuffle(deletions.begin(), deletions.end(), gen);
	for (auto &opid : deletions)
	{
		UndoOperation uop(doc.id(), op_stamp++, opid);
		doc.undo(uop);
		validator.undo(opid.stamp);
	}
	check_equal("After undos");

	// 4. Shuffle the 200 deletions and redo
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
	std::random_device rd;
	std::mt19937 gen(rd());

	PieceCRDTValidator doc;
	uint32_t op_stamp = 2;

	// 1. initial text
	std::string initial = generateRandomString(gen, start_len, start_len);
	Anchor init_anchor = doc.insertAnchor(0);
	Insertion ins(doc.id(), op_stamp++, init_anchor, initial);
	doc.insert(ins);

	std::vector<std::pair<ReplicaID, uint32_t>> deletion_ids;
	deletion_ids.reserve(numOps);
	for (int i = 0; i < numOps; ++i)
		deletion_ids.emplace_back(uuids::uuid_random_generator(gen)(), op_stamp++);
	std::shuffle(deletion_ids.begin(), deletion_ids.end(), gen);

	// 2. 200 random deletions
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

		// std::cout << "Deleting at pos " << pos << " length " << len << " id " << deletion_ids[i].second << "\n";
		ClosedRange range = doc.historyRange(pos, pos + len);
		Deletion del(deletion_ids[i].first, deletion_ids[i].second, range.begin, range.end);
		doc.del(del);

		bool ok = doc.validate();
		EXPECT_TRUE(ok) << "Validation failed after deletion " << i;
		if (!ok)
		{
			std::cout << "Validation failed after deletion " << i << "\n";
			return;
		}
	}

	// 3. reshuffle the 200 deletions and undo
	std::shuffle(deletion_ids.begin(), deletion_ids.end(), gen);
	for (auto &opid : deletion_ids)
	{
		// std::cout << "Undoing operation stamp " << opid.second << "\n";
		UndoOperation uop(doc.id(), op_stamp++, OperationID{opid.first, opid.second});
		doc.undo(uop);
		doc.validate();
	}

	// 4. reshuffle the 200 deletions and redo
	std::shuffle(deletion_ids.begin(), deletion_ids.end(), gen);
	for (auto &opid : deletion_ids)
	{
		// std::cout << "Redoing operation stamp " << opid.second << "\n";
		RedoOperation rop(doc.id(), op_stamp++, OperationID{opid.first, opid.second});
		doc.redo(rop);
		doc.validate();
	}
}

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

	std::string initial = generateRandomString(gen, start_len, start_len);
	Anchor init_anchor = doc.insertAnchor(0);
	Insertion ins(doc.id(), 2, init_anchor, initial);
	doc.insert(ins);

	for (size_t i = 0; i < operations.size(); ++i)
	{
		const auto &op = operations[i];

		if (op.type == 'D')
		{
			// std::cout << "Deleting at pos " << op.pos << " length " << op.len << " stamp " << op.stamp << "\n";

			ClosedRange range = doc.historyRange(op.pos, op.pos + op.len);
			Deletion del(generateReplicaID(), op.stamp, range.begin, range.end);
			doc.del(del);
			deletion_ids[op.stamp] = del.replica;

			bool ok = doc.validate();
			if (!ok)
			{
				std::cout << "Validation failed after deletion " << i << "\n";
			}
			EXPECT_TRUE(ok);
		}
		else if (op.type == 'U')
		{
			// std::cout << "Undoing operation stamp " << op.stamp << "\n";
			UndoOperation uop(doc.id(), op_stamp++, OperationID{deletion_ids[op.stamp], static_cast<uint32_t>(op.stamp)});
			doc.undo(uop);

			bool ok = doc.validate();
			if (!ok)
			{
				std::cout << "Validation failed after undo " << i << "\n";
			}
			EXPECT_TRUE(ok);
		}
		else if (op.type == 'R')
		{
			// std::cout << "Redoing operation stamp " << op.stamp << "\n";
			RedoOperation rop(doc.id(), op_stamp++, OperationID{deletion_ids[op.stamp], static_cast<uint32_t>(op.stamp)});
			doc.redo(rop);

			bool ok = doc.validate();
			if (!ok)
			{
				std::cout << "Validation failed after redo " << i << "\n";
			}
			EXPECT_TRUE(ok);
		}
	}
}

TEST(RandomTest, InsertOnlyRandom)
{
	runInsertTest(500, 1, 20);
}

TEST(RandomTest, InsertDeleteRandom)
{
	runInsertDeleteTest(500, 10, 20);
}

TEST(RandomTest, DeleteUndoRedoRandom)
{
	runDeleteUndoRedoTest(100, 1000);
}

TEST(RandomTest, HistoryDeleteUndoRedoRandom)
{
	runHistoryDeleteUndoRedoTest(50, 1000);
}

// two replicas insert/delete text and inline objects concurrently,
// verify both converge to a shared reference model.
// all content here is single-byte (ASCII text + 0xFF object marker), so
// character offsets equal byte offsets.
void runInlineObjectTest(int numOps)
{
	const std::string obj_marker = "\xFF";
	std::random_device rd;
	std::mt19937 gen(rd());

	PlainText a;
	PlainText b(a.origin());
	SimpleText validator;
	size_t tot_chars = 0;

	for (int i = 0; i < numOps; ++i)
	{
		PlainText *target = (gen() % 2 == 0) ? &a : &b;
		std::uniform_int_distribution<int> op_dist(0, 2);
		switch (op_dist(gen))
		{
		case 0: // insert text
		{
			std::string str = generateRandomString(gen, 1, 5);
			std::uniform_int_distribution<size_t> pos_dist(0, tot_chars);
			size_t pos = pos_dist(gen);
			validator.insert(pos, str);
			target->insert(pos, str);
			tot_chars += str.size();
			break;
		}
		case 1: // insert inline object
		{
			std::string id = "obj_" + std::to_string(i);
			std::uniform_int_distribution<size_t> pos_dist(0, tot_chars);
			size_t pos = pos_dist(gen);
			validator.insert(pos, obj_marker);
			target->insertObject(pos, id);
			++tot_chars;
			break;
		}
		default: // delete a range
		{
			if (tot_chars == 0)
			{
				--i;
				continue;
			}
			std::uniform_int_distribution<> len_dist(1, std::min<size_t>(20, tot_chars));
			size_t len = len_dist(gen);
			std::uniform_int_distribution<size_t> pos_dist(0, tot_chars - len);
			size_t pos = pos_dist(gen);
			validator.erase(pos, len);
			target->del(pos, pos + len);
			tot_chars -= len;
			break;
		}
		}

		// sync both ways
		b.apply(a.diff(b.frontline()));
		a.apply(b.diff(a.frontline()));

		std::string expect = validator.toString();
		EXPECT_EQ(a.toString(), expect) << "iteration " << i;
		EXPECT_EQ(b.toString(), expect) << "iteration " << i;
	}
}

TEST(RandomTest, InlineObjectRandom)
{
	runInlineObjectTest(200);
}
