// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  The thing that binds all of the OpenXR driver together.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_config_build.h"

#ifdef XRT_FEATURE_SERVICE

#include "xrt/xrt_instance.h"

// Forward declaration
int
ipc_instance_create(struct xrt_instance_info *i_info, struct xrt_instance **out_xinst);

int
xrt_instance_create(struct xrt_instance_info *i_info, struct xrt_instance **out_xinst)
{
	return ipc_instance_create(i_info, out_xinst);
}

#else

/*
 * For non-service runtime, xrt_instance_create defined in target_instance
 * helper lib, so we just have a dummy symbol below to silence warnings about
 * empty translation units.
 */
#include <xrt/xrt_compiler.h>
XRT_MAYBE_UNUSED static const int DUMMY = 42;

#endif
