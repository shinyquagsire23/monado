// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common interface to probe for devices.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_config.h"
#include "xrt/xrt_device.h"
#include "os/os_hid.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Prober and device manager.
 *
 */

struct xrt_prober;
struct xrt_prober_device;

/*!
 * The maximum number of devices that a single "found" function called by the
 * prober can create per-call.
 *
 * @ingroup xrt_iface
 */
#define XRT_MAX_DEVICES_PER_PROBE 16

/*!
 * Entry for a single device.
 *
 * @ingroup xrt_iface
 */
struct xrt_prober_entry
{
	uint16_t vendor_id;
	uint16_t product_id;

	int (*found)(struct xrt_prober *xp,
	             struct xrt_prober_device **devices,
	             size_t index,
	             struct xrt_device **out_xdevs);

	const char *name;
};

/*!
 * Function for creating a auto prober.
 *
 * @ingroup xrt_iface
 */
typedef struct xrt_auto_prober *(*xrt_auto_prober_creator)();

/*!
 * Main root of all of the probing device.
 *
 * @ingroup xrt_iface
 */
struct xrt_prober_entry_lists
{
	/*!
	 * A a null terminated list of null terminated lists of
	 * @ref xrt_prober_entry.
	 */
	struct xrt_prober_entry **entries;

	/*!
	 * A null terminated list of @ref xrt_auto_prober creation functions.
	 */
	xrt_auto_prober_creator *auto_probers;

	/*!
	 * Allows you to chain multiple prober entry lists.
	 */
	struct xrt_prober_entry_lists *next;
};

/*!
 * Bus type of a device.
 */
enum xrt_bus_type
{
	XRT_BUS_TYPE_UNKNOWN,
	XRT_BUS_TYPE_USB,
	XRT_BUS_TYPE_BLUETOOTH,
};

/*!
 * A probed device, may or may not be opened.
 *
 * @ingroup xrt_iface
 */
struct xrt_prober_device
{
	uint16_t vendor_id;
	uint16_t product_id;

	enum xrt_bus_type bus;
};

/*!
 * The main prober that probes and manages found but not opened HMD devices
 * that are connected to the system.
 *
 * @ingroup xrt_iface
 */
struct xrt_prober
{
	int (*probe)(struct xrt_prober *xp);
	int (*dump)(struct xrt_prober *xp);
	int (*select)(struct xrt_prober *xp,
	              struct xrt_device **xdevs,
	              size_t num_xdevs);
	int (*open_hid_interface)(struct xrt_prober *xp,
	                          struct xrt_prober_device *xpdev,
	                          int interface,
	                          struct os_hid_device **out_hid_dev);
	void (*destroy)(struct xrt_prober **xp);
};

/*!
 * Call this function to create the @ref xrt_prober. This function is setup in
 * the the very small target wrapper.c for each binary.
 *
 * @ingroup xrt_iface
 */
int
xrt_prober_create(struct xrt_prober **out_prober);

/*!
 * Used by the target binary to create the prober with a list of drivers.
 *
 * @ingroup xrt_iface
 */
int
xrt_prober_create_with_lists(struct xrt_prober **out_prober,
                             struct xrt_prober_entry_lists *list);


/*
 *
 * Auto prober.
 *
 */

/*!
 * A simple prober to probe for a HMD device connected to the system.
 *
 * @ingroup xrt_iface
 */
struct xrt_auto_prober
{
	struct xrt_device *(*lelo_dallas_autoprobe)(
	    struct xrt_auto_prober *xdev);
	void (*destroy)(struct xrt_auto_prober *xdev);
};

/*!
 * Call this function to create the @ref xrt_auto_prober. This function is setup
 * in the the very small target wrapper.c for each binary.
 *
 * @ingroup xrt_iface
 */
struct xrt_auto_prober *
xrt_auto_prober_create();


#ifdef __cplusplus
}
#endif
