// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Two call helper functions.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#define OXR_TWO_CALL_HELPER(log, cnt_input, cnt_output, output, count, data)   \
	do {                                                                   \
		if (cnt_output != NULL) {                                      \
			*cnt_output = count;                                   \
		}                                                              \
		if (cnt_input == 0) {                                          \
			return XR_SUCCESS;                                     \
		}                                                              \
		if (cnt_input < count) {                                       \
			return oxr_error(log, XR_ERROR_SIZE_INSUFFICIENT,      \
			                 #cnt_input);                          \
		}                                                              \
		for (uint32_t i = 0; i < count; i++) {                         \
			(output)[i] = (data)[i];                               \
		}                                                              \
		return XR_SUCCESS;                                             \
	} while (false)



#ifdef __cplusplus
}
#endif
