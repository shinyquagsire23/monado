// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Variable tracking code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif


struct xrt_frame_sink;

/*!
 * @ingroup aux_util
 * @{
 */

struct u_var_f32_arr
{
	void *data;
	int *index_ptr;
	int length;
};

struct u_var_timing
{
	//! Values to be plotted.
	struct u_var_f32_arr values;

	//! A reference line drawn on the plot.
	float reference_timing;

	//! If false, reference_timing will be the bottom of the graph.
	bool center_reference_timing;

	//! How many units the graph expands by default.
	float range;

	//! Rescale graph's value range when value exceeds range.
	bool dynamic_rescale;

	//! A string describing the unit used, not freed.
	const char *unit;
};

/*!
 * What kind of variable is this tracking.
 */
enum u_var_kind
{
	U_VAR_KIND_BOOL,
	U_VAR_KIND_RGB_U8,
	U_VAR_KIND_RGB_F32,
	U_VAR_KIND_U8,
	U_VAR_KIND_I32,
	U_VAR_KIND_F32,
	U_VAR_KIND_F32_ARR,
	U_VAR_KIND_TIMING,
	U_VAR_KIND_VEC3_I32,
	U_VAR_KIND_VEC3_F32,
	U_VAR_KIND_POSE,
	U_VAR_KIND_SINK,
	U_VAR_KIND_RO_TEXT,
	U_VAR_KIND_RO_I32,
	U_VAR_KIND_RO_F32,
	U_VAR_KIND_RO_VEC3_I32,
	U_VAR_KIND_RO_VEC3_F32,
	U_VAR_KIND_RO_QUAT_F32,
	U_VAR_KIND_GUI_HEADER,
};

/*!
 * Callback for entering and leaving root nodes.
 */
typedef void (*u_var_root_cb)(const char *, void *);

/*!
 * Callback on each variable a root node has.
 */
typedef void (*u_var_elm_cb)(const char *, enum u_var_kind, void *, void *);

/*!
 * Add a named root object, the u_var subsystem is completely none-invasive
 * to the object it's tracking. The root pointer is used as a entry into a
 * hashmap of hidden objecrs. When not active all calls are stubs and have no
 * side-effects.
 *
 * This is intended only for debugging and is turned off by default, as this all
 * very very unsafe. It is just pointers straight into objects, completely
 * ignores ownership or any safe practices.
 *
 * If it's stupid, but it works, it ain't stupid.
 *
 * ```c
 * // On create
 * u_var_add_root((void*)psmv, "PS Move Controller", true);
 * u_var_add_rgb_u8((void*)psmv, &psmv->led_color, "LED");
 * u_var_add_bool((void*)psmv, &psmv->print_spew, "Spew");
 * u_var_add_bool((void*)psmv, &psmv->print_debug, "Debug");
 *
 * // On destroy, only need to destroy the root object.
 * u_var_remove_root((void*)psmv);
 * ```
 *
 * @ingroup aux_util
 */
void
u_var_add_root(void *root, const char *c_name, bool number);

/*!
 * Remove the root node.
 */
void
u_var_remove_root(void *root);

/*!
 * Visit all root nodes and their variables.
 */
void
u_var_visit(u_var_root_cb enter_cb,
            u_var_root_cb exit_cb,
            u_var_elm_cb elem_cb,
            void *priv);

/*!
 * This forces the variable tracking code to on, it is disabled by default.
 */
void
u_var_force_on(void);

#define U_VAR_ADD_FUNCS()                                                      \
	ADD_FUNC(bool, bool, BOOL)                                             \
	ADD_FUNC(rgb_u8, struct xrt_colour_rgb_u8, RGB_U8)                     \
	ADD_FUNC(rgb_f32, struct xrt_colour_rgb_f32, RGB_F32)                  \
	ADD_FUNC(u8, uint8_t, U8)                                              \
	ADD_FUNC(i32, int32_t, I32)                                            \
	ADD_FUNC(f32, float, F32)                                              \
	ADD_FUNC(f32_arr, struct u_var_f32_arr, F32_ARR)                       \
	ADD_FUNC(f32_timing, struct u_var_timing, TIMING)                      \
	ADD_FUNC(vec3_i32, struct xrt_vec3_i32, VEC3_I32)                      \
	ADD_FUNC(vec3_f32, struct xrt_vec3, VEC3_F32)                          \
	ADD_FUNC(pose, struct xrt_pose, POSE)                                  \
	ADD_FUNC(sink, struct xrt_frame_sink *, SINK)                          \
	ADD_FUNC(ro_text, const char, RO_TEXT)                                 \
	ADD_FUNC(ro_i32, int32_t, RO_I32)                                      \
	ADD_FUNC(ro_f32, float, RO_F32)                                        \
	ADD_FUNC(ro_vec3_i32, struct xrt_vec3_i32, RO_VEC3_I32)                \
	ADD_FUNC(ro_vec3_f32, struct xrt_vec3, RO_VEC3_F32)                    \
	ADD_FUNC(ro_quat_f32, struct xrt_quat, RO_QUAT_F32)                    \
	ADD_FUNC(gui_header, bool, GUI_HEADER)

#define ADD_FUNC(SUFFIX, TYPE, ENUM)                                           \
	void u_var_add_##SUFFIX(void *, TYPE *, const char *);

U_VAR_ADD_FUNCS()

#undef ADD_FUNC

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
