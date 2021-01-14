// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common OpenGL code.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_ogl
 */

#include "ogl_helpers.h"
#include "ogl_api.h"

void
ogl_texture_target_for_swapchain_info(const struct xrt_swapchain_create_info *info,
                                      uint32_t *out_tex_target,
                                      uint32_t *out_tex_param_name)
{
	// see reference:
	// https://android.googlesource.com/platform/cts/+/master/tests/tests/nativehardware/jni/AHardwareBufferGLTest.cpp#1261
	if (info->face_count == 6) {
		if (info->array_size > 1) {
			*out_tex_target = GL_TEXTURE_CUBE_MAP_ARRAY;
			*out_tex_param_name = GL_TEXTURE_BINDING_CUBE_MAP_ARRAY;
			return;
		}
		*out_tex_target = GL_TEXTURE_CUBE_MAP;
		*out_tex_param_name = GL_TEXTURE_BINDING_CUBE_MAP;
		return;
	}
	// Note: on Android, some sources say always use
	// GL_TEXTURE_EXTERNAL_OES, but AHardwareBufferGLTest only uses it for
	// YUV buffers.
	//! @todo test GL_TEXTURE_EXTERNAL_OES on Android
	if (info->array_size > 1) {
		*out_tex_target = GL_TEXTURE_2D_ARRAY;
		*out_tex_param_name = GL_TEXTURE_BINDING_2D_ARRAY;
		return;
	}
	*out_tex_target = GL_TEXTURE_2D;
	*out_tex_param_name = GL_TEXTURE_BINDING_2D;
}
