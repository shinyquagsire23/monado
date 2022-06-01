// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Swapchain code for the sdl code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup sdl_test
 */

#include "sdl_internal.h"

#include "util/u_handles.h"
#include "ogl/ogl_helpers.h"


static int64_t
vk_format_to_gl(int64_t format)
{
	switch (format) {
	case 4 /*   VK_FORMAT_R5G6B5_UNORM_PACK16      */: return 0;       // GL_RGB565?
	case 23 /*  VK_FORMAT_R8G8B8_UNORM             */: return GL_RGB8; // Should not be used, colour precision.
	case 29 /*  VK_FORMAT_R8G8B8_SRGB              */: return GL_SRGB8;
	case 30 /*  VK_FORMAT_B8G8R8_UNORM             */: return 0;
	case 37 /*  VK_FORMAT_R8G8B8A8_UNORM           */: return GL_RGBA8; // Should not be used, colour precision.
	case 43 /*  VK_FORMAT_R8G8B8A8_SRGB            */: return GL_SRGB8_ALPHA8;
	case 44 /*  VK_FORMAT_B8G8R8A8_UNORM           */: return 0;
	case 50 /*  VK_FORMAT_B8G8R8A8_SRGB            */: return 0;
	case 64 /*  VK_FORMAT_A2B10G10R10_UNORM_PACK32 */: return GL_RGB10_A2;
	case 84 /*  VK_FORMAT_R16G16B16_UNORM          */: return GL_RGB16;
	case 90 /*  VK_FORMAT_R16G16B16_SFLOAT         */: return GL_RGB16F;
	case 91 /*  VK_FORMAT_R16G16B16A16_UNORM       */: return GL_RGBA16;
	case 97 /*  VK_FORMAT_R16G16B16A16_SFLOAT      */: return GL_RGBA16F;
	case 124 /* VK_FORMAT_D16_UNORM                */: return GL_DEPTH_COMPONENT16;
	case 125 /* VK_FORMAT_X8_D24_UNORM_PACK32      */: return 0; // GL_DEPTH_COMPONENT24?
	case 126 /* VK_FORMAT_D32_SFLOAT               */: return GL_DEPTH_COMPONENT32F;
	case 127 /* VK_FORMAT_S8_UINT                  */: return 0; // GL_STENCIL_INDEX8?
	case 129 /* VK_FORMAT_D24_UNORM_S8_UINT        */: return GL_DEPTH24_STENCIL8;
	case 130 /* VK_FORMAT_D32_SFLOAT_S8_UINT       */: return GL_DEPTH32F_STENCIL8;
	default: U_LOG_W("Cannot convert VK format %" PRIu64 " to GL format!", format); return 0;
	}
}

static void
post_init_setup(struct sdl_swapchain *ssc, struct sdl_program *sp, const struct xrt_swapchain_create_info *info)
{
	SP_DEBUG(sp, "CREATE");

	// Setup fields
	ssc->sp = sp;
	ssc->w = (int)info->width;
	ssc->h = (int)info->height;


	sdl_make_current(sp);

	GLuint binding_enum = 0;
	GLuint tex_target = 0;
	ogl_texture_target_for_swapchain_info(info, &tex_target, &binding_enum);

	uint32_t image_count = ssc->base.base.base.image_count;
	GLuint gl_format = vk_format_to_gl(info->format);

	glCreateTextures(tex_target, image_count, ssc->textures);
	CHECK_GL();
	glCreateMemoryObjectsEXT(image_count, ssc->memory);
	CHECK_GL();

	for (uint32_t i = 0; i < image_count; i++) {
		GLint dedicated = ssc->base.base.images[i].use_dedicated_allocation ? GL_TRUE : GL_FALSE;
		glMemoryObjectParameterivEXT(ssc->memory[i], GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
		CHECK_GL();

		// The below function consumes the handle, need to reference it.
		xrt_graphics_buffer_handle_t handle = u_graphics_buffer_ref(ssc->base.base.images[i].handle);

		glImportMemoryFdEXT(               //
		    ssc->memory[i],                //
		    ssc->base.base.images[i].size, //
		    GL_HANDLE_TYPE_OPAQUE_FD_EXT,  //
		    handle);                       //
		CHECK_GL();

		if (info->array_size == 1) {
			glTextureStorageMem2DEXT( //
			    ssc->textures[i],     //
			    info->mip_count,      //
			    gl_format,            //
			    info->width,          //
			    info->height,         //
			    ssc->memory[i],       //
			    0);                   //
		} else {
			glTextureStorageMem3DEXT( //
			    ssc->textures[i],     //
			    info->mip_count,      //
			    gl_format,            //
			    info->width,          //
			    info->height,         //
			    info->array_size,     //
			    ssc->memory[i],       //
			    0);                   //
		}
		CHECK_GL();
	}

	sdl_make_uncurrent(sp);
}

static void
really_destroy(struct comp_swapchain *sc)
{
	struct sdl_swapchain *ssc = (struct sdl_swapchain *)sc;
	struct sdl_program *sp = ssc->sp;

	SP_DEBUG(sp, "DESTROY");

	sdl_make_current(sp);

	uint32_t image_count = ssc->base.base.base.image_count;
	if (image_count > 0) {
		glDeleteTextures(image_count, ssc->textures);
		glDeleteMemoryObjectsEXT(image_count, ssc->memory);

		U_ZERO_ARRAY(ssc->textures);
		U_ZERO_ARRAY(ssc->memory);
	}

	sdl_make_uncurrent(sp);

	// Teardown the base swapchain, freeing all Vulkan resources.
	comp_swapchain_teardown(sc);

	// Teardown does not free the struct itself.
	free(ssc);
}


/*
 *
 * 'Exported' functions.
 *
 */

xrt_result_t
sdl_swapchain_create(struct xrt_compositor *xc,
                     const struct xrt_swapchain_create_info *info,
                     struct xrt_swapchain **out_xsc)
{
	struct sdl_program *sp = from_comp(xc);
	xrt_result_t xret;

	/*
	 * In case the default get properties function have been overridden
	 * make sure to correctly dispatch the call to get the properties.
	 */
	struct xrt_swapchain_create_properties xsccp = {0};
	xrt_comp_get_swapchain_create_properties(xc, info, &xsccp);

	struct sdl_swapchain *ssc = U_TYPED_CALLOC(struct sdl_swapchain);

	xret = comp_swapchain_create_init( //
	    &ssc->base,                    //
	    really_destroy,                //
	    &sp->c.base.vk,                //
	    &sp->c.base.cscgc,             //
	    info,                          //
	    &xsccp);                       //
	if (xret != XRT_SUCCESS) {
		free(ssc);
		return xret;
	}

	// Init SDL fields and create OpenGL resources.
	post_init_setup(ssc, sp, info);

	// Correctly setup refcounts, init sets refcount to zero.
	xrt_swapchain_reference(out_xsc, &ssc->base.base.base);

	return xret;
}

xrt_result_t
sdl_swapchain_import(struct xrt_compositor *xc,
                     const struct xrt_swapchain_create_info *info,
                     struct xrt_image_native *native_images,
                     uint32_t native_image_count,
                     struct xrt_swapchain **out_xsc)
{
	struct sdl_program *sp = from_comp(xc);
	xrt_result_t xret;

	struct sdl_swapchain *ssc = U_TYPED_CALLOC(struct sdl_swapchain);

	xret = comp_swapchain_import_init( //
	    &ssc->base,                    //
	    really_destroy,                //
	    &sp->c.base.vk,                //
	    &sp->c.base.cscgc,             //
	    info,                          //
	    native_images,                 //
	    native_image_count);           //
	if (xret != XRT_SUCCESS) {
		free(ssc);
		return xret;
	}

	// Init SDL fields and create OpenGL resources.
	post_init_setup(ssc, sp, info);

	// Correctly setup refcounts, init sets refcount to zero.
	xrt_swapchain_reference(out_xsc, &ssc->base.base.base);

	return xret;
}
