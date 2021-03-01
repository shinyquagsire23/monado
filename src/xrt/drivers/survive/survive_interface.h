// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: Apache-2.0
/*!
 * @file
 * @brief  Interface to Libsurvive adapter.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_survive
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_survive Lighthouse tracking using libsurvive
 * @ingroup drv
 *
 * @brief
 */

/*!
 * Create a probe for libsurvive
 *
 * @ingroup drv_survive
 */
struct xrt_auto_prober *
survive_create_auto_prober();

#ifdef __cplusplus
}
#endif
