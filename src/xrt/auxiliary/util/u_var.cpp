// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Variable tracking code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_var.h"
#include "util/u_debug.h"

#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>


namespace xrt::auxiliary::util {


/*
 *
 * Enums, Classes and Defines.
 *
 */

/*!
 * Simple container for the variable information.
 */
class Var
{
public:
	struct u_var_info info = {};
};

/*!
 * Object that has a series of tracked variables.
 */
class Obj
{
public:
	std::string name = {};
	std::string raw_name = {};
	struct u_var_root_info info = {};
	std::vector<Var> vars = {};
};

/*!
 * Object that has a series of tracked variables.
 */
class Tracker
{
public:
	std::unordered_map<std::string, uint32_t> counters = {};
	std::unordered_map<ptrdiff_t, Obj> map = {};
	bool on = false;
	bool tested = false;

public:
	uint32_t
	getNumber(const std::string &name)
	{
		auto s = counters.find(name);
		uint32_t count = (s != counters.end() ? s->second : 0u) + 1u;
		counters[name] = count;

		return count;
	}
};

/*!
 * Global variable tracking state.
 */
static class Tracker gTracker;


/*
 *
 * Helper functions.
 *
 */

static bool
get_on()
{
	if (gTracker.tested) {
		return gTracker.on;
	}
	gTracker.on = debug_get_bool_option("XRT_TRACK_VARIABLES", false);
	gTracker.tested = true;

	return gTracker.on;
}

static void
add_var(void *root, void *ptr, u_var_kind kind, const char *c_name)
{
	auto s = gTracker.map.find((ptrdiff_t)root);
	if (s == gTracker.map.end()) {
		return;
	}

	Var var;
	snprintf(var.info.name, U_VAR_NAME_STRING_SIZE, "%s", c_name);
	var.info.kind = kind;
	var.info.ptr = ptr;

	s->second.vars.push_back(var);
}


/*
 *
 * Exported functions.
 *
 */

extern "C" void
u_var_force_on(void)
{
	gTracker.on = true;
	gTracker.tested = true;
}

extern "C" void
u_var_add_root(void *root, const char *c_name, bool suffix_with_number)
{
	if (!get_on()) {
		return;
	}

	auto name = std::string(c_name);
	auto raw_name = name;
	uint32_t count = 0; // Zero means no number.

	if (suffix_with_number) {
		count = gTracker.getNumber(name);

		std::stringstream ss;
		ss << name << " #" << count;
		name = ss.str();
	}

	auto &obj = gTracker.map[(ptrdiff_t)root] = Obj();
	obj.name = name;
	obj.raw_name = raw_name;
	obj.info.name = obj.name.c_str();
	obj.info.raw_name = obj.raw_name.c_str();
	obj.info.number = count;
}

extern "C" void
u_var_remove_root(void *root)
{
	if (!get_on()) {
		return;
	}

	auto s = gTracker.map.find((ptrdiff_t)root);
	if (s == gTracker.map.end()) {
		return;
	}

	gTracker.map.erase(s);
}

extern "C" void
u_var_visit(u_var_root_cb enter_cb, u_var_root_cb exit_cb, u_var_elm_cb elem_cb, void *priv)
{
	if (!get_on()) {
		return;
	}

	std::vector<Obj *> tmp;
	tmp.reserve(gTracker.map.size());

	for (auto &n : gTracker.map) {
		tmp.push_back(&n.second);
	}

	for (Obj *obj : tmp) {
		enter_cb(&obj->info, priv);

		for (auto &var : obj->vars) {
			elem_cb(&var.info, priv);
		}

		exit_cb(&obj->info, priv);
	}
}

#define ADD_FUNC(SUFFIX, TYPE, ENUM)                                                                                   \
	extern "C" void u_var_add_##SUFFIX(void *obj, TYPE *ptr, const char *c_name)                                   \
	{                                                                                                              \
		if (!get_on()) {                                                                                       \
			return;                                                                                        \
		}                                                                                                      \
		add_var(obj, (void *)ptr, U_VAR_KIND_##ENUM, c_name);                                                  \
	}

U_VAR_ADD_FUNCS()

#undef ADD_FUNC

} // namespace xrt::auxiliary::util
