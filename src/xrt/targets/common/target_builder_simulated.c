// Copyright 2022-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simulated driver builder.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_builders.h"
#include "util/u_config_json.h"
#include "util/u_system_helpers.h"

#include "target_builder_interface.h"

#include "simulated/simulated_interface.h"

#include <assert.h>


#ifndef XRT_BUILD_DRIVER_SIMULATED
#error "Must only be built with XRT_BUILD_DRIVER_SIMULATED set"
#endif

DEBUG_GET_ONCE_BOOL_OPTION(simulated_enabled, "SIMULATED_ENABLE", false)
DEBUG_GET_ONCE_OPTION(simulated_left, "SIMULATED_LEFT", NULL)
DEBUG_GET_ONCE_OPTION(simulated_right, "SIMULATED_RIGHT", NULL)


/*
 *
 * Helper functions.
 *
 */

static const char *driver_list[] = {
    "simulated",
};

struct xrt_device *
create_controller(const char *str,
                  enum xrt_device_type type,
                  const struct xrt_pose *center,
                  struct xrt_tracking_origin *origin)
{
	enum xrt_device_name name = XRT_DEVICE_SIMPLE_CONTROLLER;

	if (str == NULL) {
		return NULL;
	} else if (strcmp(str, "simple") == 0) {
		name = XRT_DEVICE_SIMPLE_CONTROLLER;
		type = XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER; // Override left/right
	} else if (strcmp(str, "wmr") == 0) {
		name = XRT_DEVICE_WMR_CONTROLLER;
	} else if (strcmp(str, "ml2") == 0) {
		name = XRT_DEVICE_ML2_CONTROLLER;
		type = XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER; // Override left/right
	} else {
		U_LOG_E("Unsupported controller '%s' available are: simple, wmr, ml2.", str);
		return NULL;
	}

	return simulated_create_controller(name, type, center, origin);
}


/*
 *
 * Member functions.
 *
 */

static xrt_result_t
simulated_estimate_system(struct xrt_builder *xb,
                          cJSON *config,
                          struct xrt_prober *xp,
                          struct xrt_builder_estimate *estimate)
{
	estimate->certain.head = true;
	estimate->certain.left = true;
	estimate->certain.right = true;
	estimate->priority = -50;

	return XRT_SUCCESS;
}

static xrt_result_t
simulated_open_system(struct xrt_builder *xb,
                      cJSON *config,
                      struct xrt_prober *xp,
                      struct xrt_system_devices **out_xsysd,
                      struct xrt_space_overseer **out_xso)
{
	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);

	const struct xrt_pose head_center = {XRT_QUAT_IDENTITY, {0.0f, 1.6f, 0.0f}}; // "nominal height" 1.6m
	const struct xrt_pose left_center = {XRT_QUAT_IDENTITY, {-0.2f, 1.3f, -0.5f}};
	const struct xrt_pose right_center = {XRT_QUAT_IDENTITY, {0.2f, 1.3f, -0.5f}};
	const enum xrt_device_type left_type = XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER;
	const enum xrt_device_type right_type = XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER;

	const char *left_str = debug_get_option_simulated_left();
	const char *right_str = debug_get_option_simulated_right();

	struct xrt_device *head = simulated_hmd_create(SIMULATED_MOVEMENT_WOBBLE, &head_center);
	struct xrt_device *left = create_controller(left_str, left_type, &left_center, head->tracking_origin);
	struct xrt_device *right = create_controller(right_str, right_type, &right_center, head->tracking_origin);

	// Make the objects be tracked in space.
	//! @todo Make these be a option to the hmd create function, or just make it be there from the start.
	head->orientation_tracking_supported = true;
	head->position_tracking_supported = true;
	//! @todo Create a shared tracking origin on the system devices struct instead.
	head->tracking_origin->type = XRT_TRACKING_TYPE_OTHER; // Just anything other then none.

	struct u_system_devices *usysd = u_system_devices_allocate();
	usysd->base.roles.head = head;
	usysd->base.roles.left = left;
	usysd->base.roles.right = right;

	usysd->base.xdevs[usysd->base.xdev_count++] = head;
	if (left != NULL) {
		usysd->base.xdevs[usysd->base.xdev_count++] = left;
	}
	if (right != NULL) {
		usysd->base.xdevs[usysd->base.xdev_count++] = right;
	}

	*out_xsysd = &usysd->base;
	u_builder_create_space_overseer(&usysd->base, out_xso);

	return XRT_SUCCESS;
}

static void
simulated_destroy(struct xrt_builder *xb)
{
	free(xb);
}


/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_builder *
t_builder_simulated_create(void)
{
	struct xrt_builder *xb = U_TYPED_CALLOC(struct xrt_builder);
	xb->estimate_system = simulated_estimate_system;
	xb->open_system = simulated_open_system;
	xb->destroy = simulated_destroy;
	xb->identifier = "simulated";
	xb->name = "Simulated devices builder";
	xb->driver_identifiers = driver_list;
	xb->driver_identifier_count = ARRAY_SIZE(driver_list);
	xb->exclude_from_automatic_discovery = !debug_get_bool_option_simulated_enabled();

	return xb;
}
