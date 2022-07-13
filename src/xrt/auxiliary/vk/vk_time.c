// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan timestamp helpers.
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_vk
 */

#include "os/os_time.h"
#include "math/m_mathinclude.h"

#include "vk/vk_helpers.h"


#ifdef VK_EXT_calibrated_timestamps

/*
 *
 * Helper(s)
 *
 */

uint64_t
from_host_ticks_to_host_ns(uint64_t ticks)
{
#if defined(XRT_OS_LINUX)

	// No-op on Linux.
	return ticks;

#elif defined(XRT_OS_WINDOWS)

	static int64_t ns_per_qpc_tick = 0;
	if (ns_per_qpc_tick == 0) {
		// Fixed at startup, so we can cache this.
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		ns_per_qpc_tick = U_1_000_000_000 / freq.QuadPart;
	}

	return ticks / ns_per_qpc_tick;

#else
#error "Vulkan timestamp domain needs porting"
#endif
}


/*
 *
 * 'Exported' function(s).
 *
 */

XRT_CHECK_RESULT VkResult
vk_convert_timestamps_to_host_ns(struct vk_bundle *vk, uint32_t count, uint64_t *in_out_timestamps)
{
	VkResult ret;

	if (!vk->has_EXT_calibrated_timestamps) {
		VK_ERROR(vk, "VK_EXT_calibrated_timestamps not enabled");
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}

#if defined(XRT_OS_LINUX)
#define CPU_TIME_DOMAIN VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT
#elif defined(XRT_OS_WINDOWS)
#define CPU_TIME_DOMAIN VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT
#else
#error "Vulkan timestamp domain needs porting"
#endif

	// Will always be the same, can be static.
	static const VkCalibratedTimestampInfoEXT timestamp_info[2] = {
	    {
	        .sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT,
	        .pNext = NULL,
	        .timeDomain = VK_TIME_DOMAIN_DEVICE_EXT,
	    },
	    {
	        .sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT,
	        .pNext = NULL,
	        .timeDomain = CPU_TIME_DOMAIN,
	    },
	};

	assert(vk->vkGetCalibratedTimestampsEXT != NULL);

	uint64_t timestamps[2];
	uint64_t max_deviation;

	ret = vk->vkGetCalibratedTimestampsEXT( //
	    vk->device,                         // device
	    2,                                  // timestampCount
	    timestamp_info,                     // pTimestampInfos
	    timestamps,                         // pTimestamps
	    &max_deviation);                    // pMaxDeviation
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkGetCalibratedTimestampsEXT: %s", vk_result_string(ret));
		return ret;
	}

	uint64_t now_ticks = timestamps[0];
	uint64_t now_ns = from_host_ticks_to_host_ns(timestamps[1]);

	// Yupp it's floating point.
	double period = (double)vk->features.timestamp_period;
	uint64_t shift = vk->features.timestamp_valid_bits;

	for (uint32_t i = 0; i < count; i++) {
		// Read the GPU domain timestamp.
		uint64_t timestamp_gpu_ticks = in_out_timestamps[i];

		// The GPU timestamp has rolled over, move now to new epoch.
		if (timestamp_gpu_ticks > now_ticks) {
			/*
			 * If there are 64 bits of timestamps data then this
			 * code doesn't work, but we shouldn't be here then.
			 */
			assert(vk->features.timestamp_valid_bits < 64);
			now_ticks += (uint64_t)1 << shift;
		}
		assert(now_ticks > timestamp_gpu_ticks);

		/*
		 * Since these two timestamps should be close to each other and
		 * therefore a small value a double floating point value should
		 * be able to hold the value "safely".
		 */
		double diff_ticks_f = (double)(now_ticks - timestamp_gpu_ticks);

		// Convert into nanoseconds.
		int64_t diff_ns = (int64_t)floor(diff_ticks_f * period + 0.5);
		assert(diff_ns > 0);

		// And with the diff we can get the timestamp.
		uint64_t timestamp_ns = now_ns - (uint64_t)diff_ns;

		// Write out the result.
		in_out_timestamps[i] = timestamp_ns;
	}

	return VK_SUCCESS;
}

#endif /* VK_EXT_calibrated_timestamps */
