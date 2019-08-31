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


/*!
 * @ingroup aux_util
 * @{
 */

/*!
 * What kind of variable is this tracking.
 */
enum u_var_kind
{
	U_VAR_KIND_BOOL,
	U_VAR_KIND_U8,
	U_VAR_KIND_U32,
	U_VAR_KIND_RGB_U8,
	U_VAR_KIND_RGB_F32,
	U_VAR_KIND_TEXT,
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
 * u_var_add_root((void*)psmv, "PS Move Controller", psmv_var_updated_callback);
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
u_var_add_root(void *, const char *, bool);

/*!
 * Remove the root node.
 */
void
u_var_remove_root(void *);

/*!
 * Visit all root nodes and their variables.
 */
void
u_var_visit(u_var_root_cb enter,
            u_var_root_cb exit,
            u_var_elm_cb e_cb,
            void *priv);

/*!
 * This forces the variable tracking code to on, it is disabled by default.
 */
void
u_var_force_on(void);

//! Add a variable to track on a root node, does not claim ownership.
void
u_var_add_rgb_u8(void *, struct xrt_colour_rgb_u8 *, const char *);

//! Add a variable to track on a root node, does not claim ownership.
void
u_var_add_rgb_f32(void *, struct xrt_colour_rgb_f32 *, const char *);

//! Add a variable to track on a root node, does not claim ownership.
void
u_var_add_u8(void *, uint8_t *, const char *);

//! Add a variable to track on a root node, does not claim ownership.
void
u_var_add_u32(void *, uint32_t *, const char *);

//! Add a variable to track on a root node, does not claim ownership.
void
u_var_add_bool(void *, bool *, const char *);

//! Add a variable to track on a root node, does not claim ownership.
void
u_var_add_text(void *, const char *, const char *);

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
