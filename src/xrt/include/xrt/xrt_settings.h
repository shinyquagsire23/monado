// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common settings structs to be transferred between different parts of
 *         Monadon.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @ingroup xrt_iface
 * @{
 */

/*!
 * Camera type.
 */
enum xrt_settings_camera_type
{
	XRT_SETTINGS_CAMERA_TYPE_REGULAR_MONO = 0,
	XRT_SETTINGS_CAMERA_TYPE_REGULAR_SBS = 1,
	XRT_SETTINGS_CAMERA_TYPE_PS4 = 2,
	XRT_SETTINGS_CAMERA_TYPE_LEAP_MOTION = 3,
};

#define XRT_SETTINGS_CAMERA_NAME_LENGTH 256
#define XRT_SETTINGS_PATH_LENGTH 1024

/*!
 * Holding enough information to recreate a tracking pipeline.
 */
struct xrt_settings_tracking
{
	char camera_name[XRT_SETTINGS_CAMERA_NAME_LENGTH];
	int camera_mode;
	enum xrt_settings_camera_type camera_type;
	char calibration_path[XRT_SETTINGS_PATH_LENGTH];
};

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
