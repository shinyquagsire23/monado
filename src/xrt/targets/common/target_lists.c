// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common things to pull into a target.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_config_drivers.h"

#include "target_lists.h"
#include "target_builder_interface.h"


#ifdef XRT_BUILD_DRIVER_ARDUINO
#include "arduino/arduino_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_SIMULATED
#include "simulated/simulated_interface.h"
#endif

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

#ifdef XRT_BUILD_DRIVER_DAYDREAM
#include "daydream/daydream_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_ANDROID
#include "android/android_prober.h"
#endif

#ifdef XRT_BUILD_DRIVER_ILLIXR
#include "illixr/illixr_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_REALSENSE
#include "realsense/rs_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_ULV2
#include "ultraleap_v2/ulv2_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_DEPTHAI
#include "depthai/depthai_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_QWERTY
#include "qwerty/qwerty_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_WMR
#include "wmr/wmr_interface.h"
#include "wmr/wmr_common.h"
#endif

#ifdef XRT_BUILD_DRIVER_EUROC
#include "euroc/euroc_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
#ifdef XRT_BUILD_DRIVER_DEPTHAI
#include "ht/ht_interface.h"
#endif
#endif


/*!
 * Builders
 */
xrt_builder_create_func_t target_builder_list[] = {
#ifdef T_BUILDER_RGB_TRACKING
    t_builder_rgb_tracking_create,
#endif // T_BUILDER_RGB_TRACKING

#ifdef T_BUILDER_SIMULAVR
    t_builder_simula_create,
#endif

#ifdef T_BUILDER_LIGHTHOUSE
    t_builder_lighthouse_create,
#endif // T_BUILDER_LIGHTHOUSE

#ifdef T_BUILDER_REMOTE
    t_builder_remote_create,
#endif // T_BUILDER_REMOTE

#ifdef T_BUILDER_NS
    t_builder_north_star_create,
#endif
#ifdef T_BUILDER_LEGACY
    t_builder_legacy_create,
#endif // T_BUILDER_LEGACY
    NULL,
};


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
    {PSMV_VID, PSMV_PID_ZCM1, psmv_found, "PS Move Controller (ZCM1)", "psmv"},
    {PSMV_VID, PSMV_PID_ZCM2, psmv_found, "PS Move Controller (ZCM2)", "psmv"},
#endif // XRT_BUILD_DRIVER_PSMV

#ifdef XRT_BUILD_DRIVER_HYDRA
    {HYDRA_VID, HYDRA_PID, hydra_found, "Razer Hydra", "hydra"},
#endif // XRT_BUILD_DRIVER_HYDRA

#ifdef XRT_BUILD_DRIVER_HDK
    {HDK_VID, HDK_PID, hdk_found, "OSVR HDK", "osvr"},
#endif // XRT_BUILD_DRIVER_HDK

#ifdef XRT_BUILD_DRIVER_WMR
    {MICROSOFT_VID, HOLOLENS_SENSORS_PID, wmr_found, "Microsoft HoloLens Sensors", "wmr"},
    {MICROSOFT_VID, WMR_CONTROLLER_PID, wmr_bt_controller_found, "WMR Bluetooth controller", "wmr"},
    {MICROSOFT_VID, REVERB_G2_CONTROLLER_PID, wmr_bt_controller_found, "HP Reverb G2 Bluetooth controller", "wmr"},
    {MICROSOFT_VID, ODYSSEY_CONTROLLER_PID, wmr_bt_controller_found, "Odyssey Bluetooth controller", "wmr"},

#endif // XRT_BUILD_DRIVER_WMR

    {0x0000, 0x0000, NULL, NULL, NULL}, // Terminate
};

struct xrt_prober_entry *target_entry_lists[] = {
    target_entry_list,
    NULL, // Terminate
};

xrt_auto_prober_create_func_t target_auto_list[] = {
#ifdef XRT_BUILD_DRIVER_PSVR
    psvr_create_auto_prober,
#endif

#ifdef XRT_BUILD_DRIVER_ARDUINO
    // Before OpenHMD
    arduino_create_auto_prober,
#endif

#ifdef XRT_BUILD_DRIVER_DAYDREAM
    // Before OpenHMD
    daydream_create_auto_prober,
#endif

#ifdef XRT_BUILD_DRIVER_OHMD
    // OpenHMD almost as the end as we want to override it with native drivers.
    oh_create_auto_prober,
#endif

#ifdef XRT_BUILD_DRIVER_ANDROID
    android_create_auto_prober,
#endif

#ifdef XRT_BUILD_DRIVER_ILLIXR
    illixr_create_auto_prober,
#endif

#ifdef XRT_BUILD_DRIVER_REALSENSE
    rs_create_auto_prober,
#endif

#ifdef XRT_BUILD_DRIVER_EUROC
    euroc_create_auto_prober,
#endif

#ifdef XRT_BUILD_DRIVER_QWERTY
    qwerty_create_auto_prober,
#endif

#ifdef XRT_BUILD_DRIVER_SIMULATED
    // Simulated headset driver last.
    simulated_create_auto_prober,
#endif

    NULL, // Terminate
};

struct xrt_prober_entry_lists target_lists = {
    target_builder_list,
    target_entry_lists,
    target_auto_list,
    NULL,
};
