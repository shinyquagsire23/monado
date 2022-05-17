// Copyright 2019, 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Low-pass IIR filter for floats - C wrapper
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 */

#pragma once

#include "util/u_time.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/*!
 * An IIR (low pass) filter for scalar float values.
 *
 * Wraps xrt::auxiliary::math::LowPassIIRFilter - see that if you need a different scalar type, or its related types if
 * you want to filter a vector.
 *
 */
struct m_lowpass_float;

/*!
 * Constructor
 *
 * @param cutoff_hz A cutoff frequency in Hertz: signal changes much
 * lower in frequency will be passed through the filter, while signal
 * changes much higher in frequency will be blocked.
 *
 * @public @memberof m_lowpass_float
 */
struct m_lowpass_float *
m_lowpass_float_create(float cutoff_hz);


/*!
 * Filter a sample
 *
 * @param mlf self-pointer
 * @param sample The value to filter
 * @param timestamp_ns The time that this sample was measured.
 *
 * @public @memberof m_lowpass_float
 */
void
m_lowpass_float_add_sample(struct m_lowpass_float *mlf, float sample, timepoint_ns timestamp_ns);

/*!
 * Get the filtered value.
 *
 * Probably 0 or other meaningless value if it's not initialized: see @ref m_lowpass_float_is_initialized
 *
 * @param mlf self-pointer
 *
 * @public @memberof m_lowpass_float
 */
float
m_lowpass_float_get_state(const struct m_lowpass_float *mlf);

/*!
 * Get the time of last update
 *
 * @param mlf self-pointer
 *
 * @public @memberof m_lowpass_float
 */
timepoint_ns
m_lowpass_float_get_timestamp_ns(const struct m_lowpass_float *mlf);

/*!
 * Get whether we have initialized state.
 *
 * @param mlf self-pointer
 *
 * @public @memberof m_lowpass_float
 */
bool
m_lowpass_float_is_initialized(const struct m_lowpass_float *mlf);

/*!
 * Destroy a lowpass integer filter.
 *
 * Does null checks.
 *
 * @param ptr_to_mlf Address of your lowpass integer filter. Will be set to zero.
 *
 * @public @memberof m_lowpass_float
 */
void
m_lowpass_float_destroy(struct m_lowpass_float **ptr_to_mlf);

#ifdef __cplusplus
}
#endif
