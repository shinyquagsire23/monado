// Copyright 2021, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Implementation of android looper functions.
 * @author Jarvis Huang
 * @ingroup aux_android
 */

#include "android_looper.h"

#include "util/u_logging.h"

#include <android_native_app_glue.h>
#include <android/looper.h>

void
android_looper_poll_until_activity_resumed()
{
	struct android_poll_source *source;
	// Can we assume that activity already resumed if polling is failed?
	while (ALooper_pollAll(-1, NULL, NULL, (void **)&source) == LOOPER_ID_MAIN) {
		if (source) {
			source->process(source->app, source);
			if (source->app->activityState == APP_CMD_RESUME && source->app->window) {
				U_LOG_I("Activity is in resume state and window is ready now");
				break;
			}
		}
	}
}
