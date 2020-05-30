// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A FIFO for indices.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once


#define U_MAX_FIFO_INDICES 16

struct u_index_fifo
{
	uint32_t indices[U_MAX_FIFO_INDICES];
	size_t start;
	size_t end;
};

static inline int
u_index_fifo_is_empty(struct u_index_fifo *uif)
{
	if (uif->start == uif->end) {
		return 1;
	} else {
		return 0;
	}
}

static inline int
u_index_fifo_is_full(struct u_index_fifo *uif)
{
	if (((uif->end + 1) % U_MAX_FIFO_INDICES) == uif->start) {
		return 1;
	} else {
		return 0;
	}
}

static inline int
u_index_fifo_peek(struct u_index_fifo *uif, uint32_t *out_index)
{
	if (u_index_fifo_is_empty(uif)) {
		return -1;
	}

	*out_index = uif->indices[uif->start];
	return 0;
}

static inline int
u_index_fifo_pop(struct u_index_fifo *uif, uint32_t *out_index)
{
	if (u_index_fifo_is_empty(uif)) {
		return -1;
	}

	*out_index = uif->indices[uif->start];
	uif->start = (uif->start + 1) % U_MAX_FIFO_INDICES;
	return 0;
}

static inline int
u_index_fifo_push(struct u_index_fifo *uif, uint32_t index)
{
	if (u_index_fifo_is_full(uif)) {
		return -1;
	}

	uif->indices[uif->end] = index;
	uif->end = (uif->end + 1) % U_MAX_FIFO_INDICES;

	return 0;
}
