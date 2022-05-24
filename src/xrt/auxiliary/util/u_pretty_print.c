// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Pretty printing various Monado things.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_pretty
 */

#include "util/u_misc.h"
#include "util/u_pretty_print.h"

#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>


/*
 *
 * Internal helpers.
 *
 */

#define DG(str) (dg.func(dg.ptr, str, strlen(str)))

const char *
get_xrt_input_type_short_str(enum xrt_input_type type)
{
	switch (type) {
	case XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE: return "VEC1_ZERO_TO_ONE";
	case XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE: return "VEC1_MINUS_ONE_TO_ONE";
	case XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE: return "VEC2_MINUS_ONE_TO_ONE";
	case XRT_INPUT_TYPE_VEC3_MINUS_ONE_TO_ONE: return "VEC3_MINUS_ONE_TO_ONE";
	case XRT_INPUT_TYPE_BOOLEAN: return "BOOLEAN";
	case XRT_INPUT_TYPE_POSE: return "POSE";
	case XRT_INPUT_TYPE_HAND_TRACKING: return "HAND_TRACKING";
	default: return "<UNKNOWN>";
	}
}

void
stack_only_sink(void *ptr, const char *str, size_t length)
{
	struct u_pp_sink_stack_only *sink = (struct u_pp_sink_stack_only *)ptr;

	size_t used = sink->used;
	size_t left = ARRAY_SIZE(sink->buffer) - used;
	if (left == 0) {
		return;
	}

	if (length >= left) {
		length = left - 1;
	}

	memcpy(sink->buffer + used, str, length);

	used += length;

	// Null terminate and update used.
	sink->buffer[used] = '\0';
	sink->used = used;
}


/*
 *
 * 'Exported' functions.
 *
 */

void
u_pp(struct u_pp_delegate dg, const char *fmt, ...)
{
	// Should be plenty enough for most prints.
	char tmp[1024];
	char *dst = tmp;
	va_list args;

	va_start(args, fmt);
	int ret = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	if (ret <= 0) {
		return;
	}

	size_t size = (size_t)ret;
	// Safe to do because MAX_INT should be less then MAX_SIZE_T
	size_t size_with_null = size + 1;

	if (size_with_null > ARRAY_SIZE(tmp)) {
		dst = U_TYPED_ARRAY_CALLOC(char, size_with_null);
	}

	va_start(args, fmt);
	ret = vsnprintf(dst, size_with_null, fmt, args);
	va_end(args);

	dg.func(dg.ptr, dst, size);

	if (tmp != dst) {
		free(dst);
	}
}

void
u_pp_xrt_input_name(struct u_pp_delegate dg, enum xrt_input_name name)
{
	switch (name) {
	case XRT_INPUT_GENERIC_HEAD_POSE: DG("XRT_INPUT_GENERIC_HEAD_POSE"); return;
	case XRT_INPUT_GENERIC_HEAD_DETECT: DG("XRT_INPUT_GENERIC_HEAD_DETECT"); return;
	case XRT_INPUT_GENERIC_HAND_TRACKING_LEFT: DG("XRT_INPUT_GENERIC_HAND_TRACKING_LEFT"); return;
	case XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT: DG("XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT"); return;
	case XRT_INPUT_GENERIC_TRACKER_POSE: DG("XRT_INPUT_GENERIC_TRACKER_POSE"); return;
	case XRT_INPUT_SIMPLE_SELECT_CLICK: DG("XRT_INPUT_SIMPLE_SELECT_CLICK"); return;
	case XRT_INPUT_SIMPLE_MENU_CLICK: DG("XRT_INPUT_SIMPLE_MENU_CLICK"); return;
	case XRT_INPUT_SIMPLE_GRIP_POSE: DG("XRT_INPUT_SIMPLE_GRIP_POSE"); return;
	case XRT_INPUT_SIMPLE_AIM_POSE: DG("XRT_INPUT_SIMPLE_AIM_POSE"); return;
	case XRT_INPUT_PSMV_PS_CLICK: DG("XRT_INPUT_PSMV_PS_CLICK"); return;
	case XRT_INPUT_PSMV_MOVE_CLICK: DG("XRT_INPUT_PSMV_MOVE_CLICK"); return;
	case XRT_INPUT_PSMV_START_CLICK: DG("XRT_INPUT_PSMV_START_CLICK"); return;
	case XRT_INPUT_PSMV_SELECT_CLICK: DG("XRT_INPUT_PSMV_SELECT_CLICK"); return;
	case XRT_INPUT_PSMV_SQUARE_CLICK: DG("XRT_INPUT_PSMV_SQUARE_CLICK"); return;
	case XRT_INPUT_PSMV_CROSS_CLICK: DG("XRT_INPUT_PSMV_CROSS_CLICK"); return;
	case XRT_INPUT_PSMV_CIRCLE_CLICK: DG("XRT_INPUT_PSMV_CIRCLE_CLICK"); return;
	case XRT_INPUT_PSMV_TRIANGLE_CLICK: DG("XRT_INPUT_PSMV_TRIANGLE_CLICK"); return;
	case XRT_INPUT_PSMV_TRIGGER_VALUE: DG("XRT_INPUT_PSMV_TRIGGER_VALUE"); return;
	case XRT_INPUT_PSMV_GRIP_POSE: DG("XRT_INPUT_PSMV_GRIP_POSE"); return;
	case XRT_INPUT_PSMV_AIM_POSE: DG("XRT_INPUT_PSMV_AIM_POSE"); return;
	case XRT_INPUT_PSMV_BODY_CENTER_POSE: DG("XRT_INPUT_PSMV_BODY_CENTER_POSE"); return;
	case XRT_INPUT_PSMV_BALL_CENTER_POSE: DG("XRT_INPUT_PSMV_BALL_CENTER_POSE"); return;
	case XRT_INPUT_HYDRA_1_CLICK: DG("XRT_INPUT_HYDRA_1_CLICK"); return;
	case XRT_INPUT_HYDRA_2_CLICK: DG("XRT_INPUT_HYDRA_2_CLICK"); return;
	case XRT_INPUT_HYDRA_3_CLICK: DG("XRT_INPUT_HYDRA_3_CLICK"); return;
	case XRT_INPUT_HYDRA_4_CLICK: DG("XRT_INPUT_HYDRA_4_CLICK"); return;
	case XRT_INPUT_HYDRA_MIDDLE_CLICK: DG("XRT_INPUT_HYDRA_MIDDLE_CLICK"); return;
	case XRT_INPUT_HYDRA_BUMPER_CLICK: DG("XRT_INPUT_HYDRA_BUMPER_CLICK"); return;
	case XRT_INPUT_HYDRA_JOYSTICK_CLICK: DG("XRT_INPUT_HYDRA_JOYSTICK_CLICK"); return;
	case XRT_INPUT_HYDRA_JOYSTICK_VALUE: DG("XRT_INPUT_HYDRA_JOYSTICK_VALUE"); return;
	case XRT_INPUT_HYDRA_TRIGGER_VALUE: DG("XRT_INPUT_HYDRA_TRIGGER_VALUE"); return;
	case XRT_INPUT_HYDRA_POSE: DG("XRT_INPUT_HYDRA_POSE"); return;
	case XRT_INPUT_DAYDREAM_TOUCHPAD_CLICK: DG("XRT_INPUT_DAYDREAM_TOUCHPAD_CLICK"); return;
	case XRT_INPUT_DAYDREAM_BAR_CLICK: DG("XRT_INPUT_DAYDREAM_BAR_CLICK"); return;
	case XRT_INPUT_DAYDREAM_CIRCLE_CLICK: DG("XRT_INPUT_DAYDREAM_CIRCLE_CLICK"); return;
	case XRT_INPUT_DAYDREAM_VOLUP_CLICK: DG("XRT_INPUT_DAYDREAM_VOLUP_CLICK"); return;
	case XRT_INPUT_DAYDREAM_VOLDN_CLICK: DG("XRT_INPUT_DAYDREAM_VOLDN_CLICK"); return;
	case XRT_INPUT_DAYDREAM_TOUCHPAD: DG("XRT_INPUT_DAYDREAM_TOUCHPAD"); return;
	case XRT_INPUT_DAYDREAM_POSE: DG("XRT_INPUT_DAYDREAM_POSE"); return;
	case XRT_INPUT_DAYDREAM_TOUCHPAD_TOUCH: DG("XRT_INPUT_DAYDREAM_TOUCHPAD_TOUCH"); return;
	case XRT_INPUT_INDEX_SYSTEM_CLICK: DG("XRT_INPUT_INDEX_SYSTEM_CLICK"); return;
	case XRT_INPUT_INDEX_SYSTEM_TOUCH: DG("XRT_INPUT_INDEX_SYSTEM_TOUCH"); return;
	case XRT_INPUT_INDEX_A_CLICK: DG("XRT_INPUT_INDEX_A_CLICK"); return;
	case XRT_INPUT_INDEX_A_TOUCH: DG("XRT_INPUT_INDEX_A_TOUCH"); return;
	case XRT_INPUT_INDEX_B_CLICK: DG("XRT_INPUT_INDEX_B_CLICK"); return;
	case XRT_INPUT_INDEX_B_TOUCH: DG("XRT_INPUT_INDEX_B_TOUCH"); return;
	case XRT_INPUT_INDEX_SQUEEZE_VALUE: DG("XRT_INPUT_INDEX_SQUEEZE_VALUE"); return;
	case XRT_INPUT_INDEX_SQUEEZE_FORCE: DG("XRT_INPUT_INDEX_SQUEEZE_FORCE"); return;
	case XRT_INPUT_INDEX_TRIGGER_CLICK: DG("XRT_INPUT_INDEX_TRIGGER_CLICK"); return;
	case XRT_INPUT_INDEX_TRIGGER_VALUE: DG("XRT_INPUT_INDEX_TRIGGER_VALUE"); return;
	case XRT_INPUT_INDEX_TRIGGER_TOUCH: DG("XRT_INPUT_INDEX_TRIGGER_TOUCH"); return;
	case XRT_INPUT_INDEX_THUMBSTICK: DG("XRT_INPUT_INDEX_THUMBSTICK"); return;
	case XRT_INPUT_INDEX_THUMBSTICK_CLICK: DG("XRT_INPUT_INDEX_THUMBSTICK_CLICK"); return;
	case XRT_INPUT_INDEX_THUMBSTICK_TOUCH: DG("XRT_INPUT_INDEX_THUMBSTICK_TOUCH"); return;
	case XRT_INPUT_INDEX_TRACKPAD: DG("XRT_INPUT_INDEX_TRACKPAD"); return;
	case XRT_INPUT_INDEX_TRACKPAD_FORCE: DG("XRT_INPUT_INDEX_TRACKPAD_FORCE"); return;
	case XRT_INPUT_INDEX_TRACKPAD_TOUCH: DG("XRT_INPUT_INDEX_TRACKPAD_TOUCH"); return;
	case XRT_INPUT_INDEX_GRIP_POSE: DG("XRT_INPUT_INDEX_GRIP_POSE"); return;
	case XRT_INPUT_INDEX_AIM_POSE: DG("XRT_INPUT_INDEX_AIM_POSE"); return;
	case XRT_INPUT_VIVE_SYSTEM_CLICK: DG("XRT_INPUT_VIVE_SYSTEM_CLICK"); return;
	case XRT_INPUT_VIVE_SQUEEZE_CLICK: DG("XRT_INPUT_VIVE_SQUEEZE_CLICK"); return;
	case XRT_INPUT_VIVE_MENU_CLICK: DG("XRT_INPUT_VIVE_MENU_CLICK"); return;
	case XRT_INPUT_VIVE_TRIGGER_CLICK: DG("XRT_INPUT_VIVE_TRIGGER_CLICK"); return;
	case XRT_INPUT_VIVE_TRIGGER_VALUE: DG("XRT_INPUT_VIVE_TRIGGER_VALUE"); return;
	case XRT_INPUT_VIVE_TRACKPAD: DG("XRT_INPUT_VIVE_TRACKPAD"); return;
	case XRT_INPUT_VIVE_TRACKPAD_CLICK: DG("XRT_INPUT_VIVE_TRACKPAD_CLICK"); return;
	case XRT_INPUT_VIVE_TRACKPAD_TOUCH: DG("XRT_INPUT_VIVE_TRACKPAD_TOUCH"); return;
	case XRT_INPUT_VIVE_GRIP_POSE: DG("XRT_INPUT_VIVE_GRIP_POSE"); return;
	case XRT_INPUT_VIVE_AIM_POSE: DG("XRT_INPUT_VIVE_AIM_POSE"); return;
	case XRT_INPUT_VIVEPRO_SYSTEM_CLICK: DG("XRT_INPUT_VIVEPRO_SYSTEM_CLICK"); return;
	case XRT_INPUT_VIVEPRO_VOLUP_CLICK: DG("XRT_INPUT_VIVEPRO_VOLUP_CLICK"); return;
	case XRT_INPUT_VIVEPRO_VOLDN_CLICK: DG("XRT_INPUT_VIVEPRO_VOLDN_CLICK"); return;
	case XRT_INPUT_VIVEPRO_MUTE_MIC_CLICK: DG("XRT_INPUT_VIVEPRO_MUTE_MIC_CLICK"); return;
	case XRT_INPUT_WMR_MENU_CLICK: DG("XRT_INPUT_WMR_MENU_CLICK"); return;
	case XRT_INPUT_WMR_SQUEEZE_CLICK: DG("XRT_INPUT_WMR_SQUEEZE_CLICK"); return;
	case XRT_INPUT_WMR_TRIGGER_VALUE: DG("XRT_INPUT_WMR_TRIGGER_VALUE"); return;
	case XRT_INPUT_WMR_THUMBSTICK_CLICK: DG("XRT_INPUT_WMR_THUMBSTICK_CLICK"); return;
	case XRT_INPUT_WMR_THUMBSTICK: DG("XRT_INPUT_WMR_THUMBSTICK"); return;
	case XRT_INPUT_WMR_TRACKPAD_CLICK: DG("XRT_INPUT_WMR_TRACKPAD_CLICK"); return;
	case XRT_INPUT_WMR_TRACKPAD_TOUCH: DG("XRT_INPUT_WMR_TRACKPAD_TOUCH"); return;
	case XRT_INPUT_WMR_TRACKPAD: DG("XRT_INPUT_WMR_TRACKPAD"); return;
	case XRT_INPUT_WMR_GRIP_POSE: DG("XRT_INPUT_WMR_GRIP_POSE"); return;
	case XRT_INPUT_WMR_AIM_POSE: DG("XRT_INPUT_WMR_AIM_POSE"); return;
	case XRT_INPUT_XBOX_MENU_CLICK: DG("XRT_INPUT_XBOX_MENU_CLICK"); return;
	case XRT_INPUT_XBOX_VIEW_CLICK: DG("XRT_INPUT_XBOX_VIEW_CLICK"); return;
	case XRT_INPUT_XBOX_A_CLICK: DG("XRT_INPUT_XBOX_A_CLICK"); return;
	case XRT_INPUT_XBOX_B_CLICK: DG("XRT_INPUT_XBOX_B_CLICK"); return;
	case XRT_INPUT_XBOX_X_CLICK: DG("XRT_INPUT_XBOX_X_CLICK"); return;
	case XRT_INPUT_XBOX_Y_CLICK: DG("XRT_INPUT_XBOX_Y_CLICK"); return;
	case XRT_INPUT_XBOX_DPAD_DOWN_CLICK: DG("XRT_INPUT_XBOX_DPAD_DOWN_CLICK"); return;
	case XRT_INPUT_XBOX_DPAD_RIGHT_CLICK: DG("XRT_INPUT_XBOX_DPAD_RIGHT_CLICK"); return;
	case XRT_INPUT_XBOX_DPAD_UP_CLICK: DG("XRT_INPUT_XBOX_DPAD_UP_CLICK"); return;
	case XRT_INPUT_XBOX_DPAD_LEFT_CLICK: DG("XRT_INPUT_XBOX_DPAD_LEFT_CLICK"); return;
	case XRT_INPUT_XBOX_SHOULDER_LEFT_CLICK: DG("XRT_INPUT_XBOX_SHOULDER_LEFT_CLICK"); return;
	case XRT_INPUT_XBOX_SHOULDER_RIGHT_CLICK: DG("XRT_INPUT_XBOX_SHOULDER_RIGHT_CLICK"); return;
	case XRT_INPUT_XBOX_THUMBSTICK_LEFT_CLICK: DG("XRT_INPUT_XBOX_THUMBSTICK_LEFT_CLICK"); return;
	case XRT_INPUT_XBOX_THUMBSTICK_LEFT: DG("XRT_INPUT_XBOX_THUMBSTICK_LEFT"); return;
	case XRT_INPUT_XBOX_THUMBSTICK_RIGHT_CLICK: DG("XRT_INPUT_XBOX_THUMBSTICK_RIGHT_CLICK"); return;
	case XRT_INPUT_XBOX_THUMBSTICK_RIGHT: DG("XRT_INPUT_XBOX_THUMBSTICK_RIGHT"); return;
	case XRT_INPUT_XBOX_LEFT_TRIGGER_VALUE: DG("XRT_INPUT_XBOX_LEFT_TRIGGER_VALUE"); return;
	case XRT_INPUT_XBOX_RIGHT_TRIGGER_VALUE: DG("XRT_INPUT_XBOX_RIGHT_TRIGGER_VALUE"); return;
	case XRT_INPUT_GO_SYSTEM_CLICK: DG("XRT_INPUT_GO_SYSTEM_CLICK"); return;
	case XRT_INPUT_GO_TRIGGER_CLICK: DG("XRT_INPUT_GO_TRIGGER_CLICK"); return;
	case XRT_INPUT_GO_BACK_CLICK: DG("XRT_INPUT_GO_BACK_CLICK"); return;
	case XRT_INPUT_GO_TRACKPAD_CLICK: DG("XRT_INPUT_GO_TRACKPAD_CLICK"); return;
	case XRT_INPUT_GO_TRACKPAD_TOUCH: DG("XRT_INPUT_GO_TRACKPAD_TOUCH"); return;
	case XRT_INPUT_GO_TRACKPAD: DG("XRT_INPUT_GO_TRACKPAD"); return;
	case XRT_INPUT_GO_GRIP_POSE: DG("XRT_INPUT_GO_GRIP_POSE"); return;
	case XRT_INPUT_GO_AIM_POSE: DG("XRT_INPUT_GO_AIM_POSE"); return;
	case XRT_INPUT_TOUCH_X_CLICK: DG("XRT_INPUT_TOUCH_X_CLICK"); return;
	case XRT_INPUT_TOUCH_X_TOUCH: DG("XRT_INPUT_TOUCH_X_TOUCH"); return;
	case XRT_INPUT_TOUCH_Y_CLICK: DG("XRT_INPUT_TOUCH_Y_CLICK"); return;
	case XRT_INPUT_TOUCH_Y_TOUCH: DG("XRT_INPUT_TOUCH_Y_TOUCH"); return;
	case XRT_INPUT_TOUCH_MENU_CLICK: DG("XRT_INPUT_TOUCH_MENU_CLICK"); return;
	case XRT_INPUT_TOUCH_A_CLICK: DG("XRT_INPUT_TOUCH_A_CLICK"); return;
	case XRT_INPUT_TOUCH_A_TOUCH: DG("XRT_INPUT_TOUCH_A_TOUCH"); return;
	case XRT_INPUT_TOUCH_B_CLICK: DG("XRT_INPUT_TOUCH_B_CLICK"); return;
	case XRT_INPUT_TOUCH_B_TOUCH: DG("XRT_INPUT_TOUCH_B_TOUCH"); return;
	case XRT_INPUT_TOUCH_SYSTEM_CLICK: DG("XRT_INPUT_TOUCH_SYSTEM_CLICK"); return;
	case XRT_INPUT_TOUCH_SQUEEZE_VALUE: DG("XRT_INPUT_TOUCH_SQUEEZE_VALUE"); return;
	case XRT_INPUT_TOUCH_TRIGGER_TOUCH: DG("XRT_INPUT_TOUCH_TRIGGER_TOUCH"); return;
	case XRT_INPUT_TOUCH_TRIGGER_VALUE: DG("XRT_INPUT_TOUCH_TRIGGER_VALUE"); return;
	case XRT_INPUT_TOUCH_THUMBSTICK_CLICK: DG("XRT_INPUT_TOUCH_THUMBSTICK_CLICK"); return;
	case XRT_INPUT_TOUCH_THUMBSTICK_TOUCH: DG("XRT_INPUT_TOUCH_THUMBSTICK_TOUCH"); return;
	case XRT_INPUT_TOUCH_THUMBSTICK: DG("XRT_INPUT_TOUCH_THUMBSTICK"); return;
	case XRT_INPUT_TOUCH_THUMBREST_TOUCH: DG("XRT_INPUT_TOUCH_THUMBREST_TOUCH"); return;
	case XRT_INPUT_TOUCH_GRIP_POSE: DG("XRT_INPUT_TOUCH_GRIP_POSE"); return;
	case XRT_INPUT_TOUCH_AIM_POSE: DG("XRT_INPUT_TOUCH_AIM_POSE"); return;
	case XRT_INPUT_HAND_SELECT_VALUE: DG("XRT_INPUT_HAND_SELECT_VALUE"); return;
	case XRT_INPUT_HAND_SQUEEZE_VALUE: DG("XRT_INPUT_HAND_SQUEEZE_VALUE"); return;
	case XRT_INPUT_HAND_GRIP_POSE: DG("XRT_INPUT_HAND_GRIP_POSE"); return;
	case XRT_INPUT_HAND_AIM_POSE: DG("XRT_INPUT_HAND_AIM_POSE"); return;
	default: break;
	}

	uint32_t id = XRT_GET_INPUT_ID(name);
	enum xrt_input_type type = XRT_GET_INPUT_TYPE(name);
	const char *str = get_xrt_input_type_short_str(type);

	u_pp(dg, "XRT_INPUT_0x%04x_%s", id, str);
}

void
u_pp_xrt_result(struct u_pp_delegate dg, xrt_result_t xret)
{
	switch (xret) {
	// clang-format off
	case XRT_SUCCESS:                                    DG("XRT_SUCCESS"); return;
	case XRT_TIMEOUT:                                    DG("XRT_TIMEOUT"); return;
	case XRT_ERROR_IPC_FAILURE:                          DG("XRT_ERROR_IPC_FAILURE"); return;
	case XRT_ERROR_NO_IMAGE_AVAILABLE:                   DG("XRT_ERROR_NO_IMAGE_AVAILABLE"); return;
	case XRT_ERROR_VULKAN:                               DG("XRT_ERROR_VULKAN"); return;
	case XRT_ERROR_OPENGL:                               DG("XRT_ERROR_OPENGL"); return;
	case XRT_ERROR_FAILED_TO_SUBMIT_VULKAN_COMMANDS:     DG("XRT_ERROR_FAILED_TO_SUBMIT_VULKAN_COMMANDS"); return;
	case XRT_ERROR_SWAPCHAIN_FLAG_VALID_BUT_UNSUPPORTED: DG("XRT_ERROR_SWAPCHAIN_FLAG_VALID_BUT_UNSUPPORTED"); return;
	case XRT_ERROR_ALLOCATION:                           DG("XRT_ERROR_ALLOCATION"); return;
	case XRT_ERROR_POSE_NOT_ACTIVE:                      DG("XRT_ERROR_POSE_NOT_ACTIVE"); return;
	case XRT_ERROR_FENCE_CREATE_FAILED:                  DG("XRT_ERROR_FENCE_CREATE_FAILED"); return;
	case XRT_ERROR_NATIVE_HANDLE_FENCE_ERROR:            DG("XRT_ERROR_NATIVE_HANDLE_FENCE_ERROR"); return;
	case XRT_ERROR_MULTI_SESSION_NOT_IMPLEMENTED:        DG("XRT_ERROR_MULTI_SESSION_NOT_IMPLEMENTED"); return;
	case XRT_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED:         DG("XRT_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED"); return;
	case XRT_ERROR_EGL_CONFIG_MISSING:                   DG("XRT_ERROR_EGL_CONFIG_MISSING"); return;
	case XRT_ERROR_THREADING_INIT_FAILURE:               DG("XRT_ERROR_THREADING_INIT_FAILURE"); return;
	case XRT_ERROR_IPC_SESSION_NOT_CREATED:              DG("XRT_ERROR_IPC_SESSION_NOT_CREATED"); return;
	case XRT_ERROR_IPC_SESSION_ALREADY_CREATED:          DG("XRT_ERROR_IPC_SESSION_ALREADY_CREATED"); return;
	case XRT_ERROR_PROBER_NOT_SUPPORTED:                 DG("XRT_ERROR_PROBER_NOT_SUPPORTED"); return;
	case XRT_ERROR_PROBER_CREATION_FAILED:               DG("XRT_ERROR_PROBER_CREATION_FAILED"); return;
	case XRT_ERROR_PROBER_LIST_LOCKED:                   DG("XRT_ERROR_PROBER_LIST_LOCKED"); return;
	case XRT_ERROR_PROBER_LIST_NOT_LOCKED:               DG("XRT_ERROR_PROBER_LIST_NOT_LOCKED"); return;
	case XRT_ERROR_PROBING_FAILED:                       DG("XRT_ERROR_PROBING_FAILED"); return;
	case XRT_ERROR_DEVICE_CREATION_FAILED:               DG("XRT_ERROR_DEVICE_CREATION_FAILED"); return;
	case XRT_ERROR_D3D:                                  DG("XRT_ERROR_D3D"); return;
	case XRT_ERROR_D3D11:                                DG("XRT_ERROR_D3D11"); return;
	case XRT_ERROR_D3D12:                                DG("XRT_ERROR_D3D12"); return;
	// clang-format on
	default: break;
	}

	if (xret < 0) {
		u_pp(dg, "XRT_ERROR_0x%08x", xret);
	} else {
		u_pp(dg, "XRT_SUCCESS_0x%08x", xret);
	}
}


/*
 *
 * Math structs printers.
 *
 */

void
u_pp_small_vec3(u_pp_delegate_t dg, const struct xrt_vec3 *vec)
{
	u_pp(dg, "[%f, %f, %f]", vec->x, vec->y, vec->z);
}

void
u_pp_small_pose(u_pp_delegate_t dg, const struct xrt_pose *pose)
{
	const struct xrt_vec3 *p = &pose->position;
	const struct xrt_quat *q = &pose->orientation;

	u_pp(dg, "[%f, %f, %f] [%f, %f, %f, %f]", p->x, p->y, p->z, q->x, q->y, q->z, q->w);
}

void
u_pp_small_matrix_3x3(u_pp_delegate_t dg, const struct xrt_matrix_3x3 *m)
{
	u_pp(dg,
	     "[\n"
	     "\t%f, %f, %f,\n"
	     "\t%f, %f, %f,\n"
	     "\t%f, %f, %f \n"
	     "]",
	     m->v[0], m->v[3], m->v[6],  //
	     m->v[1], m->v[4], m->v[7],  //
	     m->v[2], m->v[5], m->v[8]); //
}

void
u_pp_small_matrix_4x4(u_pp_delegate_t dg, const struct xrt_matrix_4x4 *m)
{
	u_pp(dg,
	     "[\n"
	     "\t%f, %f, %f, %f,\n"
	     "\t%f, %f, %f, %f,\n"
	     "\t%f, %f, %f, %f,\n"
	     "\t%f, %f, %f, %f\n"
	     "]",
	     m->v[0], m->v[4], m->v[8], m->v[12],   //
	     m->v[1], m->v[5], m->v[9], m->v[13],   //
	     m->v[2], m->v[6], m->v[10], m->v[14],  //
	     m->v[3], m->v[7], m->v[11], m->v[15]); //
}

void
u_pp_small_matrix_4x4_f64(u_pp_delegate_t dg, const struct xrt_matrix_4x4_f64 *m)
{
	u_pp(dg,
	     "[\n"
	     "\t%f, %f, %f, %f,\n"
	     "\t%f, %f, %f, %f,\n"
	     "\t%f, %f, %f, %f,\n"
	     "\t%f, %f, %f, %f\n"
	     "]",
	     m->v[0], m->v[4], m->v[8], m->v[12],   //
	     m->v[1], m->v[5], m->v[9], m->v[13],   //
	     m->v[2], m->v[6], m->v[10], m->v[14],  //
	     m->v[3], m->v[7], m->v[11], m->v[15]); //
}

void
u_pp_small_array_f64(struct u_pp_delegate dg, const double *arr, size_t n)
{
	assert(n != 0);
	DG("[");
	for (size_t i = 0; i < n - 1; i++) {
		u_pp(dg, "%lf, ", arr[i]);
	}
	u_pp(dg, "%lf]", arr[n - 1]);
}

void
u_pp_small_array2d_f64(struct u_pp_delegate dg, const double *arr, size_t n, size_t m)
{
	DG("[\n");
	for (size_t i = 0; i < n; i++) {
		u_pp_small_array_f64(dg, &arr[i], m);
	}
	DG("\n]");
}

void
u_pp_vec3(u_pp_delegate_t dg, const struct xrt_vec3 *vec, const char *name, const char *indent)
{
	u_pp(dg, "\n%s%s = ", indent, name);
	u_pp_small_vec3(dg, vec);
}

void
u_pp_pose(u_pp_delegate_t dg, const struct xrt_pose *pose, const char *name, const char *indent)
{
	u_pp(dg, "\n%s%s = ", indent, name);
	u_pp_small_pose(dg, pose);
}

void
u_pp_matrix_3x3(u_pp_delegate_t dg, const struct xrt_matrix_3x3 *m, const char *name, const char *indent)
{
	u_pp(dg,
	     "\n%s%s = ["
	     "\n%s\t%f, %f, %f,"
	     "\n%s\t%f, %f, %f,"
	     "\n%s\t%f, %f, %f"
	     "\n%s]",
	     indent, name,                      //
	     indent, m->v[0], m->v[3], m->v[6], //
	     indent, m->v[1], m->v[4], m->v[7], //
	     indent, m->v[2], m->v[5], m->v[8], //
	     indent);                           //
}

void
u_pp_matrix_4x4(u_pp_delegate_t dg, const struct xrt_matrix_4x4 *m, const char *name, const char *indent)
{
	u_pp(dg,
	     "\n%s%s = ["
	     "\n%s\t%f, %f, %f, %f,"
	     "\n%s\t%f, %f, %f, %f,"
	     "\n%s\t%f, %f, %f, %f,"
	     "\n%s\t%f, %f, %f, %f"
	     "\n%s]",
	     indent, name,                                 //
	     indent, m->v[0], m->v[4], m->v[8], m->v[12],  //
	     indent, m->v[1], m->v[5], m->v[9], m->v[13],  //
	     indent, m->v[2], m->v[6], m->v[10], m->v[14], //
	     indent, m->v[3], m->v[7], m->v[11], m->v[15], //
	     indent);                                      //
}

void
u_pp_matrix_4x4_f64(u_pp_delegate_t dg, const struct xrt_matrix_4x4_f64 *m, const char *name, const char *indent)
{
	u_pp(dg,
	     "\n%s%s = ["
	     "\n%s\t%f, %f, %f, %f,"
	     "\n%s\t%f, %f, %f, %f,"
	     "\n%s\t%f, %f, %f, %f,"
	     "\n%s\t%f, %f, %f, %f"
	     "\n%s]",
	     indent, name,                                 //
	     indent, m->v[0], m->v[4], m->v[8], m->v[12],  //
	     indent, m->v[1], m->v[5], m->v[9], m->v[13],  //
	     indent, m->v[2], m->v[6], m->v[10], m->v[14], //
	     indent, m->v[3], m->v[7], m->v[11], m->v[15], //
	     indent);                                      //
}

void
u_pp_array_f64(u_pp_delegate_t dg, const double *arr, size_t n, const char *name, const char *indent)
{
	u_pp(dg, "\n%s%s = ", indent, name);
	u_pp_small_array_f64(dg, arr, n);
}

void
u_pp_array2d_f64(u_pp_delegate_t dg, const double *arr, size_t n, size_t m, const char *name, const char *indent)
{
	u_pp(dg, "\n%s%s = ", indent, name);
	u_pp_small_array2d_f64(dg, arr, n, m);
}


/*
 *
 * Sink functions.
 *
 */

u_pp_delegate_t
u_pp_sink_stack_only_init(struct u_pp_sink_stack_only *sink)
{
	sink->used = 0;
	return (u_pp_delegate_t){sink, stack_only_sink};
}
