// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tracing support code, see @ref tracing.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_config_os.h"
#include "xrt/xrt_config_have.h"

#include "os/os_time.h"

#include "util/u_debug.h"
#include "util/u_trace_marker.h"

#include <inttypes.h>


#ifdef U_TRACE_PERCETTO

DEBUG_GET_ONCE_BOOL_OPTION(tracing, "XRT_TRACING", false)

#if defined(__GNUC__)
#pragma GCC diagnostic push
// ATOMIC_VAR_INIT was deprecated in C14 which is used by PERCETTO_* defines.
#pragma GCC diagnostic ignored "-Wdeprecated-pragma"
#endif

PERCETTO_CATEGORY_DEFINE(U_TRACE_CATEGORIES)

PERCETTO_TRACK_DEFINE(pc_cpu, PERCETTO_TRACK_EVENTS);
PERCETTO_TRACK_DEFINE(pc_allotted, PERCETTO_TRACK_EVENTS);
PERCETTO_TRACK_DEFINE(pc_gpu, PERCETTO_TRACK_EVENTS);
PERCETTO_TRACK_DEFINE(pc_margin, PERCETTO_TRACK_EVENTS);
PERCETTO_TRACK_DEFINE(pc_error, PERCETTO_TRACK_EVENTS);
PERCETTO_TRACK_DEFINE(pc_info, PERCETTO_TRACK_EVENTS);
PERCETTO_TRACK_DEFINE(pc_present, PERCETTO_TRACK_EVENTS);
PERCETTO_TRACK_DEFINE(pa_cpu, PERCETTO_TRACK_EVENTS);
PERCETTO_TRACK_DEFINE(pa_draw, PERCETTO_TRACK_EVENTS);
PERCETTO_TRACK_DEFINE(pa_wait, PERCETTO_TRACK_EVENTS);

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static enum u_trace_which static_which;
static bool static_inited = false;


void
u_trace_marker_setup(enum u_trace_which which)
{
	static_which = which;

	I_PERCETTO_TRACK_PTR(pc_cpu)->name = "PC 1 Sleep";
	I_PERCETTO_TRACK_PTR(pc_allotted)->name = "PC 2 Allotted time";
	I_PERCETTO_TRACK_PTR(pc_gpu)->name = "PC 3 GPU";
	I_PERCETTO_TRACK_PTR(pc_margin)->name = "PC 4 Margin";
	I_PERCETTO_TRACK_PTR(pc_error)->name = "PC 5 Error";
	I_PERCETTO_TRACK_PTR(pc_info)->name = "PC 6 Info";
	I_PERCETTO_TRACK_PTR(pc_present)->name = "PC 7 Present";

	I_PERCETTO_TRACK_PTR(pa_cpu)->name = "PA 1 App";
	I_PERCETTO_TRACK_PTR(pa_draw)->name = "PA 2 Draw";
	I_PERCETTO_TRACK_PTR(pa_wait)->name = "PA 3 Wait";
}

void
u_trace_marker_init(void)
{
	if (!debug_get_bool_option_tracing()) {
		return;
	}

	if (static_inited) {
		return;
	}
	static_inited = true;

	int ret = PERCETTO_INIT(PERCETTO_CLOCK_MONOTONIC);
	if (ret != 0) {
		return;
	}

	if (static_which == U_TRACE_WHICH_SERVICE) {
		PERCETTO_REGISTER_TRACK(pc_cpu);
		PERCETTO_REGISTER_TRACK(pc_allotted);
		PERCETTO_REGISTER_TRACK(pc_gpu);
		PERCETTO_REGISTER_TRACK(pc_margin);
		PERCETTO_REGISTER_TRACK(pc_error);
		PERCETTO_REGISTER_TRACK(pc_info);
		PERCETTO_REGISTER_TRACK(pc_present);

		PERCETTO_REGISTER_TRACK(pa_cpu);
		PERCETTO_REGISTER_TRACK(pa_draw);
		PERCETTO_REGISTER_TRACK(pa_wait);
	}
}

#else // !U_TRACE_PERCETTO

void
u_trace_marker_setup(enum u_trace_which which)
{
	(void)which;

	// Noop
}

void
u_trace_marker_init(void)
{
	// Noop
}

#endif // !U_TRACE_PERCETTO
