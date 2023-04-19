// Copyright 2021, Mateo de Mayo.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Qwerty devices @ref xrt_auto_prober "autoprober".
 * @author Mateo de Mayo <mateodemayo@gmail.com>
 * @ingroup drv_qwerty
 */

#include "xrt/xrt_prober.h"

#include "util/u_logging.h"

#include "qwerty_device.h"
#include "qwerty_interface.h"


/*
 *
 * 'Exported' functions.
 *
 */

xrt_result_t
qwerty_create_devices(enum u_logging_level log_level,
                      struct xrt_device **out_hmd,
                      struct xrt_device **out_left,
                      struct xrt_device **out_right)
{
	struct qwerty_hmd *qhmd = qwerty_hmd_create();
	struct qwerty_controller *qleft = qwerty_controller_create(true, qhmd);
	struct qwerty_controller *qright = qwerty_controller_create(false, qhmd);

	qwerty_system_create(qhmd, qleft, qright, log_level);

	*out_hmd = &qhmd->base.base;
	*out_left = &qleft->base.base;
	*out_right = &qright->base.base;

	return XRT_SUCCESS;
}
