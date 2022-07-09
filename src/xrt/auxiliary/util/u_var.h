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

#include "util/u_logging.h"


#ifdef __cplusplus
extern "C" {
#endif


struct xrt_frame_sink;
struct u_sink_debug;
struct m_ff_f64;
struct m_ff_vec3_f32;

/*!
 * @addtogroup aux_util
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
 * Callback for a button action
 */
typedef void (*u_var_button_cb)(void *);

struct u_var_button
{
	//! Callback function to execute on button press
	u_var_button_cb cb;

	//! Pointer that will be passed to the function as its only argument
	void *ptr;

	//! Button text, use var `name` if zeroed
	char label[64];

	//! Button dimensions, zero for auto size
	float width;
	float height;

	//! Whether this button is disabled
	bool disabled;
};

struct u_var_combo
{
	//! Number of options.
	int count;

	//! List of `count` option names separated by \0.
	const char *options;

	//! Pointer to the option value.
	int *value;
};

struct u_var_draggable_f32
{
	float val;
	float step;
	float min;
	float max;
};

struct u_var_draggable_u16
{
	//! @note Using a float instead of storing the value like @ref
	//! u_var_draggable_f32. It seemed better to decouple the UI from the value
	//! itself.
	//! @todo Unify "draggable" widgets interface.
	uint16_t *val;
	uint16_t step;
	uint16_t min;
	uint16_t max;
};

struct u_var_histogram_f32
{
	float *values; //!< Bin heights
	int count;     //!< Number of bins
	float width;   //!< Widget width or 0 for auto
	float height;  //!< Widget height or 0 for auto
};

struct u_var_curve_point
{
	// Using doubles like ImPlotPoint
	double x;
	double y;
};

typedef struct u_var_curve_point (*u_var_curve_getter)(void *data, int i);

struct u_var_curve
{
	u_var_curve_getter getter; //!< Getter of 2D points for the curve
	void *data;                //!< User data for `getter`
	int count;                 //!< Number of points to draw; param i < count
	const char *label;         //!< Curve name
	const char *xlabel;        //!< Label of the X axis
	const char *ylabel;        //!< Label of the Y axis
};

struct u_var_curves
{
	struct u_var_curve curves[16];
	int curve_count;

	// These override individual curve axis labels
	const char *xlabel; //!< Label of the X axis
	const char *ylabel; //!< Label of the Y axis
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
	U_VAR_KIND_U16,
	U_VAR_KIND_U64,
	U_VAR_KIND_I32,
	U_VAR_KIND_F32,
	U_VAR_KIND_DRAGGABLE_F32,
	U_VAR_KIND_F64,
	U_VAR_KIND_F32_ARR,
	U_VAR_KIND_TIMING,
	U_VAR_KIND_VEC3_I32,
	U_VAR_KIND_VEC3_F32,
	U_VAR_KIND_POSE,
	U_VAR_KIND_SINK_DEBUG,
	U_VAR_KIND_LOG_LEVEL,
	U_VAR_KIND_RO_TEXT,
	U_VAR_KIND_RO_FTEXT,
	U_VAR_KIND_RO_I32,
	U_VAR_KIND_RO_U32,
	U_VAR_KIND_RO_F32,
	U_VAR_KIND_RO_I64,
	U_VAR_KIND_RO_U64,
	U_VAR_KIND_RO_F64,
	U_VAR_KIND_RO_VEC3_I32,
	U_VAR_KIND_RO_VEC3_F32,
	U_VAR_KIND_RO_QUAT_F32,
	U_VAR_KIND_RO_FF_F64,
	U_VAR_KIND_RO_FF_VEC3_F32,
	U_VAR_KIND_GUI_HEADER,
	U_VAR_KIND_BUTTON,
	U_VAR_KIND_COMBO,
	U_VAR_KIND_HISTOGRAM_F32,
	U_VAR_KIND_DRAGGABLE_U16,
	U_VAR_KIND_CURVE,
	U_VAR_KIND_CURVES,
};

#define U_VAR_NAME_STRING_SIZE 256
/*!
 * Struct that keeps all of the information about the variable, some of the UI
 * state is kept on it.
 */
struct u_var_info
{
	char name[U_VAR_NAME_STRING_SIZE];
	void *ptr;

	enum u_var_kind kind;

	struct
	{
		bool graphed;
	} gui;
};

/*!
 * Callback for entering and leaving root nodes.
 */
typedef void (*u_var_root_cb)(const char *, void *);

/*!
 * Callback on each variable a root node has.
 */
typedef void (*u_var_elm_cb)(struct u_var_info *info, void *);

/*!
 * Add a named root object, the u_var subsystem is completely none-invasive
 * to the object it's tracking. The root pointer is used as a entry into a
 * hashmap of hidden objecrs. When not active all calls are stubs and have no
 * side-effects.
 *
 * This is intended only for debugging and is turned off by default, as this all
 * very very unsafe. It is only pointers straight into objects, completely
 * ignores ownership or any safe practices.
 *
 * ```c
 * // On create
 * u_var_add_root((void*)psmv, "PS Move Controller", true);
 * u_var_add_rgb_u8((void*)psmv, &psmv->led_color, "LED");
 * u_var_add_log_level(psmv, &psmv->log_level, "Log level");
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
u_var_visit(u_var_root_cb enter_cb, u_var_root_cb exit_cb, u_var_elm_cb elem_cb, void *priv);

/*!
 * This forces the variable tracking code to on, it is disabled by default.
 */
void
u_var_force_on(void);

#define U_VAR_ADD_FUNCS()                                                                                              \
	ADD_FUNC(bool, bool, BOOL)                                                                                     \
	ADD_FUNC(rgb_u8, struct xrt_colour_rgb_u8, RGB_U8)                                                             \
	ADD_FUNC(rgb_f32, struct xrt_colour_rgb_f32, RGB_F32)                                                          \
	ADD_FUNC(u8, uint8_t, U8)                                                                                      \
	ADD_FUNC(u16, uint16_t, U16)                                                                                   \
	ADD_FUNC(u64, uint64_t, U64)                                                                                   \
	ADD_FUNC(i32, int32_t, I32)                                                                                    \
	ADD_FUNC(f32, float, F32)                                                                                      \
	ADD_FUNC(f64, double, F64)                                                                                     \
	ADD_FUNC(f32_arr, struct u_var_f32_arr, F32_ARR)                                                               \
	ADD_FUNC(f32_timing, struct u_var_timing, TIMING)                                                              \
	ADD_FUNC(vec3_i32, struct xrt_vec3_i32, VEC3_I32)                                                              \
	ADD_FUNC(vec3_f32, struct xrt_vec3, VEC3_F32)                                                                  \
	ADD_FUNC(pose, struct xrt_pose, POSE)                                                                          \
	ADD_FUNC(sink_debug, struct u_sink_debug, SINK_DEBUG)                                                          \
	ADD_FUNC(log_level, enum u_logging_level, LOG_LEVEL)                                                           \
	ADD_FUNC(ro_text, const char, RO_TEXT)                                                                         \
	ADD_FUNC(ro_ftext, const char, RO_FTEXT)                                                                       \
	ADD_FUNC(ro_i32, int32_t, RO_I32)                                                                              \
	ADD_FUNC(ro_u32, uint32_t, RO_I32)                                                                             \
	ADD_FUNC(ro_f32, float, RO_F32)                                                                                \
	ADD_FUNC(ro_i64, int64_t, RO_I64)                                                                              \
	ADD_FUNC(ro_u64, uint64_t, RO_U64)                                                                             \
	ADD_FUNC(ro_f64, double, RO_F64)                                                                               \
	ADD_FUNC(ro_vec3_i32, struct xrt_vec3_i32, RO_VEC3_I32)                                                        \
	ADD_FUNC(ro_vec3_f32, struct xrt_vec3, RO_VEC3_F32)                                                            \
	ADD_FUNC(ro_quat_f32, struct xrt_quat, RO_QUAT_F32)                                                            \
	ADD_FUNC(ro_ff_f64, struct m_ff_f64, RO_FF_F64)                                                                \
	ADD_FUNC(ro_ff_vec3_f32, struct m_ff_vec3_f32, RO_FF_VEC3_F32)                                                 \
	ADD_FUNC(gui_header, bool, GUI_HEADER)                                                                         \
	ADD_FUNC(button, struct u_var_button, BUTTON)                                                                  \
	ADD_FUNC(combo, struct u_var_combo, COMBO)                                                                     \
	ADD_FUNC(draggable_f32, struct u_var_draggable_f32, DRAGGABLE_F32)                                             \
	ADD_FUNC(draggable_u16, struct u_var_draggable_u16, DRAGGABLE_U16)                                             \
	ADD_FUNC(histogram_f32, struct u_var_histogram_f32, HISTOGRAM_F32)                                             \
	ADD_FUNC(curve, struct u_var_curve, CURVE)                                                                     \
	ADD_FUNC(curves, struct u_var_curves, CURVES)

#define ADD_FUNC(SUFFIX, TYPE, ENUM) void u_var_add_##SUFFIX(void *, TYPE *, const char *);

U_VAR_ADD_FUNCS()

#undef ADD_FUNC

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
