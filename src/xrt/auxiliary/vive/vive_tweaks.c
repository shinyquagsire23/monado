// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tweaks for various bits on Vive and Index headsets.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_vive
 */

#include "xrt/xrt_defines.h"

#include "vive_config.h"
#include "vive_tweaks.h"

#include <assert.h>
#include <string.h>


/*
 *
 * Tweaks for FOV.
 *
 */

struct fov_entry
{
	const char *device_serial_number;
	const struct xrt_fov fovs[2];
};

static const struct fov_entry fovs[1] = {
    {
        .device_serial_number = "LHR-4DC3ADD6",
        .fovs =
            {
                {
                    .angle_left = -0.907983,
                    .angle_right = 0.897738,
                    .angle_up = 0.954823,
                    .angle_down = -0.953044,
                },
                {

                    .angle_left = -0.897050,
                    .angle_right = 0.908661,
                    .angle_up = 0.954474,
                    .angle_down = -0.953057,
                },
            },
    },
};


/*
 *
 * 'Exported' functions.
 *
 */

void
vive_tweak_fov(struct vive_config *config)
{
	const char *device_serial_number = config->firmware.device_serial_number;

	for (size_t i = 0; i < ARRAY_SIZE(fovs); i++) {
		const struct fov_entry *e = &fovs[i];

		if (strcmp(device_serial_number, e->device_serial_number) != 0) {
			continue;
		}

		U_LOG_I("Applying FoV tweaks to device serial '%s'", device_serial_number);

		config->distortion.fov[0] = e->fovs[0];
		config->distortion.fov[1] = e->fovs[1];
	}
}
