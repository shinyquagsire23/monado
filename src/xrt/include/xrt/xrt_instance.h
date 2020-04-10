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
 * This object acts as a root object for Monado, it either wraps a
 * @ref xrt_prober or forms a connection to a out of process XR service. As
 * close to a singleton object there is in Monado, you should not create more
 * then one of these.
 */
struct xrt_instance
{
	/*!
	 * Returns the devices of the system represented as @ref xrt_device,
	 * see @ref xrt_prober::select, should only be called once.
	 */
	int (*select)(struct xrt_instance *xinst,
	              struct xrt_device **xdevs,
	              size_t num_xdevs);

	/*!
	 * Creates a @ref xrt_compositor_fd, should only be called once.
	 */
	int (*create_fd_compositor)(struct xrt_instance *xinst,
	                            struct xrt_device *xdev,
	                            bool flip_y,
	                            struct xrt_compositor_fd **out_xcfd);

	/*!
	 * Get the instance @ref xrt_prober, the instance might not be using a
	 * @ref xrt_prober and may return null, the instance still owns the
	 * prober and will destroy it. Can be called multiple times.
	 */
	int (*get_prober)(struct xrt_instance *xinst,
	                  struct xrt_prober **out_xp);

	/*!
	 * Use helper @p xrt_instance_destroy.
	 */
	void (*destroy)(struct xrt_instance *xinst);
};

/*!
 * Helper function for @ref xrt_instance::select, see @ref xrt_prober::select.
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
 */
static inline int
xrt_instance_get_prober(struct xrt_instance *xinst, struct xrt_prober **out_xp)
{
	return xinst->get_prober(xinst, out_xp);
}

/*!
 * Helper function for @ref xrt_instance::destroy.
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
 * Creating more then one @ref xrt_instance is probably never the right thing
 * to do, so avoid it.
 */
int
xrt_instance_create(struct xrt_instance **out_xinst);

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
