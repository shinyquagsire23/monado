// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  The NEW compositor rendering code header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_defines.h"

#include "vk/vk_helpers.h"


#ifdef __cplusplus
extern "C" {
#endif


struct comp_compositor;
struct comp_swapchain_image;

/*!
 * @addtogroup comp_main
 * @{
 */

/*
 *
 * Defines
 *
 */

//! How large in pixels the distortion image is.
#define COMP_DISTORTION_IMAGE_DIMENSIONS (128)

//! How many distortion images we have, one for each channel (3 rgb) and per view, total 6.
#define COMP_DISTORTION_NUM_IMAGES (6)


/*
 *
 * Buffer
 *
 */

/*!
 * Helper struct holding a buffer and it's memory.
 */
struct comp_buffer
{
	//! Backing memory.
	VkDeviceMemory memory;

	//! Buffer.
	VkBuffer buffer;

	//! Size of the buffer.
	VkDeviceSize size;

	//! Size of the memory allocation.
	VkDeviceSize allocation_size;

	//! Alignment of the buffer.
	VkDeviceSize alignment;

	void *mapped;
};

/*!
 * Initialize a buffer.
 */
VkResult
comp_buffer_init(struct vk_bundle *vk,
                 struct comp_buffer *buffer,
                 VkBufferUsageFlags usage_flags,
                 VkMemoryPropertyFlags memory_property_flags,
                 VkDeviceSize size);

/*!
 * Frees all resources that this buffer has, but does not free the buffer itself.
 */
void
comp_buffer_close(struct vk_bundle *vk, struct comp_buffer *buffer);

/*!
 * Maps the memory, sets comp_buffer::mapped to the memory.
 */
VkResult
comp_buffer_map(struct vk_bundle *vk, struct comp_buffer *buffer);

/*!
 * Unmaps the memory.
 */
void
comp_buffer_unmap(struct vk_bundle *vk, struct comp_buffer *buffer);

/*!
 * Maps the buffer, and copies the given data to the buffer.
 */
VkResult
comp_buffer_map_and_write(struct vk_bundle *vk, struct comp_buffer *buffer, void *data, VkDeviceSize size);

/*!
 * Writes the given data to the buffer, will map it temporarily if not mapped.
 */
VkResult
comp_buffer_write(struct vk_bundle *vk, struct comp_buffer *buffer, void *data, VkDeviceSize size);


/*
 *
 * Resources
 *
 */

/*!
 * Holds all pools and static resources for rendering.
 */
struct comp_resources
{
	/*
	 * Shared pools and caches.
	 */

	//! Shared for all rendering.
	VkPipelineCache pipeline_cache;

	//! Descriptor pool for mesh rendering.
	VkDescriptorPool mesh_descriptor_pool;


	/*
	 * Static
	 */

	struct
	{
		//! The binding index for the source texture.
		uint32_t src_binding;

		//! The binding index for the UBO.
		uint32_t ubo_binding;

		//! Descriptor set layout for mesh distortion.
		VkDescriptorSetLayout descriptor_set_layout;

		//! Pipeline layout used for mesh.
		VkPipelineLayout pipeline_layout;

		struct comp_buffer vbo;
		struct comp_buffer ibo;

		uint32_t num_vertices;
		uint32_t num_indices[2];
		uint32_t stride;
		uint32_t offset_indices[2];
		uint32_t total_num_indices;
	} mesh;

	struct
	{
		//! Descriptor pool for compute work.
		VkDescriptorPool descriptor_pool;

		//! The source projection view binding point.
		uint32_t src_binding;

		//! Image storing the distortion.
		uint32_t distortion_binding;

		//! Writing the image out too.
		uint32_t target_binding;

		//! Uniform data binding.
		uint32_t ubo_binding;

		//! Dummy sampler for null images.
		VkSampler default_sampler;

		//! Descriptor set layout for compute distortion.
		VkDescriptorSetLayout descriptor_set_layout;

		//! Pipeline layout used for compute distortion.
		VkPipelineLayout pipeline_layout;

		//! Doesn't depend on target so is static.
		VkPipeline clear_pipeline;

		//! Doesn't depend on target so is static.
		VkPipeline distortion_pipeline;

		//! Target info.
		struct comp_buffer ubo;
	} compute;

	struct
	{
		//! Backing memory to distortion images.
		VkDeviceMemory device_memories[COMP_DISTORTION_NUM_IMAGES];

		//! Distortion images.
		VkImage images[COMP_DISTORTION_NUM_IMAGES];

		//! The views into the distortion images.
		VkImageView image_views[COMP_DISTORTION_NUM_IMAGES];
	} distortion;
};

/*!
 * Allocate pools and static resources.
 *
 * @ingroup comp_main
 */
bool
comp_resources_init(struct comp_compositor *c, struct comp_resources *r);

/*!
 * Free all pools and static resources, does not free the struct itself.
 */
void
comp_resources_close(struct comp_compositor *c, struct comp_resources *r);


/*
 *
 * Rendering
 *
 */

/*!
 * Each rendering (@ref comp_rendering) render to one or more targets, each
 * target can have one or more views (@ref comp_rendering_view), this struct
 * holds all the data that is specific to the target.
 */
struct comp_target_data
{
	// The format that should be used to read from the target.
	VkFormat format;

	// Is this target a external target.
	bool is_external;

	//! Total height and width of the target.
	uint32_t width, height;
};

/*!
 * Each rendering (@ref comp_rendering) render to one or more targets, each
 * target can have one or more views (@ref comp_rendering_view), this struct
 * holds all the data that is specific to the target.
 */
struct comp_rendering_view
{
	struct
	{
		struct comp_buffer ubo;

		VkDescriptorSet descriptor_set;
	} mesh;
};

/*!
 * A rendering is used to create command buffers needed to do one frame of
 * compositor rendering, it holds onto resources used by the command buffer.
 */
struct comp_rendering
{
	struct comp_compositor *c;
	struct comp_resources *r;

	//! Command buffer where all commands are recorded.
	VkCommandBuffer cmd;

	//! Render pass used for rendering.
	VkRenderPass render_pass;

	struct
	{
		//! The data for this target.
		struct comp_target_data data;

		//! Framebuffer for this target.
		VkFramebuffer framebuffer;
	} targets[2];

	//! Number of different targets, number of views are always two.
	uint32_t num_targets;

	struct
	{
		//! Pipeline layout used for mesh.
		VkPipeline pipeline;
	} mesh;

	//! Holds per view data.
	struct comp_rendering_view views[2];

	//! The current view we are rendering to.
	uint32_t current_view;
};

/*!
 * Init struct and create resources needed for rendering.
 */
bool
comp_rendering_init(struct comp_compositor *c, struct comp_resources *r, struct comp_rendering *rr);

/*!
 * Frees all resources held by the rendering, does not free the struct itself.
 */
void
comp_rendering_close(struct comp_rendering *rr);


/*
 *
 * Drawing
 *
 */

/*!
 *  The pure data information about a view that the renderer is rendering to.
 */
struct comp_viewport_data
{
	uint32_t x, y;
	uint32_t w, h;
};

/*!
 * UBO data that is sent to the mesh shaders.
 */
struct comp_mesh_ubo_data
{
	struct xrt_matrix_2x2 vertex_rot;
};

/*!
 * This function allocates everything to start a single rendering. This is the
 * first function you call when you start rendering, you follow up with a call
 * to comp_draw_begin_view.
 */
bool
comp_draw_begin_target_single(struct comp_rendering *rr, VkImageView target, struct comp_target_data *data);

void
comp_draw_end_target(struct comp_rendering *rr);

void
comp_draw_begin_view(struct comp_rendering *rr,
                     uint32_t target,
                     uint32_t view,
                     struct comp_viewport_data *viewport_data);

void
comp_draw_end_view(struct comp_rendering *rr);

/*!
 * Does any needed teardown of state after all of the drawing commands have been
 * submitted.
 */
void
comp_draw_end_drawing(struct comp_resources *r);

void
comp_draw_projection_layer(struct comp_rendering *rr,
                           uint32_t layer,
                           VkSampler sampler,
                           VkImageView l_image_view,
                           VkImageView r_image_view,
                           struct xrt_layer_data *data);

void
comp_draw_quad_layer(
    struct comp_rendering *rr, uint32_t layer, VkSampler sampler, VkImageView image_view, struct xrt_layer_data *data);

void
comp_draw_cylinder_layer(
    struct comp_rendering *rr, uint32_t layer, VkSampler sampler, VkImageView image_view, struct xrt_layer_data *data);

void
comp_draw_distortion(struct comp_rendering *rr,
                     VkSampler sampler,
                     VkImageView image_view,
                     struct comp_mesh_ubo_data *data);


/*
 *
 * Compute distortion.
 *
 */

/*!
 * A compute rendering is used to create command buffers needed to do one frame
 * of compositor rendering using compute shaders, it holds onto resources used
 * by the command buffer.
 */
struct comp_rendering_compute
{
	struct comp_compositor *c;
	struct comp_resources *r;

	//! Command buffer where all commands are recorded.
	VkCommandBuffer cmd;

	//! Clear descriptor set.
	VkDescriptorSet clear_descriptor_set;

#if 0
	struct
	{
		//! The data for this target.
		struct comp_target_data data;

		//! Image view we are targeting, not owned by the rendering.
		VkImageView image_view;
	} targets[2];

	//! Number of different targets, number of views are always two.
	uint32_t num_targets;
#endif

	struct
	{
		int temp;
	} view;

	//! The current view we are "rendering" to.
	uint32_t current_view;
};

struct comp_rendering_compute_data
{
	struct
	{
		VkImageView source;

		VkImageView distortion;

		struct
		{
			uint32_t x;
			uint32_t y;
			uint32_t width;
			uint32_t height;
		} dst;
	} views[2];

	VkImageView target;
};

/*!
 * UBO data that is sent to the compute distortion shaders.
 */
struct comp_ubo_compute_data
{
	struct comp_viewport_data views[2];
	struct xrt_normalized_rect pre_transforms[2];
	struct xrt_normalized_rect post_transforms[2];
	struct xrt_matrix_4x4 transforms[2];
};

/*!
 * Init struct and create resources needed for compute rendering.
 */
bool
comp_rendering_compute_init(struct comp_compositor *c, struct comp_resources *r, struct comp_rendering_compute *crc);

/*!
 * Frees all resources held by the compute rendering, does not free the struct itself.
 */
void
comp_rendering_compute_close(struct comp_rendering_compute *crc);

bool
comp_rendering_compute_begin(struct comp_rendering_compute *crc);

void
comp_rendering_compute_projection(struct comp_rendering_compute *crc,            //
                                  VkSampler src_samplers[2],                     //
                                  VkImageView src_image_views[2],                //
                                  const struct xrt_normalized_rect src_rects[2], //
                                  VkImage target_image,                          //
                                  VkImageView target_image_view,                 //
                                  const struct comp_viewport_data views[2]);     //

void
comp_rendering_compute_clear(struct comp_rendering_compute *crc,        //
                             VkImage target_image,                      //
                             VkImageView target_image_view,             //
                             const struct comp_viewport_data views[2]); //

bool
comp_rendering_compute_end(struct comp_rendering_compute *crc);


/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
