// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  List of all @ref xrt_builder creation functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_config_build.h"
#include "xrt/xrt_config_drivers.h"


/*
 *
 * Config checking.
 *
 */

#if defined(XRT_BUILD_DRIVER_PSMV) || defined(XRT_BUILD_DRIVER_PSVR) || defined(XRT_DOXYGEN)
#define T_BUILDER_RGB_TRACKING
#endif

#if defined(XRT_BUILD_DRIVER_REMOTE) || defined(XRT_DOXYGEN)
#define T_BUILDER_REMOTE
#endif

#if defined(XRT_BUILD_DRIVER_SURVIVE) || defined(XRT_BUILD_DRIVER_VIVE) || defined(XRT_DOXYGEN)
#define T_BUILDER_LIGHTHOUSE
#endif

#if defined(XRT_BUILD_DRIVER_SIMULAVR) || defined(XRT_DOXYGEN)
#define T_BUILDER_SIMULAVR
#endif

#if defined(XRT_BUILD_DRIVER_NS)
#define T_BUILDER_NS
#endif

// Always enabled.
#define T_BUILDER_LEGACY


/*
 *
 * Setter upper creation functions.
 *
 */

#ifdef T_BUILDER_RGB_TRACKING
/*!
 * RGB tracking based drivers, like @ref drv_psmv and @ref drv_psvr.
 */
struct xrt_builder *
t_builder_rgb_tracking_create(void);
#endif

#ifdef T_BUILDER_REMOTE
/*!
 * The remote driver builder.
 */
struct xrt_builder *
t_builder_remote_create(void);
#endif

#ifdef T_BUILDER_LEGACY
/*!
 * Builder used as a fallback for drivers not converted to builders yet.
 */
struct xrt_builder *
t_builder_legacy_create(void);
#endif

#ifdef T_BUILDER_LIGHTHOUSE
/*!
 * Builder for Lighthouse-tracked devices (vive, index, tundra trackers, etc.)
 */
struct xrt_builder *
t_builder_lighthouse_create(void);
#endif

#ifdef T_BUILDER_SIMULAVR
/*!
 * Builder for SimulaVR headsets
 */
struct xrt_builder *
t_builder_simula_create(void);
#endif

#ifdef T_BUILDER_NS
struct xrt_builder *
t_builder_north_star_create(void);
#endif
