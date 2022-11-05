// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to simulated driver.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_wivrn
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_wivrn Simulated driver
 * @ingroup drv
 *
 * @brief Simple do-nothing simulated driver.
 */

/*!
 * Create a auto prober for simulated devices.
 *
 * @ingroup drv_wivrn
 */
struct xrt_auto_prober *
wivrn_create_auto_prober(void);

/*!
 * Create a simulated hmd.
 *
 * @ingroup drv_wivrn
 */

/*!
 * @dir drivers/wivrn
 *
 * @brief @ref drv_wivrn files.
 */


#ifdef __cplusplus
}
#endif
