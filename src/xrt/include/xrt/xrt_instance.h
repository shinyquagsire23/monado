// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header for @ref xrt_instance object.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_defines.h"


#ifdef __cplusplus
extern "C" {
#endif


struct xrt_prober;
struct xrt_device;
struct xrt_system_devices;
struct xrt_system_compositor;


/*!
 * @addtogroup xrt_iface
 * @{
 */

#define XRT_MAX_APPLICATION_NAME_SIZE 128

/*!
 * Information provided by the application at instance create time.
 */
struct xrt_instance_info
{
	char application_name[XRT_MAX_APPLICATION_NAME_SIZE];
};

/*!
 * @interface xrt_instance
 *
 * This interface acts as a root object for Monado.
 * It typically either wraps an @ref xrt_prober or forms a connection to an
 * out-of-process XR service.
 *
 * This is as close to a singleton object as there is in Monado: you should not
 * create more than one xrt_instance implementation per process.
 *
 * Each "target" will provide its own (private) implementation of this
 * interface, which is exposed by implementing xrt_instance_create().
 *
 * Additional information can be found in @ref understanding-targets.
 *
 * @sa ipc_instance_create
 */
struct xrt_instance
{
	/*!
	 * @name Interface Methods
	 *
	 * All implementations of the xrt_instance implementation must
	 * populate all these function pointers with their implementation
	 * methods. To use this interface, see the helper functions.
	 * @{
	 */

	/*!
	 * Creates all of the system resources like the devices and system
	 * compositor. The system compositor is optional.
	 *
	 * Should only be called once.
	 *
	 * @note Code consuming this interface should use xrt_instance_create_system()
	 *
	 * @param      xinst     Pointer to self
	 * @param[out] out_xsysd Return of devices, required.
	 * @param[out] out_xsysc Return of system compositor, optional.
	 *
	 * @see xrt_prober::probe, xrt_prober::select, xrt_gfx_provider_create_native
	 */
	xrt_result_t (*create_system)(struct xrt_instance *xinst,
	                              struct xrt_system_devices **out_xsysd,
	                              struct xrt_system_compositor **out_xsysc);

	/*!
	 * Get the instance @ref xrt_prober, if any.
	 *
	 * If the instance is not using an @ref xrt_prober, it may return null.
	 *
	 * The instance retains ownership of the prober and is responsible for
	 * destroying it.
	 *
	 * Can be called multiple times. (The prober is usually created at
	 * instance construction time.)
	 *
	 * @note Code consuming this interface should use
	 * xrt_instance_get_prober().
	 *
	 * @param xinst Pointer to self
	 * @param[out] out_xp Pointer to xrt_prober pointer, will be populated
	 * or set to NULL.
	 *
	 * @return XRT_SUCCESS on success, other error code on error.
	 */
	xrt_result_t (*get_prober)(struct xrt_instance *xinst, struct xrt_prober **out_xp);

	/*!
	 * Destroy the instance and its owned objects, including the prober (if
	 * any).
	 *
	 * Code consuming this interface should use xrt_instance_destroy().
	 *
	 * @param xinst Pointer to self
	 */
	void (*destroy)(struct xrt_instance *xinst);
	/*!
	 * @}
	 */
	struct xrt_instance_info instance_info;

	uint64_t startup_timestamp;
};

/*!
 * @copydoc xrt_instance::create_system
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_instance
 */
static inline xrt_result_t
xrt_instance_create_system(struct xrt_instance *xinst,
                           struct xrt_system_devices **out_xsysd,
                           struct xrt_system_compositor **out_xsysc)
{
	return xinst->create_system(xinst, out_xsysd, out_xsysc);
}

/*!
 * @copydoc xrt_instance::get_prober
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_instance
 */
static inline xrt_result_t
xrt_instance_get_prober(struct xrt_instance *xinst, struct xrt_prober **out_xp)
{
	return xinst->get_prober(xinst, out_xp);
}

/*!
 * Destroy an xrt_instance - helper function.
 *
 * @param[in,out] xinst_ptr A pointer to your instance implementation pointer.
 *
 * Will destroy the instance if *xinst_ptr is not NULL. Will then set *xinst_ptr
 * to NULL.
 *
 * @public @memberof xrt_instance
 */
static inline void
xrt_instance_destroy(struct xrt_instance **xinst_ptr)
{
	struct xrt_instance *xinst = *xinst_ptr;
	if (xinst == NULL) {
		return;
	}

	xinst->destroy(xinst);
	*xinst_ptr = NULL;
}

/*!
 * @name Factory
 * Implemented in each target.
 * @{
 */
/*!
 * Create an implementation of the xrt_instance interface.
 *
 * Creating more then one @ref xrt_instance is probably never the right thing
 * to do, so avoid it.
 *
 * Each target must implement this function.
 *
 * @param[in] ii A pointer to a info struct containing information about the
 *               application.
 * @param[out] out_xinst A pointer to an xrt_instance pointer. Will be
 * populated.
 *
 * @return 0 on success
 *
 * @relates xrt_instance
 */
xrt_result_t
xrt_instance_create(struct xrt_instance_info *ii, struct xrt_instance **out_xinst);

/*!
 * @}
 */

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
