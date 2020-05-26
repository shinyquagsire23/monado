// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header for @ref xrt_instance object.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif


struct xrt_prober;
struct xrt_device;
struct xrt_compositor_fd;

/*!
 * @ingroup xrt_iface
 * @{
 */

/*!
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
 * Additional information can be found in @ref md_targets
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
	 * Returns the devices of the system represented as @ref xrt_device.
	 *
	 * Should only be called once.
	 *
	 * @note Code consuming this interface should use xrt_instance_select()
	 *
	 * @param xinst Pointer to self
	 * @param[in,out] xdevs Pointer to xrt_device array. Array elements will
	 * be populated.
	 * @param[in] num_xdevs The capacity of the @p xdevs array.
	 *
	 * @return 0 on success, <0 on error.
	 *
	 * @see xrt_prober::probe, xrt_prober::select
	 */
	int (*select)(struct xrt_instance *xinst,
	              struct xrt_device **xdevs,
	              size_t num_xdevs);

	/*!
	 * Creates a @ref xrt_compositor_fd.
	 *
	 * Should only be called once.
	 *
	 * @note Code consuming this interface should use
	 * xrt_instance_create_fd_compositor()
	 *
	 * @param xinst Pointer to self
	 * @param[in] xdev Device to use for creating the compositor
	 * @param[in] flip_y Whether to flip the direction of the y axis
	 * @param[out] out_xcfd Pointer to xrt_compositor_fd pointer, will be
	 * populated.
	 *
	 * @return 0 on success, <0 on error.
	 *
	 * @see xrt_gfx_provider_create_fd
	 */
	int (*create_fd_compositor)(struct xrt_instance *xinst,
	                            struct xrt_device *xdev,
	                            bool flip_y,
	                            struct xrt_compositor_fd **out_xcfd);

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
	 * @return 0 on success, <0 on error. (Note that success may mean
	 * returning a null pointer!)
	 */
	int (*get_prober)(struct xrt_instance *xinst,
	                  struct xrt_prober **out_xp);

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
};

/*!
 * @name Method call helpers for xrt_instance
 * Calling code should prefer using these instead of directly using the function
 * pointer members.
 * @{
 */
/*!
 * Helper function for @ref xrt_instance::select.
 *
 * See @ref xrt_instance::select for documentation.
 *
 * @relates xrt_instance
 */
static inline int
xrt_instance_select(struct xrt_instance *xinst,
                    struct xrt_device **xdevs,
                    size_t num_xdevs)
{
	return xinst->select(xinst, xdevs, num_xdevs);
}

/*!
 * Helper function for @ref xrt_instance::create_fd_compositor.
 *
 * See @ref xrt_instance::create_fd_compositor for documentation.
 *
 * @relates xrt_instance
 */
static inline int
xrt_instance_create_fd_compositor(struct xrt_instance *xinst,
                                  struct xrt_device *xdev,
                                  bool flip_y,
                                  struct xrt_compositor_fd **out_xcfd)
{
	return xinst->create_fd_compositor(xinst, xdev, flip_y, out_xcfd);
}

/*!
 * Helper function for @ref xrt_instance::get_prober.
 *
 * See @ref xrt_instance::get_prober for documentation.
 *
 * @relates xrt_instance
 */
static inline int
xrt_instance_get_prober(struct xrt_instance *xinst, struct xrt_prober **out_xp)
{
	return xinst->get_prober(xinst, out_xp);
}

/*!
 * Helper function for @ref xrt_instance::destroy.
 *
 * @param[in,out] xinst_ptr A pointer to your instance implementation pointer.
 *
 * Will destroy the instance if *xinst_ptr is not NULL. Will then set *xinst_ptr
 * to NULL.
 *
 * @relates xrt_instance
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
 * @}
 */


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
 * @param[out] out_xinst A pointer to an xrt_instance pointer. Will be
 * populated.
 *
 * @return 0 on success
 *
 * @relates xrt_instance
 */
int
xrt_instance_create(struct xrt_instance **out_xinst);

/*!
 * @}
 */

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
