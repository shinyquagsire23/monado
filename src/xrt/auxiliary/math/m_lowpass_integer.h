// Copyright 2019, 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Low-pass IIR filter for integers - C wrapper
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/*!
 * An IIR (low pass) filter for integer values.
 *
 * Wraps xrt::auxiliary::math::IntegerLowPassIIRFilter - see that if you need a different scalar type.
 *
 */
struct m_lowpass_integer;

/*!
 * Constructor
 *
 * @note Taking alpha, not a cutoff frequency, here, because it's easier with the rational math.
 *
 * Together, the two parameters specify the alpha value used to blend between new input and existing state. Larger
 * values mean more influence from new input.
 *
 * @param alpha_numerator The numerator of the alpha value. Must be greater than 0 and less than @p alpha_denominator
 * @param alpha_denominator The denominator of the alpha value. Must be greater than 0.
 *
 * @return null if a parameter is out of range
 *
 * @public @memberof m_lowpass_integer
 */
struct m_lowpass_integer *
m_lowpass_integer_create(int64_t alpha_numerator, int64_t alpha_denominator);


/*!
 * Filter a sample
 *
 * @param mli self-pointer
 * @param sample The value to filter
 *
 * @public @memberof m_lowpass_integer
 */
void
m_lowpass_integer_add_sample(struct m_lowpass_integer *mli, int64_t sample);

/*!
 * Get the filtered value.
 *
 * Probably 0 or other meaningless value if it's not initialized: see @ref m_lowpass_integer_is_initialized
 *
 * @param mli self-pointer
 *
 * @public @memberof m_lowpass_integer
 */
int64_t
m_lowpass_integer_get_state(const struct m_lowpass_integer *mli);

/*!
 * Get whether we have initialized state.
 *
 * @param mli self-pointer
 *
 * @public @memberof m_lowpass_integer
 */
bool
m_lowpass_integer_is_initialized(const struct m_lowpass_integer *mli);

/*!
 * Destroy a lowpass integer filter.
 *
 * Does null checks.
 *
 * @param ptr_to_mli Address of your lowpass integer filter. Will be set to zero.
 *
 * @public @memberof m_lowpass_integer
 */
void
m_lowpass_integer_destroy(struct m_lowpass_integer **ptr_to_mli);

#ifdef __cplusplus
}
#endif
