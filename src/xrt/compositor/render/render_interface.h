// Copyright 2019-2022, Collabora, Ltd.
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
render_calc_time_warp_matrix(const struct xrt_pose *src_pose,
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
struct render_shaders
{
	VkShaderModule clear_comp;
	VkShaderModule distortion_comp;

	VkShaderModule mesh_vert;
	VkShaderModule mesh_frag;

	VkShaderModule equirect1_vert;
	VkShaderModule equirect1_frag;

	VkShaderModule equirect2_vert;
	VkShaderModule equirect2_frag;

	VkShaderModule cube_vert;
	VkShaderModule cube_frag;

	VkShaderModule layer_vert;
	VkShaderModule layer_frag;
};

/*!
 * Loads all of the shaders that the compositor uses.
 */
bool
render_shaders_load(struct render_shaders *s, struct vk_bundle *vk);

/*!
 * Unload and cleanup shaders.
 */
void
render_shaders_close(struct render_shaders *s, struct vk_bundle *vk);


/*
 *
 * Buffer
 *
 */

/*!
 * Helper struct holding a buffer and its memory.
 */
struct render_buffer
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
render_buffer_init(struct vk_bundle *vk,
                   struct render_buffer *buffer,
                   VkBufferUsageFlags usage_flags,
                   VkMemoryPropertyFlags memory_property_flags,
                   VkDeviceSize size);

/*!
 * Initialize a buffer, making it exportable.
 */
VkResult
render_buffer_init_exportable(struct vk_bundle *vk,
                              struct render_buffer *buffer,
                              VkBufferUsageFlags usage_flags,
                              VkMemoryPropertyFlags memory_property_flags,
                              VkDeviceSize size);

/*!
 * Frees all resources that this buffer has, but does not free the buffer itself.
 */
void
render_buffer_close(struct vk_bundle *vk, struct render_buffer *buffer);

/*!
 * Maps the memory, sets render_buffer::mapped to the memory.
 */
VkResult
render_buffer_map(struct vk_bundle *vk, struct render_buffer *buffer);

/*!
 * Unmaps the memory.
 */
void
render_buffer_unmap(struct vk_bundle *vk, struct render_buffer *buffer);

/*!
 * Maps the buffer, and copies the given data to the buffer.
 */
VkResult
render_buffer_map_and_write(struct vk_bundle *vk, struct render_buffer *buffer, void *data, VkDeviceSize size);

/*!
 * Writes the given data to the buffer, will map it temporarily if not mapped.
 */
VkResult
render_buffer_write(struct vk_bundle *vk, struct render_buffer *buffer, void *data, VkDeviceSize size);


/*
 *
 * Resources
 *
 */

/*!
 * Holds all pools and static resources for rendering.
 */
struct render_resources
{
	//! Vulkan resources.
	struct vk_bundle *vk;

	/*
	 * Loaded resources.
	 */

	//! All shaders loaded.
	struct render_shaders *shaders;


	/*
	 * Shared pools and caches.
	 */

	//! Shared for all rendering.
	VkPipelineCache pipeline_cache;

	VkCommandPool cmd_pool;

	VkQueryPool query_pool;


	/*
	 * Static
	 */

	//! Command buffer for recording everything.
	VkCommandBuffer cmd;

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

		struct render_buffer vbo;
		struct render_buffer ibo;

		uint32_t vertex_count;
		uint32_t index_counts[2];
		uint32_t stride;
		uint32_t index_offsets[2];
		uint32_t index_count_total;

		//! Descriptor pool for mesh shaders.
		VkDescriptorPool descriptor_pool;

		//! Info ubos, only supports two views currently.
		struct render_buffer ubos[2];
	} mesh;

	/*!
	 * Used as a scratch buffer by the compute layer renderer.
	 */
	struct
	{
		VkExtent2D extent;

		struct
		{
			VkDeviceMemory memory;
			VkImage image;
			VkImageView srgb_view;
			VkImageView unorm_view;
		} color;
	} scratch;

	/*!
	 * Used as a default image empty image when none is given or to pad
	 * out fixed sized descriptor sets.
	 */
	struct
	{
		struct
		{
			VkImage image;
			VkImageView image_view;
			VkDeviceMemory memory;
		} color;
	} mock;

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

		//! Default sampler for null images.
		VkSampler default_sampler;

		struct
		{
			//! Descriptor set layout for compute distortion.
			VkDescriptorSetLayout descriptor_set_layout;

			//! Pipeline layout used for compute distortion, shared with clear.
			VkPipelineLayout pipeline_layout;

			//! Doesn't depend on target so is static.
			VkPipeline pipeline;

			//! Doesn't depend on target so is static.
			VkPipeline timewarp_pipeline;

			//! Target info.
			struct render_buffer ubo;
		} distortion;

		struct
		{
			//! Doesn't depend on target so is static.
			VkPipeline pipeline;

			//! Target info.
			struct render_buffer ubo;
		} clear;
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

		//! Whether distortion images have been pre-rotated 90 degrees.
		bool pre_rotated;
	} distortion;
};

/*!
 * Allocate pools and static resources.
 *
 * @ingroup comp_main
 *
 * @public @memberof render_resources
 */
bool
render_resources_init(struct render_resources *r,
                      struct render_shaders *shaders,
                      struct vk_bundle *vk,
                      struct xrt_device *xdev);

/*!
 * Free all pools and static resources, does not free the struct itself.
 *
 * @public @memberof render_resources
 */
void
render_resources_close(struct render_resources *r);

/*!
 * Creates or recreates the compute distortion textures if necessary.
 */
bool
render_ensure_distortion_buffer(struct render_resources *r,
                                struct vk_bundle *vk,
                                struct xrt_device *xdev,
                                bool pre_rotate);

/*!
 * Ensure that the scratch image is created and has the given extent.
 */
bool
render_ensure_scratch_image(struct render_resources *r, VkExtent2D extent);

/*!
 * Returns the timestamps for when the latest GPU work started and stopped that
 * was submitted using @ref render_gfx or @ref render_compute cmd buf builders.
 *
 * Returned in the same time domain as returned by @ref os_monotonic_get_ns .
 * Behaviour for this function is undefined if the GPU has not completed before
 * calling this function, so make sure to call vkQueueWaitIdle or wait on the
 * fence that the work was submitted with have fully completed. See other
 * limitation mentioned for @ref vk_convert_timestamps_to_host_ns .
 *
 * @see vk_convert_timestamps_to_host_ns
 *
 * @public @memberof render_resources
 */
bool
render_resources_get_timestamps(struct render_resources *r, uint64_t *out_gpu_start_ns, uint64_t *out_gpu_end_ns);


/*
 *
 * Shared between both gfx and compute.
 *
 */

/*!
 *  The pure data information about a view that the renderer is rendering to.
 */
struct render_viewport_data
{
	uint32_t x, y;
	uint32_t w, h;
};


/*
 *
 * Rendering target
 *
 */

/*!
 * Each rendering (@ref render_gfx) render to one or more targets
 * (@ref render_gfx_target_resources), each target can have one or more
 * views (@ref render_gfx_view), this struct holds all the data that is
 * specific to the target.
 */
struct render_gfx_target_data
{
	// The format that should be used to read from the target.
	VkFormat format;

	// Is this target a external target.
	bool is_external;

	//! Total height and width of the target.
	uint32_t width, height;
};

/*!
 * Each rendering (@ref render_gfx) render to one or more targets
 * (@ref render_gfx_target_resources), each target can have one or more
 * views (@ref render_gfx_view), this struct holds all the vulkan resources
 * that is specific to the target.
 *
 * Technically the framebuffer could be moved out of this struct and all of this
 * state be turned into a CSO object that depends only only the format and
 * external status of the target, but is combined to reduce the number of
 * objects needed to render.
 */
struct render_gfx_target_resources
{
	//! Collections of static resources.
	struct render_resources *r;

	//! The data for this target.
	struct render_gfx_target_data data;

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
 *
 * @public @memberof render_gfx_target_resources
 */
bool
render_gfx_target_resources_init(struct render_gfx_target_resources *rtr,
                                 struct render_resources *r,
                                 VkImageView target,
                                 struct render_gfx_target_data *data);

/*!
 * Frees all resources held by the target, does not free the struct itself.
 *
 * @public @memberof render_gfx_target_resources
 */
void
render_gfx_target_resources_close(struct render_gfx_target_resources *rtr);


/*
 *
 * Rendering
 *
 */

/*!
 * Each rendering (@ref render_gfx) render to one or more targets
 * (@ref render_gfx_target_resources), each target can have one or more
 * views (@ref render_gfx_view), this struct holds all the vulkan resources
 * that is specific to the view.
 */
struct render_gfx_view
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
struct render_gfx
{
	//! Resources that we are based on.
	struct render_resources *r;

	//! The current target we are rendering too, can change during command building.
	struct render_gfx_target_resources *rtr;

	//! Holds per view data.
	struct render_gfx_view views[2];

	//! The current view we are rendering to.
	uint32_t current_view;
};

/*!
 * Init struct and create resources needed for rendering.
 *
 * @public @memberof render_gfx
 */
bool
render_gfx_init(struct render_gfx *rr, struct render_resources *r);

/*!
 * Begins the rendering, takes the vk_bundle's pool lock and leaves it locked.
 *
 * @public @memberof render_gfx
 */
bool
render_gfx_begin(struct render_gfx *rr);

/*!
 * Frees any unneeded resources and ends the command buffer so it can be used,
 * also unlocks the vk_bundle's pool lock that was taken by begin.
 *
 * @public @memberof render_gfx
 */
bool
render_gfx_end(struct render_gfx *rr);

/*!
 * Frees all resources held by the rendering, does not free the struct itself.
 *
 * @public @memberof render_gfx
 */
void
render_gfx_close(struct render_gfx *rr);


/*
 *
 * Drawing
 *
 */

/*!
 * UBO data that is sent to the mesh shaders.
 */
struct render_gfx_mesh_ubo_data
{
	struct xrt_matrix_2x2 vertex_rot;

	struct xrt_normalized_rect post_transform;
};

/*!
 * @name Drawing functions
 * @{
 */

/*!
 * This function allocates everything to start a single rendering. This is the
 * first function you call when you start rendering, you follow up with a call
 * to render_gfx_begin_view.
 *
 * @public @memberof render_gfx
 */
bool
render_gfx_begin_target(struct render_gfx *rr, struct render_gfx_target_resources *rtr);

/*!
 * @public @memberof render_gfx
 */
void
render_gfx_end_target(struct render_gfx *rr);

/*!
 * @public @memberof render_gfx
 */
void
render_gfx_begin_view(struct render_gfx *rr, uint32_t view, struct render_viewport_data *viewport_data);

/*!
 * @public @memberof render_gfx
 */
void
render_gfx_end_view(struct render_gfx *rr);

/*!
 * @public @memberof render_gfx
 */
void
render_gfx_distortion(struct render_gfx *rr);

/*!
 * @}
 */

/*
 *
 * Update functions.
 *
 */

/*!
 * @name Update functions
 * @{
 */
/*!
 * @public @memberof render_gfx
 */
void
render_gfx_update_distortion(struct render_gfx *rr,
                             uint32_t view,
                             VkSampler sampler,
                             VkImageView image_view,
                             struct render_gfx_mesh_ubo_data *data);
//! @}


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
struct render_compute
{
	//! Shared resources.
	struct render_resources *r;

	//! Shared descriptor set between clear, projection and timewarp.
	VkDescriptorSet distortion_descriptor_set;
};

/*!
 * UBO data that is sent to the compute distortion shaders.
 *
 * Used in @ref render_compute
 */
struct render_compute_distortion_ubo_data
{
	struct render_viewport_data views[2];
	struct xrt_normalized_rect pre_transforms[2];
	struct xrt_normalized_rect post_transforms[2];
	struct xrt_matrix_4x4 transforms[2];
};

/*!
 * Init struct and create resources needed for compute rendering.
 *
 * @public @memberof render_compute
 */
bool
render_compute_init(struct render_compute *crc, struct render_resources *r);

/*!
 * Frees all resources held by the compute rendering, does not free the struct itself.
 *
 * @public @memberof render_compute
 */
void
render_compute_close(struct render_compute *crc);

/*!
 * Begin the compute command buffer building, takes the vk_bundle's pool lock
 * and leaves it locked.
 *
 * @public @memberof render_compute
 */
bool
render_compute_begin(struct render_compute *crc);

/*!
 * Frees any unneeded resources and ends the command buffer so it can be used,
 * also unlocks the vk_bundle's pool lock that was taken by begin.
 *
 * @public @memberof render_compute
 */
bool
render_compute_end(struct render_compute *crc);

/*!
 * @public @memberof render_compute
 */
void
render_compute_projection_timewarp(struct render_compute *crc,
                                   VkSampler src_samplers[2],
                                   VkImageView src_image_views[2],
                                   const struct xrt_normalized_rect src_rects[2],
                                   const struct xrt_pose src_poses[2],
                                   const struct xrt_fov src_fovs[2],
                                   const struct xrt_pose new_poses[2],
                                   VkImage target_image,
                                   VkImageView target_image_view,
                                   const struct render_viewport_data views[2]);

/*!
 * @public @memberof render_compute
 */
void
render_compute_projection(struct render_compute *crc,                    //
                          VkSampler src_samplers[2],                     //
                          VkImageView src_image_views[2],                //
                          const struct xrt_normalized_rect src_rects[2], //
                          VkImage target_image,                          //
                          VkImageView target_image_view,                 //
                          const struct render_viewport_data views[2]);   //

/*!
 * @public @memberof render_compute
 */
void
render_compute_clear(struct render_compute *crc,                  //
                     VkImage target_image,                        //
                     VkImageView target_image_view,               //
                     const struct render_viewport_data views[2]); //



/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
