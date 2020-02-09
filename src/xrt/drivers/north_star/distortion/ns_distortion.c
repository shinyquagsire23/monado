// Copyright 2020, Collabora, Ltd.
// Copyright 2020, Nova King.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  North Star HMD code.
 * @author Nova King <technobaboo@gmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ns
 */


#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../ns_hmd.h"

void
ns_display_uv_to_render_uv(struct ns_uv display_uv,
                           struct ns_uv *render_uv,
                           struct ns_eye eye)
{
	std::map<float, std::map<float, struct ns_uv> >::iterator outerIter;
}
