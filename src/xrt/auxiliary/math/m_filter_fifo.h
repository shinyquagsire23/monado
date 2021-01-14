// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A fifo that also allows you to dynamically filter.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_math
 */

#pragma once

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif


struct m_ff_f64;
struct m_ff_vec3_f32;

/*!
 * Allocates a filter fifo tracking @p num samples and fills it with @p num
 * samples at timepoint zero.
 */
void
m_ff_vec3_f32_alloc(struct m_ff_vec3_f32 **ff_out, size_t num);

/*!
 * Frees the given filter fifo and all it's samples.
 */
void
m_ff_vec3_f32_free(struct m_ff_vec3_f32 **ff_ptr);

/*!
 * Return the number of samples that can fill the fifo.
 */
size_t
m_ff_vec3_f32_get_num(struct m_ff_vec3_f32 *ff);

/*!
 * Pushes a sample at the given timepoint, pushing samples out of order yields
 * unspecified behaviour, so samples must be pushed in time order.
 */
void
m_ff_vec3_f32_push(struct m_ff_vec3_f32 *ff, const struct xrt_vec3 *sample, uint64_t timestamp_ns);

/*!
 * Return the sample at the index, zero means the last sample push, one second
 * last and so on.
 */
bool
m_ff_vec3_f32_get(struct m_ff_vec3_f32 *ff, size_t num, struct xrt_vec3 *out_sample, uint64_t *out_timestamp_ns);

/*!
 * Averages all samples in the fifo between the two timepoints, returns number
 * of samples sampled, if no samples was found between the timpoints returns 0
 * and sets @p out_average to all zeros.
 *
 * @param ff          Filter fifo to search in.
 * @param start_ns    Timepoint furthest in the past, to start searching for
 *                    samples.
 * @param stop_ns     Timepoint closest in the past, or now, to stop searching
 *                    for samples.
 * @param out_average Average of all samples in the given timeframe.
 */
size_t
m_ff_vec3_f32_filter(struct m_ff_vec3_f32 *ff, uint64_t start_ns, uint64_t stop_ns, struct xrt_vec3 *out_average);

/*!
 * Allocates a filter fifo tracking @p num samples and fills it with @p num
 * samples at timepoint zero.
 */
void
m_ff_f64_alloc(struct m_ff_f64 **ff_out, size_t num);

/*!
 * Frees the given filter fifo and all it's samples.
 */
void
m_ff_f64_free(struct m_ff_f64 **ff_ptr);

/*!
 * Return the number of samples that can fill the fifo.
 */
size_t
m_ff_f64_get_num(struct m_ff_f64 *ff);

/*!
 * Pushes a sample at the given timepoint, pushing samples out of order yields
 * unspecified behaviour, so samples must be pushed in time order.
 */
void
m_ff_f64_push(struct m_ff_f64 *ff, const double *sample, uint64_t timestamp_ns);

/*!
 * Return the sample at the index, zero means the last sample push, one second
 * last and so on.
 */
bool
m_ff_f64_get(struct m_ff_f64 *ff, size_t num, double *out_sample, uint64_t *out_timestamp_ns);

/*!
 * Averages all samples in the fifo between the two timepoints, returns number
 * of samples sampled, if no samples was found between the timpoints returns 0
 * and sets @p out_average to all zeros.
 *
 * @param ff          Filter fifo to search in.
 * @param start_ns    Timepoint furthest in the past, to start searching for
 *                    samples.
 * @param stop_ns     Timepoint closest in the past, or now, to stop searching
 *                    for samples.
 * @param out_average Average of all samples in the given timeframe.
 */
size_t
m_ff_f64_filter(struct m_ff_f64 *ff, uint64_t start_ns, uint64_t stop_ns, double *out_average);


#ifdef __cplusplus
}

/*!
 * Helper class to wrap a C filter fifo.
 */
class FilterFifo3F
{
private:
	struct m_ff_vec3_f32 *ff;


public:
	FilterFifo3F() = delete;

	FilterFifo3F(size_t size)
	{
		m_ff_vec3_f32_alloc(&ff, size);
	}

	~FilterFifo3F()
	{
		m_ff_vec3_f32_free(&ff);
	}

	inline void
	push(const xrt_vec3 &sample, uint64_t timestamp_ns)
	{
		m_ff_vec3_f32_push(ff, &sample, timestamp_ns);
	}

	inline void
	get(size_t num, xrt_vec3 *out_sample, uint64_t *out_timestamp_ns)
	{
		m_ff_vec3_f32_get(ff, num, out_sample, out_timestamp_ns);
	}

	inline size_t
	filter(uint64_t start_ns, uint64_t stop_ns, struct xrt_vec3 *out_average)
	{
		return m_ff_vec3_f32_filter(ff, start_ns, stop_ns, out_average);
	}
};
#endif
