// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common things to pull into a target.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "targets_enabled_drivers.h"

#include "target_lists.h"

#ifdef XRT_BUILD_DRIVER_HDK
#include "hdk/hdk_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_OHMD
#include "ohmd/oh_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_PSMV
#include "psmv/psmv_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_PSVR
#include "psvr/psvr_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_HYDRA
#include "hydra/hydra_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_VIVE
#include "vive/vive_prober.h"
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
 * - `struct xrt_device **out_xdevs` (an array of XRT_MAX_DEVICES_PER_PROBE
 * xrt_device pointers)
 *
 * It is called when devices[index] match the VID and PID in the list.
 * It should return 0 if it decides not to create any devices, negative on
 * error, and the number of devices created if it creates one or more: it should
 * assign sequential elements of out_xdevs to the created devices.
 */
struct xrt_prober_entry target_entry_list[] = {
#ifdef XRT_BUILD_DRIVER_PSMV
    {PSMV_VID, PSMV_PID_ZCM1, psmv_found, "PS Move Controller (ZCM1)"},
    {PSMV_VID, PSMV_PID_ZCM2, psmv_found, "PS Move Controller (ZCM2)"},
#endif // XRT_BUILD_DRIVER_PSMV

#ifdef XRT_BUILD_DRIVER_HYDRA
    {HYDRA_VID, HYDRA_PID, hydra_found, "Razer Hydra"},
#endif // XRT_BUILD_DRIVER_HYDRA

#ifdef XRT_BUILD_DRIVER_HDK
    {HDK_VID, HDK_PID, hdk_found, "OSVR HDK"},
#endif // XRT_BUILD_DRIVER_HDK

#ifdef XRT_BUILD_DRIVER_VIVE
    {HTC_VID, VIVE_PID, vive_found, "HTC Vive"},
    {HTC_VID, VIVE_PRO_MAINBOARD_PID, vive_found, "HTC Vive Pro"},
#endif

    {0x0000, 0x0000, NULL, NULL}, // Terminate
};

struct xrt_prober_entry *target_entry_lists[] = {
    target_entry_list,
    NULL, // Terminate
};

xrt_auto_prober_creator target_auto_list[] = {
#ifdef XRT_BUILD_DRIVER_PSVR
    psvr_create_auto_prober,
#endif

#ifdef XRT_BUILD_DRIVER_OHMD
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
