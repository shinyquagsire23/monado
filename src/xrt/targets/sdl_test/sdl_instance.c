// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared default implementation of the instance with compositor.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_system.h"
#include "xrt/xrt_instance.h"
#include "xrt/xrt_config_drivers.h"

#include "util/u_misc.h"
#include "util/u_trace_marker.h"

#include "sdl_internal.h"

#if 0 && defined(XRT_BUILD_DRIVER_SIMULATED)
#include "simulated/simulated_interface.h"
#define USE_SIMULATED
#endif


/*
 *
 * System devices functions.
 *
 */

static void
sdl_system_devices_destroy(struct xrt_system_devices *xsysd)
{
	struct sdl_program *sp = from_xsysd(xsysd);

	for (size_t i = 0; i < xsysd->xdev_count; i++) {
		xrt_device_destroy(&xsysd->xdevs[i]);
	}

	(void)sp; // We are apart of sdl_program, do not free.
}


/*
 *
 * Instance functions.
 *
 */

static xrt_result_t
sdl_instance_get_prober(struct xrt_instance *xinst, struct xrt_prober **out_xp)
{
	return XRT_ERROR_PROBER_NOT_SUPPORTED;
}

static xrt_result_t
sdl_instance_create_system(struct xrt_instance *xinst,
                           struct xrt_system_devices **out_xsysd,
                           struct xrt_system_compositor **out_xsysc)
{
	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);
	assert(out_xsysc == NULL || *out_xsysc == NULL);

	struct sdl_program *sp = from_xinst(xinst);

	*out_xsysd = &sp->xsysd_base;

	// Early out if we only want devices.
	if (out_xsysc == NULL) {
		return XRT_SUCCESS;
	}

	sdl_compositor_create_system(sp, out_xsysc);

	return XRT_SUCCESS;
}

static void
sdl_instance_destroy(struct xrt_instance *xinst)
{
	struct sdl_program *sp = from_xinst(xinst);

	// Frees struct.
	sdl_program_plus_destroy(sp->spp);
}


/*
 *
 * Exported function(s).
 *
 */

void
sdl_system_devices_init(struct sdl_program *sp)
{
	sp->xsysd_base.destroy = sdl_system_devices_destroy;

#ifdef USE_SIMULATED
	struct xrt_device *head = simulated_hmd_create();
#else
	struct xrt_device *head = &sp->xdev_base;
#endif

	// Setup the device base as the only device.
	sp->xsysd_base.xdevs[0] = head;
	sp->xsysd_base.xdev_count = 1;
	sp->xsysd_base.roles.head = head;
}

void
sdl_instance_init(struct sdl_program *sp)
{
	sp->xinst_base.create_system = sdl_instance_create_system;
	sp->xinst_base.get_prober = sdl_instance_get_prober;
	sp->xinst_base.destroy = sdl_instance_destroy;
}

xrt_result_t
xrt_instance_create(struct xrt_instance_info *ii, struct xrt_instance **out_xinst)
{
	u_trace_marker_init();

	struct sdl_program *sp = sdl_program_plus_create();

	*out_xinst = &sp->xinst_base;

	return XRT_SUCCESS;
}
