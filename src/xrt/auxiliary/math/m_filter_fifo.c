// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A fifo that also allows you to dynamically filter.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_math
 */

#include "util/u_misc.h"
#include "math/m_filter_fifo.h"

#include <assert.h>


/*
 *
 * Filter fifo vec3_f32.
 *
 */

struct m_ff_vec3_f32
{
	size_t num;
	size_t latest;
	struct xrt_vec3 *samples;
	uint64_t *timestamps_ns;
};


/*
 *
 * Internal functions.
 *
 */

static void
vec3_f32_init(struct m_ff_vec3_f32 *ff, size_t num)
{
	ff->samples = U_TYPED_ARRAY_CALLOC(struct xrt_vec3, num);
	ff->timestamps_ns = U_TYPED_ARRAY_CALLOC(uint64_t, num);
	ff->num = num;
	ff->latest = 0;
}

static void
vec3_f32_destroy(struct m_ff_vec3_f32 *ff)
{
	if (ff->samples != NULL) {
		free(ff->samples);
		ff->samples = NULL;
	}

	if (ff->timestamps_ns != NULL) {
		free(ff->timestamps_ns);
		ff->timestamps_ns = NULL;
	}

	ff->num = 0;
	ff->latest = 0;
}


/*
 *
 * 'Exported' functions.
 *
 */

void
m_ff_vec3_f32_alloc(struct m_ff_vec3_f32 **ff_out, size_t num)
{
	struct m_ff_vec3_f32 *ff = U_TYPED_CALLOC(struct m_ff_vec3_f32);
	vec3_f32_init(ff, num);
	*ff_out = ff;
}

void
m_ff_vec3_f32_free(struct m_ff_vec3_f32 **ff_ptr)
{
	struct m_ff_vec3_f32 *ff = *ff_ptr;
	if (ff == NULL) {
		return;
	}

	vec3_f32_destroy(ff);
	free(ff);
	*ff_ptr = NULL;
}

size_t
m_ff_vec3_f32_get_num(struct m_ff_vec3_f32 *ff)
{
	return ff->num;
}

void
m_ff_vec3_f32_push(struct m_ff_vec3_f32 *ff, const struct xrt_vec3 *sample, uint64_t timestamp_ns)
{
	assert(ff->timestamps_ns[ff->latest] <= timestamp_ns);

	// We write samples backwards in the queue.
	size_t i = ff->latest == 0 ? ff->num - 1 : --ff->latest;
	ff->latest = i;

	ff->samples[i] = *sample;
	ff->timestamps_ns[i] = timestamp_ns;
}

bool
m_ff_vec3_f32_get(struct m_ff_vec3_f32 *ff, size_t num, struct xrt_vec3 *out_sample, uint64_t *out_timestamp_ns)
{
	if (num >= ff->num) {
		return false;
	}

	size_t pos = (ff->latest + num) % ff->num;
	*out_sample = ff->samples[pos];
	*out_timestamp_ns = ff->timestamps_ns[pos];

	return true;
}

size_t
m_ff_vec3_f32_filter(struct m_ff_vec3_f32 *ff, uint64_t start_ns, uint64_t stop_ns, struct xrt_vec3 *out_average)
{
	size_t num_sampled = 0;
	size_t count = 0;
	// Use double precision internally.
	double x = 0, y = 0, z = 0;

	// Error, skip averaging.
	if (start_ns > stop_ns) {
		count = ff->num;
	}

	while (count < ff->num) {
		size_t pos = (ff->latest + count) % ff->num;

		// We have not yet reached where to start.
		if (ff->timestamps_ns[pos] > stop_ns) {
			count++;
			continue;
		}

		// If the sample is before the start we have reach the end.
		if (ff->timestamps_ns[pos] < start_ns) {
			count++;
			break;
		}

		x += ff->samples[pos].x;
		y += ff->samples[pos].y;
		z += ff->samples[pos].z;
		num_sampled++;
		count++;
	}

	// Avoid division by zero.
	if (num_sampled > 0) {
		x /= num_sampled;
		y /= num_sampled;
		z /= num_sampled;
	}

	out_average->x = (float)x;
	out_average->y = (float)y;
	out_average->z = (float)z;

	return num_sampled;
}


/*
 *
 * Filter fifo f64.
 *
 */

struct m_ff_f64
{
	size_t num;
	size_t latest;
	double *samples;
	uint64_t *timestamps_ns;
};


/*
 *
 * Internal functions.
 *
 */

static void
ff_f64_init(struct m_ff_f64 *ff, size_t num)
{
	ff->samples = U_TYPED_ARRAY_CALLOC(double, num);
	ff->timestamps_ns = U_TYPED_ARRAY_CALLOC(uint64_t, num);
	ff->num = num;
	ff->latest = 0;
}

static void
ff_f64_destroy(struct m_ff_f64 *ff)
{
	if (ff->samples != NULL) {
		free(ff->samples);
		ff->samples = NULL;
	}

	if (ff->timestamps_ns != NULL) {
		free(ff->timestamps_ns);
		ff->timestamps_ns = NULL;
	}

	ff->num = 0;
	ff->latest = 0;
}


/*
 *
 * 'Exported' functions.
 *
 */

void
m_ff_f64_alloc(struct m_ff_f64 **ff_out, size_t num)
{
	struct m_ff_f64 *ff = U_TYPED_CALLOC(struct m_ff_f64);
	ff_f64_init(ff, num);
	*ff_out = ff;
}

void
m_ff_f64_free(struct m_ff_f64 **ff_ptr)
{
	struct m_ff_f64 *ff = *ff_ptr;
	if (ff == NULL) {
		return;
	}

	ff_f64_destroy(ff);
	free(ff);
	*ff_ptr = NULL;
}

size_t
m_ff_f64_get_num(struct m_ff_f64 *ff)
{
	return ff->num;
}

void
m_ff_f64_push(struct m_ff_f64 *ff, const double *sample, uint64_t timestamp_ns)
{
	assert(ff->timestamps_ns[ff->latest] <= timestamp_ns);

	// We write samples backwards in the queue.
	size_t i = ff->latest == 0 ? ff->num - 1 : --ff->latest;
	ff->latest = i;

	ff->samples[i] = *sample;
	ff->timestamps_ns[i] = timestamp_ns;
}

bool
m_ff_f64_get(struct m_ff_f64 *ff, size_t num, double *out_sample, uint64_t *out_timestamp_ns)
{
	if (num >= ff->num) {
		return false;
	}

	size_t pos = (ff->latest + num) % ff->num;
	*out_sample = ff->samples[pos];
	*out_timestamp_ns = ff->timestamps_ns[pos];

	return true;
}

size_t
m_ff_f64_filter(struct m_ff_f64 *ff, uint64_t start_ns, uint64_t stop_ns, double *out_average)
{
	size_t num_sampled = 0;
	size_t count = 0;
	double val = 0;

	// Error, skip averaging.
	if (start_ns > stop_ns) {
		count = ff->num;
	}

	while (count < ff->num) {
		size_t pos = (ff->latest + count) % ff->num;

		// We have not yet reached where to start.
		if (ff->timestamps_ns[pos] > stop_ns) {
			count++;
			continue;
		}

		// If the sample is before the start we have reach the end.
		if (ff->timestamps_ns[pos] < start_ns) {
			count++;
			break;
		}

		val += ff->samples[pos];
		num_sampled++;
		count++;
	}

	// Avoid division by zero.
	if (num_sampled > 0) {
		val /= num_sampled;
	}

	*out_average = val;

	return num_sampled;
}
