// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Small utility for keeping track of the history of an xrt_space_relation, ie. for knowing where a HMD or
 * controller was in the past
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */
#pragma once

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque type for storing the history of a space relation in a ring buffer
 *
 * @ingroup aux_util
 */
struct m_relation_history;

/**
 * @brief Describes how the resulting space relation for the desired time stamp was generated.
 *
 * @relates m_relation_history
 */
enum m_relation_history_result
{
	M_RELATION_HISTORY_RESULT_INVALID = 0,       //!< The supplied timestamp was invalid (0) or buffer was empty
	M_RELATION_HISTORY_RESULT_EXACT,             //!< The exact desired timestamp was found
	M_RELATION_HISTORY_RESULT_INTERPOLATED,      //!< The desired timestamp was between two entries
	M_RELATION_HISTORY_RESULT_PREDICTED,         //!< The desired timestamp was newer than the most recent entry
	M_RELATION_HISTORY_RESULT_REVERSE_PREDICTED, //!< The desired timestamp was older than the oldest entry
};

/*!
 * @brief Creates an opaque relation_history object.
 *
 * @public @memberof m_relation_history
 */
void
m_relation_history_create(struct m_relation_history **rh);

/*!
 * Pushes a new pose to the history.
 *
 * If the history is full, it will also pop a pose out of the other side of the buffer.
 *
 * @public @memberof m_relation_history
 */
void
m_relation_history_push(struct m_relation_history *rh, struct xrt_space_relation const *in_relation, uint64_t ts);

/*!
 * @brief Interpolates or extrapolates to the desired timestamp.
 *
 * Read-only operation - doesn't remove anything from the buffer or anything like that - you can call this as often as
 * you want.
 *
 * @public @memberof m_relation_history
 */
enum m_relation_history_result
m_relation_history_get(struct m_relation_history *rh, uint64_t at_time_ns, struct xrt_space_relation *out_relation);

/*!
 * Destroys an opaque relation_history object.
 *
 * @public @memberof m_relation_history
 */
void
m_relation_history_destroy(struct m_relation_history **rh);

#ifdef __cplusplus
}
#endif
