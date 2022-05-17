// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common interface to probe for devices.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_device.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Prober and device manager.
 *
 */

struct cJSON;
typedef struct cJSON cJSON;

struct xrt_fs;
struct xrt_frame_context;
struct xrt_prober;
struct xrt_prober_entry;
struct xrt_prober_device;
struct xrt_prober_entry_lists;
struct xrt_auto_prober;
struct xrt_tracking_factory;
struct xrt_builder;
struct xrt_system_devices;
struct os_hid_device;

/*!
 * The maximum number of devices that a single
 * @ref xrt_prober_entry::found or
 * @ref xrt_auto_prober::lelo_dallas_autoprobe
 * function called by the prober can create per-call.
 *
 * @ingroup xrt_iface
 */
#define XRT_MAX_DEVICES_PER_PROBE 16

/*!
 * The maximum number of @ref xrt_auto_prober instances that can be handled.
 *
 * @ingroup xrt_iface
 */
#define XRT_MAX_AUTO_PROBERS 16

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
	/*!
	 * USB/Bluetooth vendor ID (VID)
	 */
	uint16_t vendor_id;

	/*!
	 * USB/Bluetooth product ID (PID)
	 */
	uint16_t product_id;
	char product_name[XRT_DEVICE_PRODUCT_NAME_LEN];

	/*!
	 * Device bus type
	 */
	enum xrt_bus_type bus;

	/*!
	 * USB device class
	 */
	uint8_t usb_dev_class;
};

/*!
 * Callback for listing video devices.
 *
 * @param xp Prober
 * @param pdev Prober device being iterated
 * @param product Product string, if available
 * @param manufacturer Manufacturer string, if available
 * @param serial Serial number string, if available
 * @param ptr Your opaque userdata pointer as provided to @ref xrt_prober_list_video_devices
 * @ingroup xrt_iface
 */
typedef void (*xrt_prober_list_video_func_t)(struct xrt_prober *xp,
                                             struct xrt_prober_device *pdev,
                                             const char *product,
                                             const char *manufacturer,
                                             const char *serial,
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

	/*!
	 * Enumerate all connected devices, whether or not we have an associated
	 * driver. Can not be called with the device list is locked
	 * @ref xrt_prober::lock_list and @ref xrt_prober::unlock_list.
	 *
	 * This function along with lock/unlock allows a @ref xrt_builder to
	 * re-probe the devices after having opened another device. A bit more
	 * detailed: It can get a list of devices, search it, open the enabling
	 * one, release the list, do a probe, get the list again and re-scan
	 * to detect any additional devices that may show up once the first
	 * device has been been started.
	 *
	 * @see xrt_prober::lock_list, xrt_prober::unlock_list
	 */
	xrt_result_t (*probe)(struct xrt_prober *xp);

	/*!
	 * Locks the prober list of probed devices and returns it.
	 * While locked, calling @ref xrt_prober::probe is forbidden. Not thread safe.
	 *
	 * See @ref xrt_prober::probe for more detailed expected usage.
	 *
	 * @see xrt_prober::probe, xrt_prober::unlock_list
	 */
	xrt_result_t (*lock_list)(struct xrt_prober *xp,
	                          struct xrt_prober_device ***out_devices,
	                          size_t *out_device_count);

	/*!
	 * Unlocks the list, allowing for @ref xrt_prober::probe to be called.
	 * Takes a pointer to the list pointer and clears it. Not thread safe.
	 * See @ref xrt_prober::probe for more detailed expected usage.
	 *
	 * @see xrt_prober::probe, xrt_prober::lock_list
	 */
	xrt_result_t (*unlock_list)(struct xrt_prober *xp, struct xrt_prober_device ***devices);

	/*!
	 * Dump a listing of all devices found on the system to platform
	 * dependent output (stdout).
	 *
	 * @note Code consuming this interface should use xrt_prober_dump()
	 */
	int (*dump)(struct xrt_prober *xp);

	/*!
	 * Create system devices.
	 *
	 * @param[in]  xp        Prober self parameter.
	 * @param[out] out_xsysd Return of system devices, the pointed pointer must be NULL.
	 *
	 * @note Code consuming this interface should use xrt_prober_create_system()
	 */
	xrt_result_t (*create_system)(struct xrt_prober *xp, struct xrt_system_devices **out_xsysd);

	/*!
	 * Iterate through drivers (by ID and auto-probers) checking to see if
	 * they can handle any connected devices from the last xrt_prober::probe
	 * call, opening those devices to create instances of xrt_device
	 * implementations.
	 *
	 * If no HMD (not even a simulated HMD) is found, then no devices will be
	 * returned (all xdevs will be NULL). Otherwise, xdevs will be populated
	 * with the HMD in xdevs[0], and any subsequent non-NULL values
	 * referring to additional non-HMD devices.
	 *
	 * @param xp Pointer to self
	 * @param[in,out] xdevs Pointer to xrt_device array. Array elements will
	 * be populated.
	 * @param[in] xdev_capacity The capacity of the @p xdevs array.
	 *
	 * @return 0 on success (including "no HMD found"), <0 on error.
	 *
	 * Returned devices have their ownership transferred to the caller: all
	 * should be cleaned up with xrt_device_destroy().
	 *
	 * @note Code consuming this interface should use
	 * xrt_prober_select(). Typically used through an xrt_instance and the
	 * xrt_instance_select() method which usually calls xrt_prober_probe()
	 * and xrt_prober_select().
	 */
	int (*select)(struct xrt_prober *xp, struct xrt_device **xdevs, size_t xdev_capacity);

	/*!
	 * Open a HID (Human Interface Device) interface using native HID support.
	 *
	 * @param xp Pointer to self
	 * @param xpdev prober device
	 * @param iface HID interface number
	 * @param[out] out_hid_dev instance of @ref os_hid_device for the given interface
	 *
	 * @return 0 on success, <0 on error.
	 */
	int (*open_hid_interface)(struct xrt_prober *xp,
	                          struct xrt_prober_device *xpdev,
	                          int iface,
	                          struct os_hid_device **out_hid_dev);

	/*!
	 * Opens the selected video device and returns a @ref xrt_fs, does not
	 * start it.
	 */
	int (*open_video_device)(struct xrt_prober *xp,
	                         struct xrt_prober_device *xpdev,
	                         struct xrt_frame_context *xfctx,
	                         struct xrt_fs **out_xfs);

	/*!
	 * Iterate through available video devices, calling your callback @p cb with your userdata @p ptr.
	 *
	 * @param xp Pointer to self
	 * @param cb Callback function
	 * @param ptr Opaque pointer for your userdata, passed through to the callback.
	 *
	 * @see xrt_prober_list_video_func_t
	 * @return 0 on success, <0 on error.
	 */
	int (*list_video_devices)(struct xrt_prober *xp, xrt_prober_list_video_func_t cb, void *ptr);

	/*!
	 * Retrieve the raw @ref xrt_prober_entry and @ref xrt_auto_prober arrays.
	 *
	 * @param xp Pointer to self
	 * @param[out] out_entry_count The size of @p out_entries
	 * @param[out] out_entries An array of prober entries
	 * @param[out] out_auto_probers An array of up to @ref XRT_MAX_AUTO_PROBERS auto-probers
	 *
	 * @return 0 on success, <0 on error.
	 */
	int (*get_entries)(struct xrt_prober *xp,
	                   size_t *out_entry_count,
	                   struct xrt_prober_entry ***out_entries,
	                   struct xrt_auto_prober ***out_auto_probers);

	/*!
	 * Returns a string property on the device of the given type
	 * @p which_string in @p out_buffer.
	 *
	 * @param[in] xp             Prober.
	 * @param[in] xpdev          Device to get string property from.
	 * @param[in] which_string   Which string property to query.
	 * @param[in,out] out_buffer Target buffer.
	 * @param[in] max_length     Max length of the target buffer.
	 *
	 * @return The length of the string, or negative on error.
	 *
	 */
	int (*get_string_descriptor)(struct xrt_prober *xp,
	                             struct xrt_prober_device *xpdev,
	                             enum xrt_prober_string which_string,
	                             unsigned char *out_buffer,
	                             size_t max_length);

	/*!
	 * Determine whether a prober device can be opened.
	 *
	 * @param xp Pointer to self
	 * @param xpdev prober device
	 *
	 * @return true if @p xpdev can be opened.
	 */
	bool (*can_open)(struct xrt_prober *xp, struct xrt_prober_device *xpdev);

	/*!
	 * Destroy the prober and set the pointer to null.
	 *
	 * Code consuming this interface should use xrt_prober_destroy().
	 *
	 * @param xp_ptr pointer to self-pointer
	 */
	void (*destroy)(struct xrt_prober **xp_ptr);
};

/*!
 * @copydoc xrt_prober::probe
 *
 * Helper function for @ref xrt_prober::probe.
 *
 * @public @memberof xrt_prober
 */
static inline xrt_result_t
xrt_prober_probe(struct xrt_prober *xp)
{
	return xp->probe(xp);
}

/*!
 * @copydoc xrt_prober::lock_list
 *
 * Helper function for @ref xrt_prober::lock_list.
 *
 * @public @memberof xrt_prober
 */
static inline xrt_result_t
xrt_prober_lock_list(struct xrt_prober *xp, struct xrt_prober_device ***out_devices, size_t *out_device_count)
{
	return xp->lock_list(xp, out_devices, out_device_count);
}

/*!
 * @copydoc xrt_prober::unlock_list
 *
 * Helper function for @ref xrt_prober::unlock_list.
 *
 * @public @memberof xrt_prober
 */
static inline xrt_result_t
xrt_prober_unlock_list(struct xrt_prober *xp, struct xrt_prober_device ***devices)
{
	return xp->unlock_list(xp, devices);
}

/*!
 * @copydoc xrt_prober::dump
 *
 * Helper function for @ref xrt_prober::dump.
 *
 * @public @memberof xrt_prober
 */
static inline int
xrt_prober_dump(struct xrt_prober *xp)
{
	return xp->dump(xp);
}

/*!
 * @copydoc xrt_prober::create_system
 *
 * Helper function for @ref xrt_prober::create_system.
 *
 * @public @memberof xrt_prober
 */
static inline xrt_result_t
xrt_prober_create_system(struct xrt_prober *xp, struct xrt_system_devices **out_xsysd)
{
	return xp->create_system(xp, out_xsysd);
}

/*!
 * @copydoc xrt_prober::select
 *
 * Helper function for @ref xrt_prober::select.
 *
 * @public @memberof xrt_prober
 */
static inline int
xrt_prober_select(struct xrt_prober *xp, struct xrt_device **xdevs, size_t xdev_capacity)
{
	return xp->select(xp, xdevs, xdev_capacity);
}

/*!
 * @copydoc xrt_prober::open_hid_interface
 *
 * Helper function for @ref xrt_prober::open_hid_interface.
 *
 * @public @memberof xrt_prober
 */
static inline int
xrt_prober_open_hid_interface(struct xrt_prober *xp,
                              struct xrt_prober_device *xpdev,
                              int iface,
                              struct os_hid_device **out_hid_dev)
{
	return xp->open_hid_interface(xp, xpdev, iface, out_hid_dev);
}

/*!
 * @copydoc xrt_prober::get_string_descriptor
 *
 * Helper function for @ref xrt_prober::get_string_descriptor.
 *
 * @public @memberof xrt_prober
 */
static inline int
xrt_prober_get_string_descriptor(struct xrt_prober *xp,
                                 struct xrt_prober_device *xpdev,
                                 enum xrt_prober_string which_string,
                                 unsigned char *out_buffer,
                                 size_t max_length)
{
	return xp->get_string_descriptor(xp, xpdev, which_string, out_buffer, max_length);
}

/*!
 * @copydoc xrt_prober::can_open
 *
 * Helper function for @ref xrt_prober::can_open.
 *
 * @public @memberof xrt_prober
 */
static inline bool
xrt_prober_can_open(struct xrt_prober *xp, struct xrt_prober_device *xpdev)
{
	return xp->can_open(xp, xpdev);
}


/*!
 * @copydoc xrt_prober::open_video_device
 *
 * Helper function for @ref xrt_prober::xrt_prober_open_video_device.
 *
 * @public @memberof xrt_prober
 */
static inline int
xrt_prober_open_video_device(struct xrt_prober *xp,
                             struct xrt_prober_device *xpdev,
                             struct xrt_frame_context *xfctx,
                             struct xrt_fs **out_xfs)
{
	return xp->open_video_device(xp, xpdev, xfctx, out_xfs);
}

/*!
 * @copydoc xrt_prober::list_video_devices
 *
 * Helper function for @ref xrt_prober::list_video_devices.
 *
 * @public @memberof xrt_prober
 */
static inline int
xrt_prober_list_video_devices(struct xrt_prober *xp, xrt_prober_list_video_func_t cb, void *ptr)
{
	return xp->list_video_devices(xp, cb, ptr);
}

/*!
 * @copydoc xrt_prober::get_entries
 *
 * Helper function for @ref xrt_prober::get_entries.
 *
 * @public @memberof xrt_prober
 */
static inline int
xrt_prober_get_entries(struct xrt_prober *xp,
                       size_t *out_entry_count,
                       struct xrt_prober_entry ***out_entries,
                       struct xrt_auto_prober ***out_auto_probers)
{
	return xp->get_entries(xp, out_entry_count, out_entries, out_auto_probers);
}

/*!
 * @copydoc xrt_prober::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * xp_ptr to null if freed.
 *
 * @public @memberof xrt_prober
 */
static inline void
xrt_prober_destroy(struct xrt_prober **xp_ptr)
{
	struct xrt_prober *xp = *xp_ptr;
	if (xp == NULL) {
		return;
	}

	xp->destroy(xp_ptr);
	*xp_ptr = NULL;
}


/*
 *
 * Builder interface.
 *
 */

/*!
 * A estimate from a setter upper about how many devices they can open.
 *
 * @ingroup xrt_iface
 */
struct xrt_builder_estimate
{
	struct
	{
		bool head;
		bool left;
		bool right;
		bool dof6;
		uint32_t extra_device_count;
	} certain, maybe;

	/*!
	 * A setter upper defined priority, mostly for vive vs survive.
	 *
	 * 0 normal priority, positive value higher, negative lower.
	 */
	int32_t priority;
};

/*!
 * Function pointer type for creating a @ref xrt_builder.
 *
 * @ingroup xrt_iface
 */
typedef struct xrt_builder *(*xrt_builder_create_func_t)(void);

/*!
 * Sets up a collection of devices and builds a system, a setter upper.
 *
 * @ingroup xrt_iface
 */
struct xrt_builder
{
	//! Short identifier, like "vive", "north_star", "rgb_tracking".
	const char *identifier;

	//! "Localized" pretty name.
	const char *name;

	//! List of identifiers for drivers this setter-upper uses/supports.
	const char **driver_identifiers;

	//! Number of driver identifiers.
	size_t driver_identifier_count;

	//! Should this builder be excluded from automatic discovery.
	bool exclude_from_automatic_discovery;

	/*!
	 * From the devices found, estimate without opening the devices how
	 * good the system will be.
	 *
	 * @param[in]  xb           Builder self parameter.
	 * @param[in]  xp           Prober
	 * @param[in]  config       JSON config object if found for this setter upper.
	 * @param[out] out_estimate Estimate to be filled out.
	 *
	 * @note Code consuming this interface should use xrt_builder_estimate_system()
	 */
	xrt_result_t (*estimate_system)(struct xrt_builder *xb,
	                                cJSON *config,
	                                struct xrt_prober *xp,
	                                struct xrt_builder_estimate *out_estimate);

	/*!
	 * We are now committed to opening these devices.
	 *
	 * @param[in]  xb        Builder self parameter.
	 * @param[in]  xp        Prober
	 * @param[in]  config    JSON config object if found for this setter upper.
	 * @param[out] out_xsysd Return of system devices, the pointed pointer must be NULL.
	 *
	 * @note Code consuming this interface should use xrt_builder_open_system()
	 */
	xrt_result_t (*open_system)(struct xrt_builder *xb,
	                            cJSON *config,
	                            struct xrt_prober *xp,
	                            struct xrt_system_devices **out_xsysd);

	/*!
	 * Destroy this setter upper.
	 *
	 * @note Code consuming this interface should use xrt_builder_destroy()
	 */
	void (*destroy)(struct xrt_builder *xb);
};

/*!
 * @copydoc xrt_builder::estimate_system
 *
 * Helper function for @ref xrt_builder::estimate_system.
 *
 * @public @memberof xrt_builder
 */
static inline xrt_result_t
xrt_builder_estimate_system(struct xrt_builder *xb,
                            cJSON *config,
                            struct xrt_prober *xp,
                            struct xrt_builder_estimate *out_estimate)
{
	return xb->estimate_system(xb, config, xp, out_estimate);
}

/*!
 * @copydoc xrt_builder::open_system
 *
 * Helper function for @ref xrt_builder::open_system.
 *
 * @public @memberof xrt_builder
 */
static inline xrt_result_t
xrt_builder_open_system(struct xrt_builder *xb,
                        cJSON *config,
                        struct xrt_prober *xp,
                        struct xrt_system_devices **out_xsysd)
{
	return xb->open_system(xb, config, xp, out_xsysd);
}

/*!
 * @copydoc xrt_builder::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * xb_ptr to null if freed.
 *
 * @public @memberof xrt_builder
 */
static inline void
xrt_builder_destroy(struct xrt_builder **xb_ptr)
{
	struct xrt_builder *xb = *xb_ptr;
	if (xb == NULL) {
		return;
	}

	xb->destroy(xb);
	*xb_ptr = NULL;
}


/*
 *
 * Found device interface.
 *
 */

/*!
 * Function pointer type for a handler that gets called when a device matching vendor and product ID is detected.
 *
 * @param xp Prober
 * @param devices The array of prober devices found by the prober.
 * @param num_devices The number of elements in @p devices
 * @param index Which element in the prober device array matches your query?
 * @param attached_data
 * @param out_xdevs An empty array of size @p XRT_MAX_DEVICES_PER_PROBE you may populate with @ref xrt_device
 * instances.
 *
 * @return the number of elements of @p out_xdevs populated by this call.
 */
typedef int (*xrt_prober_found_func_t)(struct xrt_prober *xp,
                                       struct xrt_prober_device **devices,
                                       size_t num_devices,
                                       size_t index,
                                       cJSON *attached_data,
                                       struct xrt_device **out_xdevs);

/*!
 * Entry for a single device.
 *
 * @ingroup xrt_iface
 */
struct xrt_prober_entry
{
	/*!
	 * USB/Bluetooth vendor ID (VID) to filter on.
	 */
	uint16_t vendor_id;

	/*!
	 * USB/Bluetooth product ID (PID) to filter on.
	 */
	uint16_t product_id;

	/*!
	 * Handler that gets called when a device matching vendor and product ID is detected.
	 *
	 * @see xrt_prober_found_func_t
	 */
	xrt_prober_found_func_t found;

	/*!
	 * A human-readable name for the device associated with this VID/PID.
	 */
	const char *name;

	/*!
	 * A human-readable name for the driver associated with this VID/PID.
	 *
	 * Separate because a single driver might handle multiple VID/PID entries.
	 */
	const char *driver_name;
};


/*
 *
 * Auto prober.
 *
 */

/*!
 * Function pointer type for creating a auto prober.
 *
 * @ingroup xrt_iface
 */
typedef struct xrt_auto_prober *(*xrt_auto_prober_create_func_t)();

/*!
 * @interface xrt_auto_prober
 *
 * An interface to be exposed by a device driver that should probe for the
 * existence of its own device on the system, rather than using shared probers
 * with vendor/product IDs, etc.
 *
 * @ingroup xrt_iface
 */
struct xrt_auto_prober
{
	const char *name;

	/*!
	 * Do the internal probing that the driver needs to do to find
	 * devices.
	 *
	 * @param xap Self pointer
	 * @param attached_data JSON "attached data" for this device from
	 * config, if any.
	 * @param[in] no_hmds If true, do not probe for HMDs, only other
	 * devices.
	 * @param[in] xp Prober: provided to use the tracking factory,
	 * among other reasons.
	 * @param[out] out_xdevs Array of @ref XRT_MAX_DEVICES_PER_PROBE @c NULL
	 * @ref xrt_device pointers. First elements will be populated with new
	 * devices.
	 *
	 * @return The number of devices written into @p out_xdevs, 0 if none.
	 *
	 * @note Leeloo Dallas is a reference to The Fifth Element.
	 */
	int (*lelo_dallas_autoprobe)(struct xrt_auto_prober *xap,
	                             cJSON *attached_data,
	                             bool no_hmds,
	                             struct xrt_prober *xp,
	                             struct xrt_device **out_xdevs);
	/*!
	 * Destroy this auto-prober.
	 *
	 * @param xap Self pointer
	 */
	void (*destroy)(struct xrt_auto_prober *xap);
};


/*
 *
 * Prober creation.
 *
 */

/*!
 * Main root of all of the probing device.
 *
 * @ingroup xrt_iface
 */
struct xrt_prober_entry_lists
{
	/*!
	 * A null terminated list of @ref xrt_builder creation functions.
	 */
	xrt_builder_create_func_t *builders;

	/*!
	 * A null terminated list of null terminated lists of
	 * @ref xrt_prober_entry.
	 */
	struct xrt_prober_entry **entries;

	/*!
	 * A null terminated list of @ref xrt_auto_prober creation functions.
	 */
	xrt_auto_prober_create_func_t *auto_probers;

	/*!
	 * Lets you chain multiple prober entry lists.
	 */
	struct xrt_prober_entry_lists *next;
};

/*!
 * Create a prober with a list of known devices and autoprobers.
 *
 * Typically used by xrt_instance_create implementations to create the prober,
 * often with a shared list called `target_list`.
 *
 * @param[out] out_xp Pointer to xrt_prober pointer, will be populated with
 * created xrt_prober instance.
 * @param[in] list Prober entry list
 *
 * @public @memberof xrt_prober
 */
int
xrt_prober_create_with_lists(struct xrt_prober **out_xp, struct xrt_prober_entry_lists *list);


#ifdef __cplusplus
}
#endif
