// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common interface to probe for devices.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

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

struct xrt_fs;
struct xrt_frame_context;
struct xrt_prober;
struct xrt_prober_device;
struct xrt_tracking_factory;

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
	             size_t num_devices,
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
 * String descriptor types
 */
enum xrt_prober_string
{
	XRT_PROBER_STRING_MANUFACTURER,
	XRT_PROBER_STRING_PRODUCT,
	XRT_PROBER_STRING_SERIAL_NUMBER,
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

	uint8_t usb_dev_class;
};

/*!
 * Callback for listing video devices.
 *
 * @ingroup xrt_iface
 */
typedef void (*xrt_prober_list_video_cb)(struct xrt_prober *xp,
                                         struct xrt_prober_device *pdev,
                                         const char *name,
                                         void *ptr);

/*!
 * The main prober that probes and manages found but not opened HMD devices
 * that are connected to the system.
 *
 * @ingroup xrt_iface
 */
struct xrt_prober
{
	//! Factory for producing tracked objects.
	struct xrt_tracking_factory *tracking;

	int (*probe)(struct xrt_prober *xp);
	int (*dump)(struct xrt_prober *xp);
	int (*select)(struct xrt_prober *xp,
	              struct xrt_device **xdevs,
	              size_t num_xdevs);
	int (*open_hid_interface)(struct xrt_prober *xp,
	                          struct xrt_prober_device *xpdev,
	                          int interface,
	                          struct os_hid_device **out_hid_dev);
	int (*open_video_device)(struct xrt_prober *xp,
	                         struct xrt_prober_device *xpdev,
	                         struct xrt_frame_context *xfctx,
	                         struct xrt_fs **out_xfs);
	int (*list_video_devices)(struct xrt_prober *xp,
	                          xrt_prober_list_video_cb cb,
	                          void *ptr);
	int (*get_string_descriptor)(struct xrt_prober *xp,
	                             struct xrt_prober_device *xpdev,
	                             enum xrt_prober_string which_string,
	                             unsigned char *buffer,
	                             int length);
	bool (*can_open)(struct xrt_prober *xp,
	                 struct xrt_prober_device *xpdev);
	void (*destroy)(struct xrt_prober **xp_ptr);
};

/*!
 * Helper function for @ref xrt_prober::probe.
 *
 * @ingroup xrt_iface
 */
XRT_MAYBE_UNUSED static inline int
xrt_prober_probe(struct xrt_prober *xp)
{
	return xp->probe(xp);
}

/*!
 * Helper function for @ref xrt_prober::dump.
 *
 * @ingroup xrt_iface
 */
XRT_MAYBE_UNUSED static inline int
xrt_prober_dump(struct xrt_prober *xp)
{
	return xp->dump(xp);
}

/*!
 * Helper function for @ref xrt_prober::select.
 *
 * @ingroup xrt_iface
 */
XRT_MAYBE_UNUSED static inline int
xrt_prober_select(struct xrt_prober *xp,
                  struct xrt_device **xdevs,
                  size_t num_xdevs)
{
	return xp->select(xp, xdevs, num_xdevs);
}

/*!
 * Helper function for @ref xrt_prober::open_hid_interface.
 *
 * @ingroup xrt_iface
 */
XRT_MAYBE_UNUSED static inline int
xrt_prober_open_hid_interface(struct xrt_prober *xp,
                              struct xrt_prober_device *xpdev,
                              int interface,
                              struct os_hid_device **out_hid_dev)
{
	return xp->open_hid_interface(xp, xpdev, interface, out_hid_dev);
}

/*!
 * Helper function for @ref xrt_prober::get_string_descriptor.
 *
 * @ingroup xrt_iface
 */
XRT_MAYBE_UNUSED static inline int
xrt_prober_get_string_descriptor(struct xrt_prober *xp,
                                 struct xrt_prober_device *xpdev,
                                 enum xrt_prober_string which_string,
                                 unsigned char *buffer,
                                 int length)
{
	return xp->get_string_descriptor(xp, xpdev, which_string, buffer,
	                                 length);
}

/*!
 * Helper function for @ref xrt_prober::can_open.
 *
 * @ingroup xrt_iface
 */
XRT_MAYBE_UNUSED static inline bool
xrt_prober_can_open(struct xrt_prober *xp, struct xrt_prober_device *xpdev)
{
	return xp->can_open(xp, xpdev);
}


/*!
 * Helper function for @ref xrt_prober::xrt_prober_open_video_device.
 *
 * @ingroup xrt_iface
 */
XRT_MAYBE_UNUSED static inline int
xrt_prober_open_video_device(struct xrt_prober *xp,
                             struct xrt_prober_device *xpdev,
                             struct xrt_frame_context *xfctx,
                             struct xrt_fs **out_xfs)
{
	return xp->open_video_device(xp, xpdev, xfctx, out_xfs);
}

/*!
 * Helper function for @ref xrt_prober::list_video_devices.
 *
 * @ingroup xrt_iface
 */
XRT_MAYBE_UNUSED static inline int
xrt_prober_list_video_devices(struct xrt_prober *xp,
                              xrt_prober_list_video_cb cb,
                              void *ptr)
{
	return xp->list_video_devices(xp, cb, ptr);
}

/*!
 * Helper function for @ref xrt_prober::destroy.
 *
 * @ingroup xrt_iface
 */
XRT_MAYBE_UNUSED static inline void
xrt_prober_destroy(struct xrt_prober **xp_ptr)
{
	struct xrt_prober *xp = *xp_ptr;
	if (xp == NULL) {
		return;
	}

	xp->destroy(xp_ptr);
}

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
	struct xrt_device *(*lelo_dallas_autoprobe)(struct xrt_auto_prober *xap,
	                                            struct xrt_prober *xp);
	void (*destroy)(struct xrt_auto_prober *xdev);
};


#ifdef __cplusplus
}
#endif
