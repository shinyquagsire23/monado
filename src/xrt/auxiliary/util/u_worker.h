// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Worker and threading pool.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_defines.h"


#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Worker thread pool.
 *
 */

/*!
 * A worker pool, can shared between multiple groups worker pool.
 *
 * @ingroup aux_util
 */
struct u_worker_thread_pool
{
	struct xrt_reference reference;
};

/*!
 * Creates a new thread pool to be used by a worker group.
 *
 * @param starting_worker_count How many worker threads can be active at the
 *                              same time without any "donated" threads.
 * @param thread_count          The number of threads to be created in total,
 *                              this is the maximum threads that can be in
 *                              flight at the same time.
 *
 * @ingroup aux_util
 */
struct u_worker_thread_pool *
u_worker_thread_pool_create(uint32_t starting_worker_count, uint32_t thread_count);

/*!
 * Internal function, only called by reference.
 *
 * @ingroup aux_util
 */
void
u_worker_thread_pool_destroy(struct u_worker_thread_pool *uwtp);

/*!
 * Standard Monado reference function.
 *
 * @ingroup aux_util
 */
static inline void
u_worker_thread_pool_reference(struct u_worker_thread_pool **dst, struct u_worker_thread_pool *src)
{
	struct u_worker_thread_pool *old_dst = *dst;

	if (old_dst == src) {
		return;
	}

	if (src) {
		xrt_reference_inc(&src->reference);
	}

	*dst = src;

	if (old_dst) {
		if (xrt_reference_dec(&old_dst->reference)) {
			u_worker_thread_pool_destroy(old_dst);
		}
	}
}


/*
 *
 * Worker group.
 *
 */

/*!
 * A worker group where you submit tasks to. Can share a thread pool with
 * multiple groups. Also can "donate" a thread to the thread pool by waiting.
 *
 * @ingroup aux_util
 */
struct u_worker_group
{
	struct xrt_reference reference;
};

/*!
 * Function typedef for tasks.
 *
 * @ingroup aux_util
 */
typedef void (*u_worker_group_func_t)(void *);

/*!
 * Create a new worker group.
 *
 * @ingroup aux_util
 */
struct u_worker_group *
u_worker_group_create(struct u_worker_thread_pool *uwtp);

/*!
 * Push a new task to worker group.
 *
 * @ingroup aux_util
 */
void
u_worker_group_push(struct u_worker_group *uwg, u_worker_group_func_t f, void *data);

/*!
 * Wait for all pushed tasks to be completed, "donates" this thread to the
 * shared thread pool.
 *
 * @ingroup aux_util
 */
void
u_worker_group_wait_all(struct u_worker_group *uwg);

/*!
 * Destroy a worker pool.
 *
 * @ingroup aux_util
 */
void
u_worker_group_destroy(struct u_worker_group *uwg);

/*!
 * Standard Monado reference function.
 *
 * @ingroup aux_util
 */
static inline void
u_worker_group_reference(struct u_worker_group **dst, struct u_worker_group *src)
{
	struct u_worker_group *old_dst = *dst;

	if (old_dst == src) {
		return;
	}

	if (src) {
		xrt_reference_inc(&src->reference);
	}

	*dst = src;

	if (old_dst) {
		if (xrt_reference_dec(&old_dst->reference)) {
			u_worker_group_destroy(old_dst);
		}
	}
}


#ifdef __cplusplus
}
#endif
