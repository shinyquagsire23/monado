// Copyright 2020-2021, N Madsen.
// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Defines and constants related to WMR driver code.
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Nova King <technobaboo@proton.me>
 * @ingroup drv_wmr
 */

#pragma once

#include "xrt/xrt_compiler.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Headset type, used to select different control and init/shutdown procedures.
 *
 * @ingroup drv_wmr
 */
enum wmr_headset_type
{
	WMR_HEADSET_GENERIC,
	WMR_HEADSET_HP_VR1000,
	WMR_HEADSET_REVERB_G1,
	WMR_HEADSET_REVERB_G2,
	WMR_HEADSET_SAMSUNG_XE700X3AI,
	WMR_HEADSET_SAMSUNG_800ZAA,
	WMR_HEADSET_LENOVO_EXPLORER,
	WMR_HEADSET_MEDION_ERAZER_X1000,
};

/*!
 * Defines for the WMR driver.
 *
 * @addtogroup drv_wmr
 * @{
 */

#define MS_HOLOLENS_MANUFACTURER_STRING "Microsoft"
#define MS_HOLOLENS_PRODUCT_STRING "HoloLens Sensors"

#define MICROSOFT_VID 0x045e
#define HOLOLENS_SENSORS_PID 0x0659
#define WMR_CONTROLLER_PID 0x065b
#define WMR_CONTROLLER_LEFT_PRODUCT_STRING "Motion controller - Left"
#define WMR_CONTROLLER_RIGHT_PRODUCT_STRING "Motion controller - Right"

#define HP_VID 0x03f0
#define VR1000_PID 0x0367
#define REVERB_G1_PID 0x0c6a
#define REVERB_G2_PID 0x0580
#define REVERB_G2_CONTROLLER_PID 0x066a /* On 0x045e Microsoft VID */

#define LENOVO_VID 0x17ef
#define EXPLORER_PID 0xb801

#define SAMSUNG_VID 0x04e8
#define ODYSSEY_PID 0x7310
#define ODYSSEY_PLUS_PID 0x7312
#define ODYSSEY_CONTROLLER_PID 0x065d

#define QUANTA_VID 0x0408 /* Medion? */
#define MEDION_ERAZER_X1000_PID 0xb5d5

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
