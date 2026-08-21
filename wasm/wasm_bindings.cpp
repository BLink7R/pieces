#include "../src/text.hpp"
#include <emscripten/bind.h>
#include <emscripten/val.h>

using namespace emscripten;

// Helpers for property accessors
namespace
{

// Helper for Apply
void applyHelper(PlainText<char16_t> &self, const val &opVal)
{
	if (opVal.isNull() || opVal.isUndefined())
		return;
	if (opVal.isArray())
	{
		unsigned length = opVal["length"].as<unsigned>();
		for (unsigned i = 0; i < length; ++i)
		{
			applyHelper(self, opVal[i]);
		}
		return;
	}

	OperationType type = opVal["type"].as<OperationType>();
	val::global("console").call<void>("log", std::string("apply type: ") + std::to_string(static_cast<int>(type)));
	switch (type)
	{
	case OperationType::Insert:
		self.apply(opVal.as<Insertion<char16_t>>()); // Auto-conversion from JS object to Insertion<char16_t> struct
		break;
	case OperationType::Delete:
		self.apply(opVal.as<Deletion>());
		break;
	case OperationType::Undo:
		self.apply(opVal.as<UndoOperation>());
		break;
	case OperationType::Redo:
		self.apply(opVal.as<RedoOperation>());
		break;
	default:
		break;
	}
}

val diffHelper(PlainText<char16_t> &self, const val &frontlineVal)
{
	std::vector<OperationID> frontline;
	if (frontlineVal.isArray())
	{
		unsigned length = frontlineVal["length"].as<unsigned>();
		for (unsigned i = 0; i < length; ++i)
		{
			frontline.push_back(frontlineVal[i].as<OperationID>());
		}
	}
	else if (frontlineVal.isUndefined() || frontlineVal.isNull())
	{
		// Empty frontline
	}
	else
	{
		// Output error to JS console
		val::global("console").call<void>("error", std::string("diffHelper: Invalid frontline argument. Expected Array, null, or undefined."));
		return val::array();
	}

	auto ops = self.diff(frontline);
	val result = val::array();
	for (const auto &op : ops)
	{
		switch (op->type)
		{
		case OperationType::Insert:
			result.call<void>("push", *static_cast<Insertion<char16_t> *>(op.get()));
			break;
		case OperationType::Delete:
			result.call<void>("push", *static_cast<Deletion *>(op.get()));
			break;
		case OperationType::Undo:
			result.call<void>("push", *static_cast<UndoOperation *>(op.get()));
			break;
		case OperationType::Redo:
			result.call<void>("push", *static_cast<RedoOperation *>(op.get()));
			break;
		default:
			// Fallback, maybe shouldn't happen or push partially?
			// result.call<void>("push", *op); // Base value object not ideal
			continue;
		}
	}
	return result;
}

val frontlineHelper(PlainText<char16_t> &self)
{
	auto frontline = self.frontline();
	val result = val::array();
	for (const auto &opID : frontline)
	{
		result.call<void>("push", opID);
	}
	return result;
}

std::string replicaIDHelper(const PlainText<char16_t> &self) { return uuids::to_string(self.replicaID()); }

std::string originHelper(const PlainText<char16_t> &self) { return uuids::to_string(self.origin()); }

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
		.value("ParagraphHead", OperationType::ParagraphHead)
		.value("InlineObject", OperationType::InlineObject)
		.value("Delete", OperationType::Delete)
		.value("RangeFormat", OperationType::RangeFormat)
		.value("Undo", OperationType::Undo)
		.value("Redo", OperationType::Redo);

	value_object<OperationID>("OperationID")
		.field("replica", &getOpIDReplica, &setOpIDReplica)
		.field("stamp", &OperationID::stamp);

	value_object<Anchor>("Anchor")
		.field("replica", &getAnchorReplica, &setAnchorReplica)
		.field("stamp", &Anchor::stamp)
		.field("offset", &Anchor::offset);

	value_object<OpenedRange>("OpenedRange")
		.field("begin", &OpenedRange::begin)
		.field("end", &OpenedRange::end);

	value_object<ClosedRange>("ClosedRange")
		.field("begin", &ClosedRange::begin)
		.field("end", &ClosedRange::end);

	// Note: value_object cannot inherit from other value_objects.
	// We must flatten the fields from the base Operation class into each derived struct's value_object definition.
	value_object<Insertion<char16_t>>("Insertion")
		// Base Operation fields
		.field("replica", &getOpReplica, &setOpReplica)
		.field("stamp", &Operation::stamp)
		.field("type", &Operation::type)
		// Insertion<char16_t> specific fields
		.field("anchor", &Insertion<char16_t>::anchor)
		.field("str", &Insertion<char16_t>::str);

	value_object<ParagraphHeadInsert>("ParagraphHeadInsert")
		// Base Operation fields
		.field("replica", &getOpReplica, &setOpReplica)
		.field("stamp", &Operation::stamp)
		.field("type", &Operation::type)
		// ParagraphHeadInsert specific fields
		.field("anchor", &ParagraphHeadInsert::anchor);

	value_object<InlineObjectInsert>("InlineObjectInsert")
		// Base Operation fields
		.field("replica", &getOpReplica, &setOpReplica)
		.field("stamp", &Operation::stamp)
		.field("type", &Operation::type)
		// InlineObjectInsert specific fields
		.field("anchor", &InlineObjectInsert::anchor)
		.field("id", &InlineObjectInsert::id);

	value_object<Deletion>("Deletion")
		// Base Operation fields
		.field("replica", &getOpReplica, &setOpReplica)
		.field("stamp", &Operation::stamp)
		.field("type", &Operation::type)
		// Deletion specific fields
		.field("range", &Deletion::range);

	value_object<UndoOperation>("UndoOperation")
		// Base Operation fields
		.field("replica", &getOpReplica, &setOpReplica)
		.field("stamp", &Operation::stamp)
		.field("type", &Operation::type)
		// Undo specific fields
		.field("target", &UndoOperation::target);

	value_object<RedoOperation>("RedoOperation")
		// Base Operation fields
		.field("replica", &getOpReplica, &setOpReplica)
		.field("stamp", &Operation::stamp)
		.field("type", &Operation::type)
		// Redo specific fields
		.field("target", &RedoOperation::target);

	register_vector<OperationID>("VectorOperationID");

	class_<PlainText<char16_t>>("PlainText")
		.constructor<>()
		.function("size", &PlainText<char16_t>::size)
		.function("empty", &PlainText<char16_t>::empty)
		.function("toString", &PlainText<char16_t>::toString)
		.function("slice", select_overload<std::u16string(size_t, size_t) const>(&PlainText<char16_t>::slice))
		.function("insert", select_overload<size_t(size_t, const std::u16string &)>(&PlainText<char16_t>::insert))
		.function("insertAnchor", select_overload<size_t(const Anchor &, const std::u16string &)>(&PlainText<char16_t>::insert))
		.function("del", select_overload<size_t(size_t, size_t)>(&PlainText<char16_t>::del))
		.function("delAnchor", select_overload<size_t(const ClosedRange &)>(&PlainText<char16_t>::del))
		.function("toRange", &PlainText<char16_t>::toClosedRange)
		.function("toAnchor", &PlainText<char16_t>::toAnchor)
		.function("toOffset", &PlainText<char16_t>::toOffset)
		.function("undo", &PlainText<char16_t>::undo)
		.function("redo", &PlainText<char16_t>::redo)
		.function("canUndo", &PlainText<char16_t>::canUndo)
		.function("canRedo", &PlainText<char16_t>::canRedo)
		.function("undoSpecific", &PlainText<char16_t>::undoSpecific)
		.function("redoSpecific", &PlainText<char16_t>::redoSpecific)
		.function("replicaID", &replicaIDHelper)
		.function("origin", &originHelper)
		.function("apply", &applyHelper)
		.function("frontline", &frontlineHelper)
		.function("diff", &diffHelper);
}
