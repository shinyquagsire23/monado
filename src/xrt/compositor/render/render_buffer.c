// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Buffer functions.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_render
 */

#include "main/comp_compositor.h"

#include "render/render_interface.h"

#include <stdio.h>


/*
 *
 * Common helpers.
 *
 */

static VkResult
create_buffer(struct vk_bundle *vk,
              VkBufferUsageFlags usage_flags,
              VkMemoryPropertyFlags memory_property_flags,
              VkDeviceSize size,
              const void *pNext_for_create,
              const void *pNext_for_allocate,
              VkBuffer *out_buffer,
              VkDeviceMemory *out_memory,
              VkDeviceSize *out_alignment,
              VkDeviceSize *out_allocation_size)
{
	VkResult ret;

	// Create the buffer handle.
	VkBufferCreateInfo buffer_info = {
	    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	    .pNext = pNext_for_create,
	    .size = size,
	    .usage = usage_flags,
	};

	VkBuffer buffer = VK_NULL_HANDLE;
	ret = vk->vkCreateBuffer(vk->device,   //
	                         &buffer_info, //
	                         NULL,         //
	                         &buffer);     //
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateBuffer failed: '%s'", vk_result_string(ret));
		return ret;
	}

	// Create the memory backing up the buffer handle.
	VkMemoryRequirements mem_reqs;
	vk->vkGetBufferMemoryRequirements(vk->device, //
	                                  buffer,     //
	                                  &mem_reqs); //

	// Find a memory type index that fits the properties of the buffer.
	uint32_t memory_type_index = 0;
	if (!vk_get_memory_type(vk,                      //
	                        mem_reqs.memoryTypeBits, //
	                        memory_property_flags,   //
	                        &memory_type_index)) {   //
		VK_ERROR(vk, "vk_get_memory_type failed: 'false'\n\tFailed to find a matching memory type.");
		ret = VK_ERROR_OUT_OF_DEVICE_MEMORY;
		goto err_buffer;
	}

	VkMemoryAllocateInfo mem_alloc = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
	    .allocationSize = mem_reqs.size,
	    .memoryTypeIndex = memory_type_index,
	    .pNext = pNext_for_allocate,
	};

	VkDeviceMemory memory = VK_NULL_HANDLE;
	ret = vk->vkAllocateMemory(vk->device, //
	                           &mem_alloc, //
	                           NULL,       //
	                           &memory);   //
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkAllocateMemory failed: '%s'", vk_result_string(ret));
		goto err_buffer;
	}


	// Attach the memory to the buffer object
	ret = vk->vkBindBufferMemory(vk->device, //
	                             buffer,     // buffer
	                             memory,     // memory
	                             0);         // memoryOffset
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkBindBufferMemory failed: '%s'", vk_result_string(ret));
		goto err_memory;
	}

	*out_memory = memory;
	*out_buffer = buffer;
	*out_alignment = mem_reqs.alignment;
	*out_allocation_size = mem_alloc.allocationSize;

	return VK_SUCCESS;


err_memory:
	vk->vkFreeMemory(vk->device, memory, NULL);

err_buffer:
	vk->vkDestroyBuffer(vk->device, buffer, NULL);

	return ret;
}


/*
 *
 * 'Exported' functions.
 *
 */

VkResult
render_buffer_init(struct vk_bundle *vk,
                   struct render_buffer *buffer,
                   VkBufferUsageFlags usage_flags,
                   VkMemoryPropertyFlags memory_property_flags,
                   VkDeviceSize size)
{
	VkResult ret;

	ret = create_buffer(vk,                        //
	                    usage_flags,               // usage_flags
	                    memory_property_flags,     // memory_property_flags
	                    size,                      // size
	                    NULL,                      // pNext for create
	                    NULL,                      // pNext_for_allocate
	                    &buffer->buffer,           // out_buffer
	                    &buffer->memory,           // out_memory
	                    &buffer->alignment,        // out_alignment
	                    &buffer->allocation_size); // out_allocation_size
	if (ret == VK_SUCCESS) {
		buffer->size = size;
	}

	return ret;
}

VkResult
render_buffer_init_exportable(struct vk_bundle *vk,
                              struct render_buffer *buffer,
                              VkBufferUsageFlags usage_flags,
                              VkMemoryPropertyFlags memory_property_flags,
                              VkDeviceSize size)
{
	VkResult ret;

	VkExternalMemoryBufferCreateInfo export_create_info = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
	    .handleTypes = vk_cb_get_buffer_external_handle_type(vk),
	};

	VkExportMemoryAllocateInfo export_alloc_info = {
	    .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR,
	    .pNext = NULL,
	    .handleTypes = vk_cb_get_buffer_external_handle_type(vk),
	};

	ret = create_buffer(vk,                        //
	                    usage_flags,               // usage_flags
	                    memory_property_flags,     // memory_property_flags
	                    size,                      // size
	                    &export_create_info,       // pNext_for_create
	                    &export_alloc_info,        // pNext_for_allocate
	                    &buffer->buffer,           // out_buffer
	                    &buffer->memory,           // out_memory
	                    &buffer->alignment,        // out_alignment
	                    &buffer->allocation_size); // out_allocation_size
	if (ret == VK_SUCCESS) {
		buffer->size = size;
	}

	return ret;
}

void
render_buffer_close(struct vk_bundle *vk, struct render_buffer *buffer)
{
	if (buffer->buffer != VK_NULL_HANDLE) {
		vk->vkDestroyBuffer(vk->device, buffer->buffer, NULL);
	}
	if (buffer->memory != VK_NULL_HANDLE) {
		vk->vkFreeMemory(vk->device, buffer->memory, NULL);
	}

	U_ZERO(buffer);
}

VkResult
render_buffer_map(struct vk_bundle *vk, struct render_buffer *buffer)
{
	return vk->vkMapMemory(vk->device,       //
	                       buffer->memory,   // memory
	                       0,                // offset
	                       VK_WHOLE_SIZE,    // size
	                       0,                // flags
	                       &buffer->mapped); // ppData
}

void
render_buffer_unmap(struct vk_bundle *vk, struct render_buffer *buffer)
{
	if (buffer->mapped != NULL) {
		vk->vkUnmapMemory(vk->device, buffer->memory);
		buffer->mapped = NULL;
	}
}

VkResult
render_buffer_map_and_write(struct vk_bundle *vk, struct render_buffer *buffer, void *data, VkDeviceSize size)
{
	VkResult ret;

	if (size > buffer->allocation_size) {
		VK_ERROR(vk, "Trying to write more the buffer size!");
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}

	if (buffer->mapped == NULL) {
		ret = render_buffer_map(vk, buffer);
		if (ret != VK_SUCCESS) {
			return ret;
		}
	}

	memcpy(buffer->mapped, data, size);

	return VK_SUCCESS;
}

VkResult
render_buffer_write(struct vk_bundle *vk, struct render_buffer *buffer, void *data, VkDeviceSize size)
{
	if (size > buffer->allocation_size) {
		VK_ERROR(vk, "Trying to write more the buffer size!");
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}

	bool mapped = buffer->mapped != NULL;
	if (!mapped) {
		VkResult ret = render_buffer_map(vk, buffer);
		if (ret != VK_SUCCESS) {
			return ret;
		}
	}

	memcpy(buffer->mapped, data, size);

	// Only unmap if we did the mapping.
	if (!mapped) {
		render_buffer_unmap(vk, buffer);
	}

	return VK_SUCCESS;
}
