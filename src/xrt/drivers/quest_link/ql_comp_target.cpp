/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ql_comp_target.h"
#include "main/comp_compositor.h"
#include "math/m_space.h"
#include "util/u_pacing.h"
#include "video_encoder.h"
#include "xrt/xrt_config_have.h"
#include "xrt_cast.h"
#include <atomic>
#include <condition_variable>
#include <list>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "ql_types.h"
#include "ql_xrsp.h"
#include "ql_system.h"

namespace xrt::drivers::quest_link
{

static const uint8_t image_free = 0;
static const uint8_t image_acquired = 1;

struct pseudo_swapchain
{
	struct pseudo_swapchain_memory
	{
		VkFence fence;
		VkDeviceMemory memory;
		uint8_t status; // bitmask of consumer status, index 0 for acquired, the rest for each encoder
		uint64_t frame_index;
		xrt::drivers::wivrn::to_headset::video_stream_data_shard::view_info_t view_info{};
	} * images{};
	std::mutex mutex;
	std::condition_variable cv;
};

struct ql_comp_target : public comp_target
{
	ql_xrsp_host* host;

	//! Compositor frame pacing helper
	struct u_pacing_compositor * upc{};

	float fps;

	int64_t current_frame_id;

	int64_t last_avg_tx;
	int64_t last_avg_enc;
	int frames_since_encode_adjust;

	// Monotonic counter, for video stream
	uint64_t frame_index = 0;

	struct pseudo_swapchain psc;

	VkColorSpaceKHR color_space;

	struct encoder_thread
	{
		int index;
		os_thread_helper thread;

		encoder_thread()
		{
			os_thread_helper_init(&thread);
		}
		~encoder_thread()
		{
			os_thread_helper_stop_and_wait(&thread);
			os_thread_helper_destroy(&thread);
		}
	};
	std::list<encoder_thread> encoder_threads;
	std::vector<std::shared_ptr<xrt::drivers::wivrn::VideoEncoder>> encoders;
};

static void target_init_semaphores(struct ql_comp_target * cn);

static void target_fini_semaphores(struct ql_comp_target * cn);

static inline struct vk_bundle * get_vk(struct ql_comp_target * cn)
{
	return &cn->c->base.vk;
}

static void destroy_images(struct ql_comp_target * cn)
{
	if (cn->images == NULL)
	{
		return;
	}

	cn->encoder_threads.clear();
	cn->encoders.clear();

	struct vk_bundle * vk = get_vk(cn);

	for (uint32_t i = 0; i < cn->image_count; i++)
	{
		if (cn->images[i].view == VK_NULL_HANDLE)
		{
			continue;
		}

		vk->vkDestroyImageView(vk->device, cn->images[i].view, NULL);
		vk->vkDestroyImage(vk->device, cn->images[i].handle, NULL);
		vk->vkFreeMemory(vk->device, cn->psc.images[i].memory, NULL);
	}

	free(cn->images);
	free(cn->psc.images);
	cn->images = NULL;
	cn->psc.images = NULL;

	target_fini_semaphores(cn);
}

struct encoder_thread_param
{
	ql_comp_target * cn;
	ql_comp_target::encoder_thread * thread;
	std::vector<std::shared_ptr<xrt::drivers::wivrn::VideoEncoder>> encoders;
};

static void * comp_ql_present_thread(void * void_param);

static void create_encoders(ql_comp_target * cn, std::vector<xrt::drivers::wivrn::encoder_settings> & _settings)
{
	auto vk = get_vk(cn);
	assert(cn->encoders.empty());
	assert(cn->encoder_threads.empty());

	xrt::drivers::wivrn::to_headset::video_stream_description desc{};
	desc.width = cn->width;
	desc.height = cn->height;
	desc.fps = cn->fps;

	cn->last_avg_tx = 0;
	cn->last_avg_enc = 0;
	cn->frames_since_encode_adjust = 0;

	std::map<int, encoder_thread_param> thread_params;

	for (auto & settings: _settings)
	{
		uint8_t stream_index = cn->encoders.size();

		int slice_w = desc.width;
		int slice_h = desc.height / QL_NUM_SLICES;
		for (int slice_num = 0; slice_num < QL_NUM_SLICES; slice_num++)
		{
			auto & encoder = cn->encoders.emplace_back(
			        xrt::drivers::wivrn::VideoEncoder::Create(vk, settings, stream_index, slice_num, QL_NUM_SLICES, slice_w, slice_h, desc.fps));
			encoder->SetXrspHost(cn->host);

			std::vector<VkImage> images(cn->image_count);
			std::vector<VkDeviceMemory> memory(cn->image_count);
			std::vector<VkImageView> views(cn->image_count);

			for (size_t j = 0; j < cn->image_count; j++)
			{
				images[j] = cn->images[j].handle;
				memory[j] = cn->psc.images[j].memory;
				views[j] = cn->images[j].view;
			}
			encoder->SetImages(cn->width, cn->height, cn->format, cn->image_count, images.data(), views.data(), memory.data());
			thread_params[settings.group].encoders.emplace_back(encoder);
		}

		desc.items.push_back(settings);
	}

	for (auto & [group, params]: thread_params)
	{
		auto params_ptr = new encoder_thread_param(params);
		auto & thread = cn->encoder_threads.emplace_back();
		thread.index = cn->encoder_threads.size() - 1;
		params_ptr->thread = &thread;
		params_ptr->cn = cn;
		os_thread_helper_start(&thread.thread, comp_ql_present_thread, params_ptr);
		std::string name = "encoder " + std::to_string(group);
		os_thread_helper_name(&thread.thread, name.c_str());
	}
	//cn->cnx->send_control(desc);
}

class drm_image_modifier_helper
{
	VkImageDrmFormatModifierListCreateInfoEXT drm_info{};
	std::vector<uint64_t> modifiers;

public:
	void * pNext = nullptr;
	drm_image_modifier_helper(vk_bundle * vk, VkFormat format, VkImageTiling tiling)
	{
		if (tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
			return;

		VkDrmFormatModifierPropertiesListEXT drm_list{};
		drm_list.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;

		VkFormatProperties2 format_prop{};
		format_prop.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
		format_prop.pNext = &drm_list;

		vk->vkGetPhysicalDeviceFormatProperties2(vk->physical_device, format, &format_prop);

		std::vector<VkDrmFormatModifierPropertiesEXT> properties(drm_list.drmFormatModifierCount);
		drm_list.pDrmFormatModifierProperties = properties.data();

		vk->vkGetPhysicalDeviceFormatProperties2(vk->physical_device, format, &format_prop);

		VkFormatFeatureFlags required_features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
		                                         VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
		                                         VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
		for (const auto & mod: properties)
		{
			if ((mod.drmFormatModifierTilingFeatures & required_features) == required_features)
				modifiers.push_back(mod.drmFormatModifier);
		}

		assert(not modifiers.empty());

		drm_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT;
		drm_info.drmFormatModifierCount = modifiers.size();
		drm_info.pDrmFormatModifiers = modifiers.data();

		pNext = &drm_info;
	}
};

static VkResult create_images(struct ql_comp_target * cn, VkImageUsageFlags flags, VkImageTiling tiling)
{
	struct vk_bundle * vk = get_vk(cn);

	assert(cn->image_count > 0);
	COMP_DEBUG(cn->c, "Creating %d images.", cn->image_count);

	destroy_images(cn);

	cn->images = U_TYPED_ARRAY_CALLOC(struct comp_target_image, cn->image_count);
	cn->psc.images = U_TYPED_ARRAY_CALLOC(pseudo_swapchain::pseudo_swapchain_memory, cn->image_count);

	VkImageSubresourceRange subresource_range{};
	subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresource_range.baseMipLevel = 0;
	subresource_range.levelCount = 1;
	subresource_range.baseArrayLayer = 0;
	subresource_range.layerCount = 1;

	flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	drm_image_modifier_helper drm_list(vk, cn->format, tiling);

	for (uint32_t i = 0; i < cn->image_count; i++)
	{
		auto & image = cn->images[i].handle;
		auto & image_view = cn->images[i].view;
		auto & memory = cn->psc.images[i].memory;

		VkExtent3D extent{cn->width, cn->height, 1};

		VkImageCreateInfo image_info{};
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.pNext = drm_list.pNext;
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.format = cn->format;
		image_info.extent = extent;
		image_info.mipLevels = 1;
		image_info.arrayLayers = 1;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.tiling = tiling;
		image_info.usage = flags;
		image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_info.queueFamilyIndexCount = 0;
		image_info.pQueueFamilyIndices = NULL;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkResult res = vk->vkCreateImage(vk->device, &image_info, NULL, &image);
		vk_check_error("vkCreateImage", res, res);

		VkMemoryDedicatedAllocateInfoKHR dedicated_allocate_info{};
		dedicated_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
		dedicated_allocate_info.image = image;

		VkDeviceSize size;
		res = vk_alloc_and_bind_image_memory(vk, image, -1, &dedicated_allocate_info, "ql_comp_target", &memory, &size);
		vk_check_error("vk_alloc_and_bind_image_memory", res, res);

		res = vk_create_view(          //
		        vk,                    // vk_bundle
		        image,                 // image
		        VK_IMAGE_VIEW_TYPE_2D, // type
		        cn->format,            // format
		        subresource_range,     // subresource_range
		        &image_view);          // out_view
		vk_check_error("vk_create_view", res, res);

		VkFenceCreateInfo createinfo{};
		createinfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		createinfo.pNext = NULL;
		createinfo.flags = 0;

		res = vk->vkCreateFence(vk->device, &createinfo, NULL, &cn->psc.images[i].fence);

		vk_check_error("vkCreateFence", res, res);
	}

	return VK_SUCCESS;
}

static bool comp_ql_init_pre_vulkan(struct comp_target * ct)
{
	return true;
}

static bool comp_ql_init_post_vulkan(struct comp_target * ct, uint32_t preferred_width, uint32_t preferred_height)
{
	return true;
}

static bool comp_ql_check_ready(struct comp_target * ct)
{
	return true;
}

static void target_fini_semaphores(struct ql_comp_target * cn)
{
	struct vk_bundle * vk = get_vk(cn);

	if (cn->semaphores.present_complete != VK_NULL_HANDLE)
	{
		vk->vkDestroySemaphore(vk->device, cn->semaphores.present_complete, NULL);
		cn->semaphores.present_complete = VK_NULL_HANDLE;
	}

	if (cn->semaphores.render_complete != VK_NULL_HANDLE)
	{
		vk->vkDestroySemaphore(vk->device, cn->semaphores.render_complete, NULL);
		cn->semaphores.render_complete = VK_NULL_HANDLE;
	}
}

static void target_init_semaphores(struct ql_comp_target * cn)
{
	struct vk_bundle * vk = get_vk(cn);
	VkResult ret;

	target_fini_semaphores(cn);

	VkSemaphoreCreateInfo info = {
	        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	ret = vk->vkCreateSemaphore(vk->device, &info, NULL, &cn->semaphores.present_complete);
	if (ret != VK_SUCCESS)
	{
		COMP_ERROR(cn->c, "vkCreateSemaphore: %s", vk_result_string(ret));
	}

	cn->semaphores.render_complete_is_timeline = false;
	ret = vk->vkCreateSemaphore(vk->device, &info, NULL, &cn->semaphores.render_complete);
	if (ret != VK_SUCCESS)
	{
		COMP_ERROR(cn->c, "vkCreateSemaphore: %s", vk_result_string(ret));
	}
}

static void comp_ql_create_images(struct comp_target * ct,
                                     uint32_t preferred_width,
                                     uint32_t preferred_height,
                                     VkFormat preferred_color_format,
                                     VkColorSpaceKHR preferred_color_space,
                                     VkImageUsageFlags image_usage,
                                     VkPresentModeKHR present_mode)
{
	struct ql_comp_target * cn = (struct ql_comp_target *)ct;

	uint64_t now_ns = os_monotonic_get_ns();
	if (cn->upc == NULL)
	{
		u_pc_fake_create(ct->c->settings.nominal_frame_interval_ns, now_ns, &cn->upc);
	}

	// Free old images.
	destroy_images(cn);

	target_init_semaphores(cn);

	ct->format = preferred_color_format;
	ct->width = preferred_width;
	ct->height = preferred_height;
	ct->surface_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

	cn->image_count = 3;
	cn->format = preferred_color_format;
	cn->width = preferred_width;
	cn->height = preferred_height;
	cn->color_space = preferred_color_space;

	auto settings = xrt::drivers::wivrn::get_encoder_settings(get_vk(cn), cn->width, cn->height);

	VkResult res = create_images(cn, image_usage, get_required_tiling(get_vk(cn), settings));
	if (vk_has_error(res, "create_images", __FILE__, __LINE__))
	{
		// TODO
		abort();
	}

	try
	{
		create_encoders(cn, settings);
	}
	catch (const std::exception & e)
	{
		U_LOG_E("Failed to create video encoder: %s", e.what());
		abort();
	}
}

static bool comp_ql_has_images(struct comp_target * ct)
{
	return ct->images;
}

static VkResult comp_ql_acquire(struct comp_target * ct, uint32_t * out_index)
{
	struct ql_comp_target * cn = (struct ql_comp_target *)ct;
	struct vk_bundle * vk = get_vk(cn);

	VkSubmitInfo submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = NULL;
	submit.waitSemaphoreCount = 0;
	submit.pWaitSemaphores = NULL;
	submit.pWaitDstStageMask = NULL;
	submit.commandBufferCount = 0;
	submit.pCommandBuffers = NULL;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &cn->semaphores.present_complete;

	VkResult res = vk_locked_submit(vk, vk->queue, 1, &submit, VK_NULL_HANDLE);
	vk_check_error("vk_locked_submit", res, res);

	res = VK_ERROR_OUT_OF_HOST_MEMORY;

	while (res != VK_SUCCESS)
	{
		std::this_thread::yield();

		std::lock_guard lock(cn->psc.mutex);
		for (uint32_t i = 0; i < ct->image_count; i++)
		{
			if (cn->psc.images[i].status == image_free)
			{
				cn->psc.images[i].status = image_acquired;

				*out_index = i;
				res = VK_SUCCESS;
				break;
			}
		}
		//printf("Not Acquire?\n");
	};

	return res;
}

static void * comp_ql_present_thread(void * void_param)
{
	std::unique_ptr<encoder_thread_param> param((encoder_thread_param *)void_param);
	struct ql_comp_target * cn = param->cn;
	struct vk_bundle * vk = get_vk(cn);
	U_LOG_I("Starting encoder thread %d", param->thread->index);

	uint8_t status_bit = 1 << (param->thread->index + 1);

	std::vector<VkFence> fences(cn->image_count);
	std::vector<int> indices(cn->image_count);
	while (os_thread_helper_is_running(&param->thread->thread))
	{
		int presenting_index = -1;
		int nb_fences = 0;
		{
			std::unique_lock lock(cn->psc.mutex);

			uint64_t timestamp = xrsp_ts_ns(cn->host);

			for (uint32_t i = 0; i < cn->image_count; i++)
			{
				if (cn->psc.images[i].status & status_bit)
				{
					//printf("%llx %llx\n", timestamp, cn->psc.images[i].view_info.display_time);
					//if (timestamp < cn->psc.images[i].view_info.display_time)
					{
						presenting_index = i;
					}
					indices[nb_fences] = i;
					fences[nb_fences++] = cn->psc.images[i].fence;
					break;
				}
			}

			if (presenting_index < 0)
			{
				// condition variable is not notified when we want to stop thread,
				// use a timeout, but longer than a typical frame
				cn->psc.cv.wait_for(lock, std::chrono::milliseconds(50));
				//std::this_thread::yield();
				continue;
			}
		}

		VkResult res = vk->vkWaitForFences(vk->device, nb_fences, fences.data(), VK_TRUE, UINT64_MAX);
		vk_check_error("vkWaitForFences", res, NULL);

		if (++cn->frames_since_encode_adjust > 120)
		{
			if (cn->last_avg_enc / 1000000.0 > 1.0) {
				for (auto & encoder: param->encoders)
				{
					encoder->ModifyBitrate(-1000000);
				}
			}
			else if (cn->last_avg_enc / 1000000.0 < 1.0) {
				for (auto & encoder: param->encoders)
				{
					encoder->ModifyBitrate(100000);
				}
			}
			cn->frames_since_encode_adjust = 0;
		}
		

		const auto & psc_image = cn->psc.images[presenting_index];
#ifdef XRT_HAVE_VT // TODO: nvenc etc etc
		for (int i = 0; i < QL_NUM_SLICES; i++) {
			cn->host->start_encode(cn->host, psc_image.view_info.display_time, presenting_index, i);
		}
#endif

		try
		{
			for (auto & encoder: param->encoders)
			{
				bool idr_requested = false;

#ifndef XRT_HAVE_VT // TODO: nvenc etc etc
				cn->host->start_encode(cn->host, psc_image.view_info.display_time, presenting_index, encoder->slice_idx);
#endif
				encoder->Encode(nullptr, psc_image.view_info, psc_image.frame_index, presenting_index, idr_requested);
			}
		}
		catch (...)
		{
			// Ignore errors
		}
#if 0
		for (int i = 0; i < QL_NUM_SLICES; i++) {
			cn->host->start_encode(cn->host, psc_image.view_info.display_time, presenting_index, i);
		}
#endif

		std::lock_guard lock(cn->psc.mutex);
		for (int i = 0; i < nb_fences; i++)
		{
			cn->psc.images[indices[i]].status &= ~status_bit;
		}
	}

	return NULL;
}

static VkResult comp_ql_present(struct comp_target * ct,
                                   VkQueue queue,
                                   uint32_t index,
                                   uint64_t timeline_semaphore_value,
                                   uint64_t desired_present_time_ns,
                                   uint64_t present_slop_ns)
{
	struct ql_comp_target * cn = (struct ql_comp_target *)ct;
	struct vk_bundle * vk = get_vk(cn);

	assert(index < cn->image_count);

	if (cn->c->base.slot.layer_count == 0)
	{
		// TODO: Tell the headset that there is no image to display
		assert(cn->psc.images[index].status == image_acquired);
		cn->psc.images[index].status = image_free;
		return VK_SUCCESS;
	}

	assert(index < ct->image_count);
	assert(ct->images != NULL);

	std::vector<VkCommandBuffer> cmdBuffers;

	VkCommandBuffer cmdBuffer;
	VkResult res = vk_cmd_buffer_create_and_begin(vk, &cmdBuffer);
	vk_check_error("vk_cmd_buffer_create_and_begin", res, res);

	VkImageSubresourceRange first_color_level_subresource_range{};
	first_color_level_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	first_color_level_subresource_range.baseMipLevel = 0;
	first_color_level_subresource_range.levelCount = 1;
	first_color_level_subresource_range.baseArrayLayer = 0;
	first_color_level_subresource_range.layerCount = 1;
	vk_cmd_image_barrier_gpu_locked(vk, cmdBuffer, ct->images[index].handle, 0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL, first_color_level_subresource_range);

	res = vk->vkEndCommandBuffer(cmdBuffer);
	vk_check_error("vkEndCommandBuffer", res, res);
	cmdBuffers.push_back(cmdBuffer);

	//if (cn->host)
	//	cn->host->flush_stream(cn->host);

	for (auto & encoder: cn->encoders)
	{
		VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
		encoder->PresentImage(index, &cmdBuffer);
		if (cmdBuffer != VK_NULL_HANDLE)
			cmdBuffers.push_back(cmdBuffer);
	}

	VkPipelineStageFlags sem_flags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	VkSubmitInfo submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = NULL;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &cn->semaphores.render_complete;
	submit.pWaitDstStageMask = &sem_flags;
	submit.commandBufferCount = cmdBuffers.size();
	submit.pCommandBuffers = cmdBuffers.data();
	submit.signalSemaphoreCount = 0;
	submit.pSignalSemaphores = NULL;

	std::lock_guard lock(cn->psc.mutex);
	res = vk->vkResetFences(vk->device, 1, &cn->psc.images[index].fence);
	vk_check_error("vkResetFences", res, res);
	res = vk_locked_submit(vk, vk->queue, 1, &submit, cn->psc.images[index].fence);
	vk_check_error("vk_locked_submit", res, res);

	assert(cn->psc.images[index].status == image_acquired);
	cn->frame_index++;
	// set bits to 1 for index 1..num encoder threads + 1
	cn->psc.images[index].status = (1 << (cn->encoder_threads.size() + 1)) - 2;
	cn->psc.images[index].frame_index = cn->frame_index;

	auto & view_info = cn->psc.images[index].view_info;
	view_info.display_time = desired_present_time_ns+present_slop_ns;//1;//cn->cnx->get_offset().to_headset(desired_present_time_ns).count();
	for (int eye = 0; eye < 2; ++eye)
	{
		xrt_relation_chain xrc{};
		xrt_space_relation result{};
		m_relation_chain_push_pose_if_not_identity(&xrc, &cn->c->base.slot.poses[eye]);
		m_relation_chain_push_relation(&xrc, &cn->c->base.slot.head_relation);
		m_relation_chain_resolve(&xrc, &result);
		view_info.fov[eye] = xrt_cast(cn->c->base.slot.fovs[eye]);
		view_info.pose[eye] = xrt_cast(result.pose);
	}
	cn->psc.cv.notify_all();

	return VK_SUCCESS;
}

static void comp_ql_flush(struct comp_target * ct)
{
	(void)ct;
}

// Helper function for clamping the time averages
static int64_t comp_ql_clamp_delta_ts(int64_t ts, int64_t min_ts, int64_t max_ts, int64_t max_ts_sub)
{
	if (ts < min_ts) {
		return min_ts;
	}
	if (ts > max_ts) {
		return max_ts_sub;
	}
	return ts;
}

static void comp_ql_calc_frame_pacing(struct comp_target * ct,
                                         int64_t * out_frame_id,
                                         uint64_t * out_wake_up_time_ns,
                                         uint64_t * out_desired_present_time_ns,
                                         uint64_t * out_present_slop_ns,
                                         uint64_t * out_predicted_display_time_ns)
{
	struct ql_comp_target* cn = (struct ql_comp_target *)ct;

	//
	// Weighted slightly heavier towards larger amounts, so if the user is rotating 
	// their head a lot, we will err towards longer encodes/transmits
	//

	// For encoding, we take the longest start-end difference
	int64_t avg_encode = cn->last_avg_enc;
	for (int i = 0; i < QL_SWAPCHAIN_DEPTH; i++)
	{
		int64_t largest_enc_diff = 0;
		for (int j = 0; j < QL_NUM_SLICES; j++)
		{
			int64_t enc_diff = cn->host->encode_duration_ns[QL_IDX_SLICE(j,i)];//cn->host->encode_done_ns[i] - cn->host->encode_started_ns[i];
			if (enc_diff > largest_enc_diff) {
				largest_enc_diff = enc_diff;
			}
		}
		avg_encode += largest_enc_diff;
		avg_encode /= 2;
	}

	// For transmission, we sum all of the transmission times across slices
	int64_t avg_tx = cn->last_avg_tx;
	for (int i = 0; i < QL_SWAPCHAIN_DEPTH; i++)
	{
		int64_t full_tx_time = 0;
		for (int j = 0; j < QL_NUM_SLICES; j++)
		{
			int64_t tx_diff = cn->host->tx_duration_ns[QL_IDX_SLICE(j,i)];
			full_tx_time += tx_diff;
		}
		avg_tx += full_tx_time;
		avg_tx /= 2;
	}

	// If tx or enc takes longer than 10ms, weight lower so that
	// it hopefully recovers to a normal value quicker?
	avg_tx = comp_ql_clamp_delta_ts(avg_tx, 0, U_TIME_1MS_IN_NS * 10, 5 * U_TIME_1MS_IN_NS);
	avg_encode = comp_ql_clamp_delta_ts(avg_encode, 0, U_TIME_1MS_IN_NS * 10, 5 * U_TIME_1MS_IN_NS);

	cn->last_avg_tx = avg_tx;
	cn->last_avg_enc = avg_encode;
	
	//static int64_t add_test = 0;
	static int limit_info_spam = 0;
	if (++limit_info_spam > 100) {
		QUEST_LINK_INFO("Avg: tx %fms, encode %fms, add %fms", (double)avg_tx / 1000000.0, (double)avg_encode / 1000000.0, (double)cn->host->add_test / 1000000.0);
		limit_info_spam = 0;

		//add_test += (1 * U_TIME_HALF_MS_IN_NS);
	}

	int64_t encode_display_delay = avg_encode + avg_tx + cn->host->add_test;// + (1 * U_TIME_HALF_MS_IN_NS) + (5 * U_TIME_1MS_IN_NS);

	int64_t frame_id = ++cn->current_frame_id; //-1;
	uint64_t now_ns = os_monotonic_get_ns();
	uint64_t desired_present_time_ns = now_ns + (U_TIME_1S_IN_NS / (cn->fps));
	uint64_t wake_up_time_ns = desired_present_time_ns - 5 * U_TIME_1MS_IN_NS - encode_display_delay;
	uint64_t present_slop_ns = encode_display_delay;//U_TIME_HALF_MS_IN_NS;
	uint64_t predicted_display_time_ns = desired_present_time_ns + encode_display_delay;

	uint64_t predicted_display_period_ns = U_TIME_1S_IN_NS / (cn->fps) + encode_display_delay;
	uint64_t min_display_period_ns = predicted_display_period_ns;
	
	//u_pc_update_present_offset(cn->upc, frame_id, encode_display_delay);

	u_pc_predict(cn->upc,                      //
	             now_ns,                       //
	             &frame_id,                    //
	             &wake_up_time_ns,             //
	             &desired_present_time_ns,     //
	             &present_slop_ns,             //
	             &predicted_display_time_ns,   //
	             &predicted_display_period_ns, //
	             &min_display_period_ns);      //

	cn->current_frame_id = frame_id;

	*out_frame_id = frame_id;
	*out_wake_up_time_ns = wake_up_time_ns;
	*out_desired_present_time_ns = desired_present_time_ns;
	*out_predicted_display_time_ns = predicted_display_time_ns;
	*out_present_slop_ns = present_slop_ns;


	cn->c->base.base.base.never_repeat_frames = true;
}

static void comp_ql_mark_timing_point(struct comp_target * ct,
                                         enum comp_target_timing_point point,
                                         int64_t frame_id,
                                         uint64_t when_ns)
{
	struct ql_comp_target * cn = (struct ql_comp_target *)ct;
	assert(frame_id == cn->current_frame_id);

	switch (point)
	{
		case COMP_TARGET_TIMING_POINT_WAKE_UP:
			u_pc_mark_point(cn->upc, U_TIMING_POINT_WAKE_UP, cn->current_frame_id, when_ns);
			break;
		case COMP_TARGET_TIMING_POINT_BEGIN:
			u_pc_mark_point(cn->upc, U_TIMING_POINT_BEGIN, cn->current_frame_id, when_ns);
			break;
		case COMP_TARGET_TIMING_POINT_SUBMIT:
			u_pc_mark_point(cn->upc, U_TIMING_POINT_SUBMIT, cn->current_frame_id, when_ns);
			break;
		default:
			assert(false);
	}
}

static VkResult comp_ql_update_timings(struct comp_target * ct)
{
	// TODO
	return VK_SUCCESS;
}

static void comp_ql_set_title(struct comp_target * ct, const char * title)
{}

static void comp_ql_destroy(struct comp_target * ct)
{
	struct ql_comp_target * cn = (struct ql_comp_target *)ct;

	vk_bundle * vk = get_vk(cn);
	vk->vkDeviceWaitIdle(vk->device);
	destroy_images(cn);

	delete cn;
}

static void comp_ql_info_gpu(struct comp_target * ct, int64_t frame_id, uint64_t gpu_start_ns, uint64_t gpu_end_ns, uint64_t when_ns)
{
	COMP_TRACE_MARKER();

	struct ql_comp_target * cn = (struct ql_comp_target *)ct;

	u_pc_info_gpu(cn->upc, frame_id, gpu_start_ns, gpu_end_ns, when_ns);
}

/*
 *
 * 'Exported' functions.
 *
 */

extern "C" comp_target * comp_target_ql_create(struct ql_xrsp_host* host, float fps)
{
	ql_comp_target * cn = new ql_comp_target{};

	cn->host = host;
	//cn->cnx = NULL;
	cn->check_ready = comp_ql_check_ready;
	cn->create_images = comp_ql_create_images;
	cn->has_images = comp_ql_has_images;
	cn->acquire = comp_ql_acquire;
	cn->present = comp_ql_present;
	cn->calc_frame_pacing = comp_ql_calc_frame_pacing;
	cn->mark_timing_point = comp_ql_mark_timing_point;
	cn->update_timings = comp_ql_update_timings;
	cn->info_gpu = comp_ql_info_gpu;
	cn->destroy = comp_ql_destroy;
	cn->init_pre_vulkan = comp_ql_init_pre_vulkan;
	cn->init_post_vulkan = comp_ql_init_post_vulkan;
	cn->set_title = comp_ql_set_title;
	cn->flush = comp_ql_flush;
	cn->fps = fps;

	return cn;
}
}