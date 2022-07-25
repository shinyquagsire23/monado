// Copyright 2016-2019, Philipp Zabel
// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common vive header
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

#include <stdint.h>
#include <asm/byteorder.h>

#include "util/u_debug.h"
#include "util/u_time.h"

/*
 *
 * Printing functions.
 *
 */

#define VIVE_TRACE(d, ...) U_LOG_IFL_T(d->log_level, __VA_ARGS__)
#define VIVE_DEBUG(d, ...) U_LOG_IFL_D(d->log_level, __VA_ARGS__)
#define VIVE_INFO(d, ...) U_LOG_IFL_I(d->log_level, __VA_ARGS__)
#define VIVE_WARN(d, ...) U_LOG_IFL_W(d->log_level, __VA_ARGS__)
#define VIVE_ERROR(d, ...) U_LOG_IFL_E(d->log_level, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(vive_log, "VIVE_LOG", U_LOGGING_WARN)

#define VIVE_CLOCK_FREQ 48e6 // 48 MHz
#define CAMERA_FREQUENCY 54
#define IMU_FREQUENCY 1000

/*!
 * Helper function to convert raw device ticks to nanosecond timestamps.
 *
 * `inout` params should start as the results from the previous sample call.
 * For the first call, pass zero for the `inout` params.
 *
 * @param sample_ticks_raw Current sample ticks
 * @param[in, out] inout_prev_ticks Overwritten with `sample_ticks_raw`
 * @param[in, out] inout_ts_ns Resulting timestamp in nanoseconds
 */
static inline void
ticks_to_ns(uint32_t sample_ticks_raw, uint32_t *inout_prev_ticks, timepoint_ns *inout_ts_ns)
{
	uint32_t sample_ticks = __le32_to_cpu(sample_ticks_raw);
	uint32_t prev_ticks = *inout_prev_ticks;

	// From the C standard https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2912.pdf
	// "A computation involving unsigned operands can never produce an overflow,
	// because arithmetic for the unsigned type is performed modulo 2^N"
	uint32_t delta_ticks = sample_ticks - prev_ticks;

	const double one_tick_in_s = (1 / VIVE_CLOCK_FREQ);
	const double one_tick_in_ns = one_tick_in_s * U_TIME_1S_IN_NS;
	time_duration_ns delta_ns = delta_ticks * one_tick_in_ns;

	*inout_prev_ticks = sample_ticks;
	*inout_ts_ns += delta_ns;
}
