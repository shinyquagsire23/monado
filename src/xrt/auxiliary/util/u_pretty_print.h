// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Pretty printing various Monado things.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_pretty
 */

#include "xrt/xrt_defines.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup aux_pretty Pretty printing functions and helpers
 * @ingroup aux_util
 *
 * This is common functionality used directly and shared by additional pretty
 * printing functions implemented in multiple modules, such as @ref oxr_api.
 *
 * Some functions have a `_indented` suffix added to them, this means that what
 * they print starts indented, but also they start with a newline. This is so
 * they can easily be chained together to form a debug message printing out
 * various information. Most of the final logging functions in Monado inserts a
 * newline at the end of the message and we don't want two to be inserted.
 */

/*!
 * Function prototype for receiving pretty printed strings.
 *
 * @note Do not keep a reference to the pointer as it's often allocated on the
 * stack for speed.
 *
 * @ingroup aux_pretty
 */
typedef void (*u_pp_delegate_func_t)(void *ptr, const char *str, size_t length);

/*!
 * Helper struct to hold a function pointer and data pointer.
 *
 * @ingroup aux_pretty
 */
struct u_pp_delegate
{
	//! Userdata pointer, placed first to match D/Volt delegates.
	void *ptr;

	//! String receiving function.
	u_pp_delegate_func_t func;
};

/*!
 * Helper typedef for delegate struct, less typing.
 *
 * @ingroup aux_pretty
 */
typedef struct u_pp_delegate u_pp_delegate_t;

/*!
 * Formats a string and sends to the delegate.
 *
 * @ingroup aux_pretty
 */
void
u_pp(struct u_pp_delegate dg, const char *fmt, ...) XRT_PRINTF_FORMAT(2, 3);

/*!
 * Pretty prints the @ref xrt_input_name.
 *
 * @ingroup aux_pretty
 */
void
u_pp_xrt_input_name(struct u_pp_delegate dg, enum xrt_input_name name);

/*!
 * Pretty prints the @ref xrt_result_t.
 *
 * @ingroup aux_pretty
 */
void
u_pp_xrt_result(struct u_pp_delegate dg, xrt_result_t xret);


/*
 *
 * Math struct printers.
 *
 */

/*!
 * Printers for math structs. None of these functions inserts trailing newlines
 * because it's hard to remove a trailing newline but easy to add one if one
 * should be needed. The small functions do not insert a starting newline while
 * the other functions does. This is so that you can easily chain print
 * functions to print a struct.
 *
 * @note xrt_matrix_* parameters assumed to be column major.
 *
 * @ingroup aux_pretty
 * @{
 */
void
u_pp_small_vec3(u_pp_delegate_t dg, const struct xrt_vec3 *vec);

void
u_pp_small_pose(u_pp_delegate_t dg, const struct xrt_pose *pose);

void
u_pp_small_matrix_3x3(u_pp_delegate_t dg, const struct xrt_matrix_3x3 *m);

void
u_pp_small_matrix_4x4(u_pp_delegate_t dg, const struct xrt_matrix_4x4 *m);

void
u_pp_small_matrix_4x4_f64(u_pp_delegate_t dg, const struct xrt_matrix_4x4_f64 *m);

void
u_pp_small_array_f64(struct u_pp_delegate dg, const double *arr, size_t n);

void
u_pp_small_array2d_f64(struct u_pp_delegate dg, const double *arr, size_t n, size_t m);

void
u_pp_vec3(u_pp_delegate_t dg, const struct xrt_vec3 *vec, const char *name, const char *indent);

void
u_pp_pose(u_pp_delegate_t dg, const struct xrt_pose *pose, const char *name, const char *indent);

void
u_pp_matrix_3x3(u_pp_delegate_t dg, const struct xrt_matrix_3x3 *m, const char *name, const char *indent);

void
u_pp_matrix_4x4(u_pp_delegate_t dg, const struct xrt_matrix_4x4 *m, const char *name, const char *indent);

void
u_pp_matrix_4x4_f64(u_pp_delegate_t dg, const struct xrt_matrix_4x4_f64 *m, const char *name, const char *indent);

//! Pretty prints `double arr[n]`
void
u_pp_array_f64(u_pp_delegate_t dg, const double *arr, size_t n, const char *name, const char *indent);

//! Pretty prints `double arr[n][m]`
void
u_pp_array2d_f64(u_pp_delegate_t dg, const double *arr, size_t n, size_t m, const char *name, const char *indent);

/*!
 * @}
 */


/*
 *
 * Sinks.
 *
 */

/*!
 * Stack only pretty printer sink, no need to free, must be inited before use.
 *
 * @ingroup aux_pretty
 */
struct u_pp_sink_stack_only
{
	//! How much of the buffer is used.
	size_t used;

	//! Storage for the sink.
	char buffer[1024 * 8];
};

u_pp_delegate_t
u_pp_sink_stack_only_init(struct u_pp_sink_stack_only *sink);


#ifdef __cplusplus
}
#endif
