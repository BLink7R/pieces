#include "../src/text.hpp"
#include <emscripten/bind.h>

using namespace emscripten;

// Helpers for property accessors
namespace
{

std::string getOpIDReplica(const OperationID &o) { return uuids::to_string(o.replica); }
void setOpIDReplica(OperationID &o, const std::string &s)
{
	if (auto id = uuids::uuid::from_string(s))
		o.replica = *id;
}

std::string getAnchorReplica(const Anchor &a) { return uuids::to_string(a.replica); }
void setAnchorReplica(Anchor &a, const std::string &s)
{
	if (auto id = uuids::uuid::from_string(s))
		a.replica = *id;
}

std::string getOpReplica(const Operation &o) { return uuids::to_string(o.replica); }
void setOpReplica(Operation &o, const std::string &s)
{
	if (auto id = uuids::uuid::from_string(s))
		o.replica = *id;
}

} // namespace

EMSCRIPTEN_BINDINGS(my_module)
{
	enum_<OperationType>("OperationType")
		.value("Insert", OperationType::Insert)
		.value("Delete", OperationType::Delete)
		.value("Format", OperationType::Format)
		.value("Undo", OperationType::Undo)
		.value("Redo", OperationType::Redo);

	value_object<OperationID>("OperationID")
		.field("replica", &getOpIDReplica, &setOpIDReplica)
		.field("stamp", &OperationID::stamp);

	value_object<Anchor>("Anchor")
		.field("replica", &getAnchorReplica, &setAnchorReplica)
		.field("stamp", &Anchor::stamp)
		.field("pos", &Anchor::pos);

	class_<Operation>("Operation")
		.property("replica", &getOpReplica, &setOpReplica)
		.property("stamp", &Operation::stamp)
		.property("type", &Operation::type);

	class_<Insertion, base<Operation>>("Insertion")
		.property("anchor", &Insertion::anchor)
		.property("str", &Insertion::str);

	class_<Deletion, base<Operation>>("Deletion")
		.property("begin", &Deletion::begin)
		.property("end", &Deletion::end);

	class_<UndoOperation, base<Operation>>("UndoOperation")
		.property("target", &UndoOperation::target);

	class_<RedoOperation, base<Operation>>("RedoOperation")
		.property("target", &RedoOperation::target);

	register_vector<OperationID>("VectorOperationID");
	register_vector<std::shared_ptr<Operation>>("VectorOperation");

	class_<PlainText>("PlainText")
		.constructor<>()
		.function("size", &PlainText::size)
		.function("empty", &PlainText::empty)
		.function("toString", &PlainText::toString)
		.function("slice", select_overload<std::string(size_t, size_t) const>(&PlainText::slice))
		.function("insert", select_overload<size_t(size_t, const std::string &)>(&PlainText::insert))
		.function("insertAnchor", select_overload<size_t(const Anchor &, const std::string &)>(&PlainText::insert))
		.function("del", select_overload<size_t(size_t, size_t)>(&PlainText::del))
		.function("delAnchor", select_overload<size_t(const Anchor &, const Anchor &)>(&PlainText::del))
		.function("toAnchor", &PlainText::toAnchor)
		.function("toOffset", &PlainText::toPos)
		.function("undo", &PlainText::undo)
		.function("redo", &PlainText::redo)
		.function("canUndo", &PlainText::canUndo)
		.function("canRedo", &PlainText::canRedo)
		.function("undoSpecific", &PlainText::undoSpecific)
		.function("redoSpecific", &PlainText::redoSpecific)
		.function("replicaID", &PlainText::replicaID)
		.function("apply", select_overload<void(const Operation &)>(&PlainText::apply))
		.function("applyBatch", select_overload<void(const std::vector<Operation> &)>(&PlainText::apply))
		.function("frontline", &PlainText::frontline)
		.function("diff", &PlainText::diff)
		;
}
