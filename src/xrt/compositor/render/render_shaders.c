// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shader loading code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_render
 */

#include "render/render_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wnewline-eof"
#endif

#include "xrt/xrt_config_build.h"

#include "shaders/clear.comp.h"
#include "shaders/distortion.comp.h"
#include "shaders/layer.frag.h"
#include "shaders/layer.vert.h"
#include "shaders/equirect1.frag.h"
#include "shaders/equirect1.vert.h"
#include "shaders/equirect2.frag.h"
#include "shaders/equirect2.vert.h"
#include "shaders/mesh.frag.h"
#include "shaders/mesh.vert.h"

#ifdef XRT_FEATURE_OPENXR_LAYER_CUBE
#include "shaders/cube.frag.h"
#include "shaders/cube.vert.h"
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif


/*
 *
 * Functions.
 *
 */

static VkResult
shader_load(struct vk_bundle *vk, const uint32_t *code, size_t size, VkShaderModule *out_module)
{
	VkResult ret;

	VkShaderModuleCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
	    .codeSize = size,
	    .pCode = code,
	};

	VkShaderModule module;
	ret = vk->vkCreateShaderModule(vk->device, //
	                               &info,      //
	                               NULL,       //
	                               &module);   //
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateShaderModule failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_module = module;

	return VK_SUCCESS;
}

#define C(c)                                                                                                           \
	do {                                                                                                           \
		VkResult ret = c;                                                                                      \
		if (ret != VK_SUCCESS) {                                                                               \
			render_shaders_close(s, vk);                                                                   \
			return false;                                                                                  \
		}                                                                                                      \
	} while (false)

bool
render_shaders_load(struct render_shaders *s, struct vk_bundle *vk)
{
	C(shader_load(vk,                         // vk_bundle
	              shaders_clear_comp,         // data
	              sizeof(shaders_clear_comp), // size
	              &s->clear_comp));           // out

	C(shader_load(vk,                              // vk_bundle
	              shaders_distortion_comp,         // data
	              sizeof(shaders_distortion_comp), // size
	              &s->distortion_comp));           // out

	C(shader_load(vk,                        // vk_bundle
	              shaders_mesh_vert,         // data
	              sizeof(shaders_mesh_vert), // size
	              &s->mesh_vert));           // out
	C(shader_load(vk,                        // vk_bundle
	              shaders_mesh_frag,         // data
	              sizeof(shaders_mesh_frag), // size
	              &s->mesh_frag));           // out

	C(shader_load(vk,                             // vk_bundle
	              shaders_equirect1_vert,         // data
	              sizeof(shaders_equirect1_vert), // size
	              &s->equirect1_vert));           // out
	C(shader_load(vk,                             // vk_bundle
	              shaders_equirect1_frag,         // data
	              sizeof(shaders_equirect1_frag), // size
	              &s->equirect1_frag));           // out

	C(shader_load(vk,                             // vk_bundle
	              shaders_equirect2_vert,         // data
	              sizeof(shaders_equirect2_vert), // size
	              &s->equirect2_vert));           // out
	C(shader_load(vk,                             // vk_bundle
	              shaders_equirect2_frag,         // data
	              sizeof(shaders_equirect2_frag), // size
	              &s->equirect2_frag));           // out

#ifdef XRT_FEATURE_OPENXR_LAYER_CUBE
	C(shader_load(vk,                        // vk_bundle
	              shaders_cube_vert,         // data
	              sizeof(shaders_cube_vert), // size
	              &s->cube_vert));           // out
	C(shader_load(vk,                        // vk_bundle
	              shaders_cube_frag,         // data
	              sizeof(shaders_cube_frag), // size
	              &s->cube_frag));           // out
#endif

	C(shader_load(vk,                         // vk_bundle
	              shaders_layer_vert,         // data
	              sizeof(shaders_layer_vert), // size
	              &s->layer_vert));           // out
	C(shader_load(vk,                         // vk_bundle
	              shaders_layer_frag,         // data
	              sizeof(shaders_layer_frag), // size
	              &s->layer_frag));           // out

	VK_DEBUG(vk, "Shaders loaded!");

	return true;
}

#define D(shader)                                                                                                      \
	if (s->shader != VK_NULL_HANDLE) {                                                                             \
		vk->vkDestroyShaderModule(vk->device, s->shader, NULL);                                                \
		s->shader = VK_NULL_HANDLE;                                                                            \
	}

void
render_shaders_close(struct render_shaders *s, struct vk_bundle *vk)
{
	D(clear_comp);
	D(distortion_comp);
	D(mesh_vert);
	D(mesh_frag);
	D(equirect1_vert);
	D(equirect1_frag);
	D(equirect2_vert);
	D(equirect2_frag);
#ifdef XRT_FEATURE_OPENXR_LAYER_CUBE
	D(cube_vert);
	D(cube_frag);
#endif
	D(layer_vert);
	D(layer_frag);

	VK_DEBUG(vk, "Shaders destroyed!");
}
