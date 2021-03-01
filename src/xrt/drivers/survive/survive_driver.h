// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: Apache-2.0
/*!
 * @file
 * @brief  Adapter to Libsurvive.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup drv_survive
 */


int
survive_device_autoprobe(struct xrt_auto_prober *xap,
                         cJSON *attached_data,
                         bool no_hmds,
                         struct xrt_prober *xp,
                         struct xrt_device **out_xdevs);
