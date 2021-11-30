// Copyright 2021, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Utility functions for android looper.
 * @author Jarvis Huang
 * @ingroup aux_android
 */

#pragma once

#include <xrt/xrt_config_os.h>

#ifdef XRT_OS_ANDROID

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Poll the looper until activity is in resume state.
 */
void
android_looper_poll_until_activity_resumed();

#ifdef __cplusplus
}
#endif

#endif // XRT_OS_ANDROID
