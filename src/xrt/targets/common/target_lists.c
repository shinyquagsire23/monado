// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common things to pull into a target.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "target_lists.h"

#ifdef XRT_BUILD_HDK
#include "hdk/hdk_interface.h"
#endif

#ifdef XRT_BUILD_OHMD
#include "ohmd/oh_interface.h"
#endif

#ifdef XRT_BUILD_PSMV
#include "psmv/psmv_interface.h"
#endif

#ifdef XRT_BUILD_PSVR
#include "psvr/psvr_interface.h"
#endif


/*!
 * Each entry should be a vendor ID (VID), product ID (PID), a "found" function,
 * and a string literal name.
 *
 * The "found" function must return `int` and take as parameters:
 *
 * - `struct xrt_prober *xp`
 * - `struct xrt_prober_device **devices`
 * - `size_t index`
 * - `struct xrt_device **out_xdev`
 *
 * It is called when devices[index] match the VID and PID in the list.
 * It should return 0 if it decides not to create any devices, or 1 if it
 * creates one: it should assign *out_xdev to the created device.
 */
struct xrt_prober_entry target_entry_list[] = {
#ifdef XRT_BUILD_PSVR
    {PSMV_VID, PSMV_PID, psmv_found, "PS Move"},
#endif
    {0x0000, 0x0000, NULL, NULL}, // Terminate
};

struct xrt_prober_entry *target_entry_lists[] = {
    target_entry_list,
    NULL, // Terminate
};

xrt_auto_prober_creator target_auto_list[] = {
#ifdef XRT_BUILD_HDK
    hdk_create_auto_prober,
#endif

#ifdef XRT_BUILD_PSVR
    psvr_create_auto_prober,
#endif

#ifdef XRT_BUILD_OHMD
    // OpenHMD last as we want to override it with native drivers.
    oh_create_auto_prober,
#endif
    NULL, // Terminate
};

struct xrt_prober_entry_lists target_lists = {
    target_entry_lists,
    target_auto_list,
    NULL,
};
