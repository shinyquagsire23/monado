// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C++ wrapper for cJSON.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "util/u_file.h"
#include "util/u_debug.h"
#include "cjson/cJSON.h"

#include <cassert>
#include <stack>
#include <vector>
#include <map>
#include <memory>
#include <variant>
#include <fstream>
#include <sstream>

DEBUG_GET_ONCE_LOG_OPTION(json_log, "JSON_LOG", U_LOGGING_WARN)

#define JSON_TRACE(...) U_LOG_IFL_T(debug_get_log_option_json_log(), __VA_ARGS__)
#define JSON_DEBUG(...) U_LOG_IFL_D(debug_get_log_option_json_log(), __VA_ARGS__)
#define JSON_INFO(...) U_LOG_IFL_I(debug_get_log_option_json_log(), __VA_ARGS__)
#define JSON_WARN(...) U_LOG_IFL_W(debug_get_log_option_json_log(), __VA_ARGS__)
#define JSON_ERROR(...) U_LOG_IFL_E(debug_get_log_option_json_log(), __VA_ARGS__)
#define JSON_ASSERT(fatal, predicate, ...)                                                                             \
	do {                                                                                                           \
		bool p = predicate;                                                                                    \
		if (!p) {                                                                                              \
			JSON_ERROR(__VA_ARGS__);                                                                       \
			if (fatal) {                                                                                   \
				assert(false && "Assertion failed: " #predicate);                                      \
				exit(EXIT_FAILURE);                                                                    \
			}                                                                                              \
		}                                                                                                      \
	} while (false);

// Fatal assertion
#define JSON_ASSERTF(predicate, ...) JSON_ASSERT(true, predicate, __VA_ARGS__)
#define JSON_ASSERTF_(predicate) JSON_ASSERT(true, predicate, "Assertion failed " #predicate)

// Warn-only assertion
#define JSON_ASSERTW(predicate, ...) JSON_ASSERT(false, predicate, __VA_ARGS__)

namespace xrt::auxiliary::util::json {

using std::get;
using std::get_if;
using std::holds_alternative;
using std::make_shared;
using std::map;
using std::shared_ptr;
using std::string;
using std::to_string;
using std::variant;
using std::vector;

class JSONBuilder;

/*!
 * @brief A JSONNode wraps a cJSON object and presents useful functions for
 * accessing the different properties of the json structure like `operator[]`,
 * `isType()` and `asType()` methods.
 *
 * The main ways a user can build a JSONNode is from a json string, from a
 * json file with `loadFromFile` or with the @ref JSONBuilder.
 */
class JSONNode
{
private:
	friend class JSONBuilder;
	using Ptr = shared_ptr<JSONNode>;

	//! Wrapped cJSON object
	cJSON *cjson = nullptr;

	//! Whether this node is responsible for deleting the cjson object
	bool is_owner = false;

	//! Parent of this node, used only by @ref JSONBuilder.
	JSONNode::Ptr parent = nullptr;

public:
	// Class resource management

	//! This is public so that make_shared works; do not use outside of this file.
	JSONNode(cJSON *cjson, bool is_owner, const JSONNode::Ptr &parent)
	    : cjson(cjson), is_owner(is_owner), parent(parent)
	{}

	//! Wrap cJSON object for easy manipulation, does not take ownership
	JSONNode(cJSON *cjson) : JSONNode(cjson, false, nullptr) {}

	//! Makes a null object; `isInvalid()` on it returns true.
	JSONNode() {}

	//! Receives a json string and constructs a wrapped cJSON object out of it.
	JSONNode(const string &content)
	{
		cjson = cJSON_Parse(content.c_str());
		if (cjson == nullptr) {
			const int max_length = 64;
			string msg = string(cJSON_GetErrorPtr()).substr(0, max_length);
			JSON_ERROR("Invalid syntax right before: '%s'", msg.c_str());
			return;
		}
		is_owner = true;
		parent = nullptr;
	}

	JSONNode(JSONNode &&) = default;

	JSONNode(const JSONNode &node)
	{
		is_owner = node.is_owner;
		parent = node.parent;
		if (node.is_owner) {
			cjson = cJSON_Duplicate(node.cjson, true); // Deep copy
		} else {
			cjson = node.cjson; // Shallow copy
		}
	};

	JSONNode &
	operator=(JSONNode &&) = default;

	JSONNode &
	operator=(JSONNode rhs)
	{
		swap(*this, rhs);
		return *this;
	};

	~JSONNode()
	{
		if (is_owner) {
			cJSON_Delete(cjson);
		}
	}

	friend void
	swap(JSONNode &lhs, JSONNode &rhs) noexcept
	{
		using std::swap;
		swap(lhs.cjson, rhs.cjson);
		swap(lhs.is_owner, rhs.is_owner);
		swap(lhs.parent, rhs.parent);
	}

	// Methods for explicit usage by the user of this class

	static JSONNode
	loadFromFile(const string &filepath)
	{
		std::ifstream file(filepath);
		if (!file.is_open()) {
			JSON_ERROR("Unable to open file %s", filepath.c_str());
			return JSONNode{};
		}

		std::stringstream stream{};
		stream << file.rdbuf();
		string content = stream.str();

		return JSONNode{content};
	}

	bool
	saveToFile(const string &filepath) const
	{
		string contents = toString(false);
		std::ofstream file(filepath);

		if (!file.is_open()) {
			JSON_ERROR("Unable to open file %s", filepath.c_str());
			return false;
		}

		file << contents;
		return true;
	}

	JSONNode
	operator[](const string &key) const
	{
		const char *name = key.c_str();
		JSON_ASSERTW(isObject(), "Trying to retrieve field '%s' from non-object %s", name, toString().c_str());

		cJSON *value = cJSON_GetObjectItemCaseSensitive(cjson, name);
		JSON_ASSERTW(value != nullptr, "Unable to retrieve field '%s' from %s", name, toString().c_str());

		return JSONNode{value, false, nullptr};
	}

	JSONNode
	operator[](int i) const
	{
		JSON_ASSERTW(isArray(), "Trying to retrieve index '%d' from non-array %s", i, toString().c_str());

		cJSON *value = cJSON_GetArrayItem(cjson, i);
		JSON_ASSERTW(value != nullptr, "Unable to retrieve index %d from %s", i, toString().c_str());

		return JSONNode{value, false, nullptr};
	}

	// clang-format off
	bool isObject() const { return cJSON_IsObject(cjson); }
	bool isArray() const { return cJSON_IsArray(cjson); }
	bool isString() const { return cJSON_IsString(cjson); }
	bool isNumber() const { return cJSON_IsNumber(cjson); }
	bool isInt() const { return isNumber() && cjson->valuedouble == cjson->valueint; }
	bool isDouble() const { return isNumber(); }
	bool isNull() const { return cJSON_IsNull(cjson); }
	bool isBool() const { return cJSON_IsBool(cjson); }
	bool isInvalid() const { return cjson == nullptr || cJSON_IsInvalid(cjson); }
	bool isValid() const { return !isInvalid(); }

	bool canBool() const { return isBool() || (isInt() && (cjson->valueint == 0 || cjson->valueint == 1)); }
	// clang-format on

	map<string, JSONNode>
	asObject(const map<string, JSONNode> &otherwise = map<string, JSONNode>()) const
	{
		JSON_ASSERTW(isObject(), "Invalid object: %s, defaults", toString().c_str());
		if (isObject()) {
			map<string, JSONNode> object{};

			cJSON *item = NULL;
			cJSON_ArrayForEach(item, cjson)
			{
				const char *key = item->string;
				JSON_ASSERTF(key, "Unexpected unnamed pair in json: %s", toString().c_str());
				JSON_ASSERTW(object.count(key) == 0, "Duplicated key '%s'", key);
				object.insert({key, JSONNode{item, false, nullptr}});
			}

			return object;
		}
		return otherwise;
	}

	vector<JSONNode>
	asArray(const vector<JSONNode> &otherwise = vector<JSONNode>()) const
	{
		JSON_ASSERTW(isArray(), "Invalid array: %s, defaults", toString().c_str());
		if (isArray()) {
			vector<JSONNode> array{};

			cJSON *item = NULL;
			cJSON_ArrayForEach(item, cjson)
			{
				array.push_back(JSONNode{item, false, nullptr});
			}

			return array;
		}
		return otherwise;
	}

	string
	asString(const string &otherwise = "") const
	{
		JSON_ASSERTW(isString(), "Invalid string: %s, defaults %s", toString().c_str(), otherwise.c_str());
		return isString() ? cjson->valuestring : otherwise;
	}

	int
	asInt(int otherwise = 0) const
	{
		JSON_ASSERTW(isInt(), "Invalid int: %s, defaults %d", toString().c_str(), otherwise);
		return isInt() ? cjson->valueint : otherwise;
	}

	double
	asDouble(double otherwise = 0.0) const
	{
		JSON_ASSERTW(isDouble(), "Invalid double: %s, defaults %lf", toString().c_str(), otherwise);
		return isDouble() ? cjson->valuedouble : otherwise;
	}

	void *
	asNull(void *otherwise = nullptr) const
	{
		JSON_ASSERTW(isNull(), "Invalid null: %s, defaults %p", toString().c_str(), otherwise);
		return isNull() ? nullptr : otherwise;
	}

	bool
	asBool(bool otherwise = false) const
	{
		JSON_ASSERTW(canBool(), "Invalid bool: %s, defaults %d", toString().c_str(), otherwise);
		return isBool() ? cJSON_IsTrue(cjson) : (canBool() ? cjson->valueint : otherwise);
	}

	bool
	hasKey(const string &key) const
	{
		return asObject().count(key) == 1;
	}

	string
	toString(bool show_field = true) const
	{
		char *cstr = cJSON_Print(cjson);
		string str{cstr};
		free(cstr);

		// Show the named field this comes from if any
		if (show_field) {
			str += "\nFrom field named: " + getName();
		}

		return str;
	}

	string
	getName() const
	{
		return string(cjson->string ? cjson->string : "");
	}

	cJSON *
	getCJSON()
	{
		return cjson;
	}
};


/*!
 * @brief Helper class for building cJSON trees through `operator<<`
 *
 * JSONBuild is implemented with a pushdown automata to keep track of the JSON
 * construction state.
 *
 */
class JSONBuilder
{
private:
	enum class StackAlphabet
	{
		Base, // Unique occurrence as the base of the stack
		Array,
		Object
	};

	enum class State
	{
		Empty,
		BuildArray,
		BuildObjectKey,
		BuildObjectValue,
		Finish,
		Invalid
	};

	enum class InputAlphabet
	{
		StartArray,
		EndArray,
		StartObject,
		EndObject,
		PushKey,
		PushValue,
	};

	using G = StackAlphabet;
	using S = InputAlphabet;
	using Q = State;

	std::stack<StackAlphabet> stack{{G::Base}};
	State state{Q::Empty};
	JSONNode::Ptr node = nullptr; //!< Current node we are pointing to in the tree.

	using JSONValue = variant<string, const char *, int, double, bool>;

	//! String representation of @p value.
	static string
	valueToString(const JSONValue &value)
	{
		string s = "JSONValue<invalid>()";
		if (const string *v = get_if<string>(&value)) {
			s = string{"JSONValue<string>("} + *v + ")";
		} else if (const char *const *v = get_if<const char *>(&value)) {
			s = string{"JSONValue<const char*>("} + *v + ")";
		} else if (const int *v = get_if<int>(&value)) {
			s = string{"JSONValue<int>("} + to_string(*v) + ")";
		} else if (const double *v = get_if<double>(&value)) {
			s = string{"JSONValue<double>("} + to_string(*v) + ")";
		} else if (const bool *v = get_if<bool>(&value)) {
			s = string{"JSONValue<bool>("} + to_string(*v) + ")";
		} else {
			JSON_ASSERTF(false, "Unsupported variant type");
			s = "[Invalid JSONValue]";
		}
		return s;
	}

	//! Construct a cJSON object out of native types.
	static cJSON *
	makeCJSONValue(const JSONValue &value)
	{
		cJSON *ret = nullptr;
		if (holds_alternative<string>(value)) {
			ret = cJSON_CreateString(get<string>(value).c_str());
		} else if (holds_alternative<const char *>(value)) {
			ret = cJSON_CreateString(get<const char *>(value));
		} else if (holds_alternative<int>(value)) {
			ret = cJSON_CreateNumber(get<int>(value));
		} else if (holds_alternative<double>(value)) {
			ret = cJSON_CreateNumber(get<double>(value));
		} else if (holds_alternative<bool>(value)) {
			ret = cJSON_CreateBool(get<bool>(value));
		} else {
			JSON_ASSERTF(false, "Unexpected value");
		}
		return ret;
	}

	/*!
	 * @brief Receives inputs and transitions the automata from state to state.
	 *
	 * This is the table of transitions. Can be thought of as three regular FSM
	 * that get switched based on the stack's [top] value. The function is
	 * the implementation of the table.
	 *
	 * [top],  [state],          [symbol]    -> [new-state],      [stack-action]
	 * Base,   Empty,            PushValue   -> Finish,           -
	 * Base,   Empty,            StartObject -> BuildObjectKey,   push(Object)
	 * Base,   Empty,            StartArray  -> BuildArray,       push(Array)
	 * Array,  BuildArray,       PushValue   -> BuildArray,       -
	 * Array,  BuildArray,       StartArray  -> BuildArray,       push(Array)
	 * Array,  BuildArray,       EndArray    -> [1],              pop
	 * Array,  BuildArray,       StartObject -> BuildObjectKey,   push(Object)
	 * Object, BuildObjectKey,   PushKey     -> BuildObjectValue, -
	 * Object, BuildObjectKey,   EndObject   -> [1],              pop
	 * Object, BuildObjectValue, PushValue   -> BuildObjectKey,   -
	 * Object, BuildObjectValue, StartObject -> BuildObjectKey,   push(Object)
	 * Object, BuildObjectValue, StartArray  -> BuildArray,       push(Array)
	 * _,      _,                _,          -> Invalid,          -
	 *
	 * [1]: Empty or BuildArray or BuildObjectKey depending on new stack.top
	 */
	void
	transition(InputAlphabet symbol, const JSONValue &value)
	{
		StackAlphabet top = stack.top();
		JSON_DEBUG("stacksz=%zu top=%d state=%d symbol=%d value=%s", stack.size(), static_cast<int>(top),
		           static_cast<int>(state), static_cast<int>(symbol), valueToString(value).c_str());

		// This is basically an if-defined transition function for a pushdown automata
		if (top == G::Base && state == Q::Empty && symbol == S::PushValue) {

			JSON_ASSERTF(node == nullptr, "Failed with %s", valueToString(value).c_str());
			cJSON *cjson_value = makeCJSONValue(value);
			node = make_shared<JSONNode>(cjson_value, true, nullptr);

			state = Q::Finish;

		} else if (top == G::Base && state == Q::Empty && symbol == S::StartObject) {

			JSON_ASSERTF(node == nullptr, "Failed with %s", valueToString(value).c_str());
			cJSON *cjson_object = cJSON_CreateObject();
			node = make_shared<JSONNode>(cjson_object, true, nullptr);

			state = Q::BuildObjectKey;
			stack.push(G::Object);

		} else if (top == G::Base && state == Q::Empty && symbol == S::StartArray) {

			JSON_ASSERTF(node == nullptr, "Failed with %s", valueToString(value).c_str());
			cJSON *cjson_array = cJSON_CreateArray();
			node = make_shared<JSONNode>(cjson_array, true, nullptr);

			state = Q::BuildArray;
			stack.push(G::Array);

		} else if (top == G::Array && state == Q::BuildArray && symbol == S::PushValue) {

			JSON_ASSERTF(node->cjson != nullptr, "Failed with %s", valueToString(value).c_str());
			cJSON *cjson_value = makeCJSONValue(value);
			cJSON_AddItemToArray(node->cjson, cjson_value);
			// node = node; // The current node does not change, it is still the array

			state = Q::BuildArray;

		} else if (top == G::Array && state == Q::BuildArray && symbol == S::StartArray) {

			JSON_ASSERTF(node->cjson != nullptr, "Failed with %s", valueToString(value).c_str());
			cJSON *cjson_array = cJSON_CreateArray();
			cJSON_AddItemToArray(node->cjson, cjson_array);
			node = make_shared<JSONNode>(cjson_array, false, node);

			state = Q::BuildArray;
			stack.push(G::Array);

		} else if (top == G::Array && state == Q::BuildArray && symbol == S::EndArray) {

			stack.pop();
			map<G, Q> m{{G::Object, Q::BuildObjectKey}, {G::Array, Q::BuildArray}, {G::Base, Q::Finish}};
			state = m[stack.top()];

			JSON_ASSERTF(node->cjson != nullptr, "Failed with %s", valueToString(value).c_str());
			if (node->parent) {
				node = node->parent;
			} else {
				JSON_ASSERTF(stack.top() == G::Base, "Unexpected non-root node without");
			}

		} else if (top == G::Array && state == Q::BuildArray && symbol == S::StartObject) {

			JSON_ASSERTF(node->cjson != nullptr, "Failed with %s", valueToString(value).c_str());
			cJSON *cjson_object = cJSON_CreateObject();
			cJSON_AddItemToArray(node->cjson, cjson_object);
			node = make_shared<JSONNode>(cjson_object, false, node);

			state = Q::BuildObjectKey;
			stack.push(G::Object);

		} else if (top == G::Object && state == Q::BuildObjectKey && symbol == S::PushKey) {

			JSON_ASSERTF(node->cjson != nullptr, "Failed with %s", valueToString(value).c_str());
			JSON_ASSERTF(holds_alternative<string>(value), "Non-string key not allowed");
			cJSON *cjson_null = cJSON_CreateNull();
			cJSON_AddItemToObject(node->cjson, get<string>(value).c_str(), cjson_null);
			node = make_shared<JSONNode>(cjson_null, false, node);

			state = Q::BuildObjectValue;

		} else if (top == G::Object && state == Q::BuildObjectKey && symbol == S::EndObject) {

			stack.pop();
			map<G, Q> m{{G::Object, Q::BuildObjectKey}, {G::Array, Q::BuildArray}, {G::Base, Q::Finish}};
			state = m[stack.top()];

			JSON_ASSERTF(node->cjson != nullptr, "Failed with %s", valueToString(value).c_str());
			if (node->parent) {
				node = node->parent;
			} else {
				JSON_ASSERTF(stack.top() == G::Base, "Unexpected non-root node without")
			}

		} else if (top == G::Object && state == Q::BuildObjectValue && symbol == S::PushValue) {

			JSON_ASSERTF(node->cjson != nullptr, "Failed with %s", valueToString(value).c_str());
			JSON_ASSERTF(cJSON_IsNull(node->cjson), "Partial pair value is not null");
			cJSON *cjson_value = makeCJSONValue(value);
			cJSON_ReplaceItemInObject(node->parent->cjson, node->cjson->string, cjson_value);
			node->cjson = cjson_value;
			node = node->parent;

			state = Q::BuildObjectKey;

		} else if (top == G::Object && state == Q::BuildObjectValue && symbol == S::StartObject) {

			JSON_ASSERTF(node->cjson != nullptr, "Failed with %s", valueToString(value).c_str());
			JSON_ASSERTF(cJSON_IsNull(node->cjson), "Partial pair value is not null");
			cJSON *cjson_object = cJSON_CreateObject();
			cJSON_ReplaceItemInObject(node->parent->cjson, node->cjson->string, cjson_object);
			node->cjson = cjson_object;

			state = Q::BuildObjectKey;
			stack.push(G::Object);

		} else if (top == G::Object && state == Q::BuildObjectValue && symbol == S::StartArray) {

			JSON_ASSERTF(node->cjson != nullptr, "Failed with %s", valueToString(value).c_str());
			JSON_ASSERTF(cJSON_IsNull(node->cjson), "Partial pair value is not null");
			cJSON *cjson_array = cJSON_CreateArray();
			cJSON_ReplaceItemInObject(node->parent->cjson, node->cjson->string, cjson_array);
			node->cjson = cjson_array;

			state = Q::BuildArray;
			stack.push(G::Array);

		} else {

			JSON_ASSERTF(false, "Invalid construction transition: top=%d state=%d symbol=%d value=%s",
			             static_cast<int>(top), static_cast<int>(state), static_cast<int>(symbol),
			             valueToString(value).c_str());
			node = make_shared<JSONNode>();

			state = Q::Invalid;
		}

		JSON_DEBUG("After transition: node=%p parent=%p\n", (void *)node.get(),
		           (void *)(node ? node->parent.get() : nullptr));
	}

public:
	JSONBuilder() {}

	//! Receives "[", "]", "{", "}", or any of string, const char*, double, int,
	//! bool as inputs. Updates the JSONBuilder state with it, after finishing the
	//! JSON tree, obtain the result with @ref getBuiltNode.
	JSONBuilder &
	operator<<(const JSONValue &value)
	{
		bool is_string = holds_alternative<string>(value) || holds_alternative<const char *>(value);
		if (!is_string) {
			transition(S::PushValue, value);
			return *this;
		}

		string as_string = holds_alternative<string>(value) ? get<string>(value) : get<const char *>(value);
		if (as_string == "[") {
			transition(S::StartArray, as_string);
		} else if (as_string == "]") {
			transition(S::EndArray, as_string);
		} else if (as_string == "{") {
			transition(S::StartObject, as_string);
		} else if (as_string == "}") {
			transition(S::EndObject, as_string);
		} else if (state == Q::BuildObjectKey) {
			transition(S::PushKey, as_string);
		} else if (state == Q::BuildObjectValue) {
			transition(S::PushValue, as_string);
		} else {
			JSON_ASSERTF(false, "Invalid state=%d value=%s", static_cast<int>(state), as_string.c_str());
		}
		return *this;
	}

	//! Gets the built JSONNode or crash if the construction has not finished
	JSONNode::Ptr
	getBuiltNode()
	{
		JSON_ASSERTF(state == Q::Finish, "Trying to getBuiltNode but the construction has not ended");
		return node;
	}
};

} // namespace xrt::auxiliary::util::json
