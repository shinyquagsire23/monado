// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Variable tracking code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_var.h"
#include "util/u_time.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"

#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>


/*
 *
 * Enums, Classes and Defines.
 *
 */


class Var
{
public:
	std::string name;
	u_var_kind kind;
	union {
		void *ptr;
		struct xrt_colour_rgb_u8 *rgb_u8;
		struct xrt_colour_rgb_f32 *rgb_f32;
		bool *boolean;
	};
};

class Obj
{
public:
	std::string name = {};
	std::vector<Var> vars = {};
};

class Tracker
{
public:
	std::unordered_map<std::string, size_t> counters = {};
	std::unordered_map<ptrdiff_t, Obj> map = {};
	bool on;
	bool tested;

public:
	int
	getNumber(std::string name)
	{
		auto s = counters.find(name);
		int count = (s != counters.end() ? s->second : 0) + 1;
		counters[name] = count;

		return count;
	}
};

static class Tracker tracker;


/*
 *
 * Helper functions.
 *
 */

static bool
get_on()
{
	if (tracker.tested) {
		return tracker.on;
	}
	tracker.on = debug_get_bool_option("XRT_TRACK_VARIABLES", false);
	tracker.tested = true;

	return tracker.on;
}

static void
add_var(void *root, void *ptr, u_var_kind kind, const char *c_name)
{
	auto s = tracker.map.find((ptrdiff_t)root);
	if (s == tracker.map.end()) {
		return;
	}

	Var var;
	var.name = std::string(c_name);
	var.kind = kind;
	var.ptr = ptr;

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
	tracker.on = true;
	tracker.tested = true;
}

extern "C" void
u_var_add_root(void *root, const char *c_name, bool number)
{
	if (!get_on()) {
		return;
	}

	auto name = std::string(c_name);

	if (number) {
		int count = tracker.getNumber(name);

		std::stringstream ss;
		ss << name << " #" << count;
		name = ss.str();
	}

	auto &obj = tracker.map[(ptrdiff_t)root] = Obj();
	obj.name = name;
}

extern "C" void
u_var_remove_root(void *root)
{
	if (!get_on()) {
		return;
	}

	auto s = tracker.map.find((ptrdiff_t)root);
	if (s == tracker.map.end()) {
		return;
	}

	tracker.map.erase(s);
}

extern "C" void
u_var_visit(u_var_root_cb enter,
            u_var_root_cb exit,
            u_var_elm_cb elem,
            void *priv)
{
	if (!get_on()) {
		return;
	}

	std::vector<Obj *> tmp;
	tmp.reserve(tracker.map.size());

	for (auto &n : tracker.map) {
		tmp.push_back(&n.second);
	}

	for (Obj *obj : tmp) {
		enter(obj->name.c_str(), priv);

		for (auto &var : obj->vars) {
			elem(var.name.c_str(), (u_var_kind)var.kind, var.ptr,
			     priv);
		}

		exit(obj->name.c_str(), priv);
	}
}

#define ADD_FUNC(SUFFIX, TYPE, ENUM)                                           \
	extern "C" void u_var_add_##SUFFIX(void *obj, TYPE *ptr,               \
	                                   const char *c_name)                 \
	{                                                                      \
		if (!get_on()) {                                               \
			return;                                                \
		}                                                              \
		add_var(obj, (void *)ptr, U_VAR_KIND_##ENUM, c_name);          \
	}

ADD_FUNC(bool, bool, BOOL);
ADD_FUNC(rgb_u8, struct xrt_colour_rgb_u8, RGB_U8);
ADD_FUNC(rgb_f32, struct xrt_colour_rgb_f32, RGB_F32);
ADD_FUNC(u8, uint8_t, U8);
ADD_FUNC(i32, int32_t, I32);
ADD_FUNC(f32, float, F32);
ADD_FUNC(vec3_i32, struct xrt_vec3_i32, VEC3_I32);
ADD_FUNC(vec3_f32, struct xrt_vec3, VEC3_F32);
ADD_FUNC(ro_text, const char, RO_TEXT);
ADD_FUNC(ro_i32, int32_t, RO_I32);
ADD_FUNC(ro_f32, float, RO_F32);
ADD_FUNC(ro_vec3_i32, struct xrt_vec3_i32, RO_VEC3_I32);
ADD_FUNC(ro_vec3_f32, struct xrt_vec3, RO_VEC3_F32);
ADD_FUNC(ro_quat_f32, struct xrt_quat, RO_QUAT_F32);
ADD_FUNC(gui_header, bool, GUI_HEADER);

#undef ADD_FUNC
