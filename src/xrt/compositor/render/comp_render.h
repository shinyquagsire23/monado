// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  The NEW compositor rendering code header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_render
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_defines.h"

#include "vk/vk_helpers.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup comp_render Compositor render code
 * @ingroup comp
 *
 * @brief Rendering helper that is used by the compositor to render.
 */

/*!
 * @addtogroup comp_render
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
 * Util functions.
 *
 */

/*!
 * Calculates a timewarp matrix which takes in NDC coords and gives out results
 * in [-1, 1] space that needs a perspective divide.
 */
void
comp_calc_time_warp_matrix(const struct xrt_pose *src_pose,
                           const struct xrt_fov *src_fov,
                           const struct xrt_pose *new_pose,
                           struct xrt_matrix_4x4 *matrix);


/*
 *
 * Shaders.
 *
 */

/*!
 * Holds all shaders.
 */
struct comp_shaders
{
	VkShaderModule clear_comp;
	VkShaderModule distortion_comp;
	VkShaderModule distortion_timewarp_comp;

	VkShaderModule mesh_vert;
	VkShaderModule mesh_frag;

	VkShaderModule equirect1_vert;
	VkShaderModule equirect1_frag;

	VkShaderModule equirect2_vert;
	VkShaderModule equirect2_frag;

	VkShaderModule layer_vert;
	VkShaderModule layer_frag;
};

/*!
 * Loads all of the shaders that the compositor uses.
 */
bool
comp_shaders_load(struct comp_shaders *s, struct vk_bundle *vk);

/*!
 * Unload and cleanup shaders.
 */
void
comp_shaders_close(struct comp_shaders *s, struct vk_bundle *vk);


/*
 *
 * Buffer
 *
 */

/*!
 * Helper struct holding a buffer and its memory.
 */
struct comp_buffer
{
	//! Backing memory.
	VkDeviceMemory memory;

	//! Buffer.
	VkBuffer buffer;

	//! Size requested for the buffer.
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
	//! Vulkan resources.
	struct vk_bundle *vk;

	/*
	 * Loaded resources.
	 */

	//! All shaders loaded.
	struct comp_shaders *shaders;


	/*
	 * Shared pools and caches.
	 */

	//! Shared for all rendering.
	VkPipelineCache pipeline_cache;


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

		uint32_t vertex_count;
		uint32_t index_counts[2];
		uint32_t stride;
		uint32_t index_offsets[2];
		uint32_t index_count_total;

		//! Descriptor pool for mesh shaders.
		VkDescriptorPool descriptor_pool;

		//! Info ubos, only supports two views currently.
		struct comp_buffer ubos[2];
	} mesh;

	struct
	{
		struct
		{
			VkImage image;
			VkImageView image_view;
			VkDeviceMemory memory;
		} color;
	} dummy;

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

		//! Doesn't depend on target so is static.
		VkPipeline distortion_timewarp_pipeline;

		//! Target info.
		struct comp_buffer ubo;
	} compute;

	struct
	{
		//! Transform to go from UV to tangle angles.
		struct xrt_normalized_rect uv_to_tanangle[2];

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
comp_resources_init(struct comp_resources *r,
                    struct comp_shaders *shaders,
                    struct vk_bundle *vk,
                    struct xrt_device *xdev);

/*!
 * Free all pools and static resources, does not free the struct itself.
 */
void
comp_resources_close(struct comp_resources *r);


/*
 *
 * Rendering target
 *
 */

/*!
 * Each rendering (@ref comp_rendering) render to one or more targets
 * (@ref comp_rendering_target_resources), each target can have one or more
 * views (@ref comp_rendering_view), this struct holds all the data that is
 * specific to the target.
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
 * Each rendering (@ref comp_rendering) render to one or more targets
 * (@ref comp_rendering_target_resources), each target can have one or more
 * views (@ref comp_rendering_view), this struct holds all the vulkan resources
 * that is specific to the target.
 *
 * Technically the framebuffer could be moved out of this struct and all of this
 * state be turned into a CSO object that depends only only the format and
 * external status of the target, but is combined to reduce the number of
 * objects needed to render.
 */
struct comp_rendering_target_resources
{
	//! Collections of static resources.
	struct comp_resources *r;

	//! The data for this target.
	struct comp_target_data data;

	//! Render pass used for rendering, does not depend on framebuffer.
	VkRenderPass render_pass;

	struct
	{
		//! Pipeline layout used for mesh, does not depend on framebuffer.
		VkPipeline pipeline;
	} mesh;

	//! Framebuffer for this target, depends on given VkImageView.
	VkFramebuffer framebuffer;
};


/*!
 * Init a target resource struct, caller has to keep target alive until closed.
 */
bool
comp_rendering_target_resources_init(struct comp_rendering_target_resources *rtr,
                                     struct comp_resources *r,
                                     VkImageView target,
                                     struct comp_target_data *data);

/*!
 * Frees all resources held by the target, does not free the struct itself.
 */
void
comp_rendering_target_resources_close(struct comp_rendering_target_resources *rtr);


/*
 *
 * Rendering
 *
 */

/*!
 * Each rendering (@ref comp_rendering) render to one or more targets
 * (@ref comp_rendering_target_resources), each target can have one or more
 * views (@ref comp_rendering_view), this struct holds all the vulkan resources
 * that is specific to the view.
 */
struct comp_rendering_view
{
	struct
	{
		VkDescriptorSet descriptor_set;
	} mesh;
};

/*!
 * A rendering is used to create command buffers needed to do one frame of
 * compositor rendering, it holds onto resources used by the command buffer.
 */
struct comp_rendering
{
	//! Resources that we are based on.
	struct comp_resources *r;

	//! The current target we are rendering too, can change during command building.
	struct comp_rendering_target_resources *rtr;

	//! Command buffer where all commands are recorded.
	VkCommandBuffer cmd;

	//! Holds per view data.
	struct comp_rendering_view views[2];

	//! The current view we are rendering to.
	uint32_t current_view;
};

/*!
 * Init struct and create resources needed for rendering.
 */
bool
comp_rendering_init(struct comp_rendering *rr, struct comp_resources *r);

/*!
 * Frees any unneeded resources and ends the command buffer so it can be used.
 */
void
comp_rendering_finalize(struct comp_rendering *rr);

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

	struct xrt_normalized_rect post_transform;
};

/*!
 * This function allocates everything to start a single rendering. This is the
 * first function you call when you start rendering, you follow up with a call
 * to comp_draw_begin_view.
 */
bool
comp_draw_begin_target(struct comp_rendering *rr, struct comp_rendering_target_resources *rtr);

void
comp_draw_end_target(struct comp_rendering *rr);

void
comp_draw_begin_view(struct comp_rendering *rr, uint32_t view, struct comp_viewport_data *viewport_data);

void
comp_draw_end_view(struct comp_rendering *rr);

void
comp_draw_distortion(struct comp_rendering *rr);


/*
 *
 * Update functions.
 *
 */

void
comp_draw_update_distortion(struct comp_rendering *rr,
                            uint32_t view,
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
	//! Shared resources.
	struct comp_resources *r;

	//! Command buffer where all commands are recorded.
	VkCommandBuffer cmd;

	//! Shared descriptor set between clear, projection and timewarp.
	VkDescriptorSet descriptor_set;
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
comp_rendering_compute_init(struct comp_rendering_compute *crc, struct comp_resources *r);

/*!
 * Frees all resources held by the compute rendering, does not free the struct itself.
 */
void
comp_rendering_compute_close(struct comp_rendering_compute *crc);

bool
comp_rendering_compute_begin(struct comp_rendering_compute *crc);

void
comp_rendering_compute_projection_timewarp(struct comp_rendering_compute *crc,
                                           VkSampler src_samplers[2],
                                           VkImageView src_image_views[2],
                                           const struct xrt_normalized_rect src_rects[2],
                                           const struct xrt_pose src_poses[2],
                                           const struct xrt_fov src_fovs[2],
                                           const struct xrt_pose new_poses[2],
                                           VkImage target_image,
                                           VkImageView target_image_view,
                                           const struct comp_viewport_data views[2]);

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
