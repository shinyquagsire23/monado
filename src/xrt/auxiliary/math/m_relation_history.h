// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Small utility for keeping track of the history of an xrt_space_relation, ie. for knowing where a HMD or
 * controller was in the past
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#include "xrt/xrt_defines.h"
struct m_relation_history;

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Creates an opaque relation_history object.
 *
 * @ingroup aux_util
 */
void
m_relation_history_create(struct m_relation_history **rh);

/*!
 * Pushes a new pose to the history - if the history is full, it will also pop a pose out of the other side of the
 * buffer.
 *
 * @ingroup aux_util
 */
void
m_relation_history_push(struct m_relation_history *rh, struct xrt_space_relation *in_relation, uint64_t ts);

/*!
 * Interpolates or extrapolates to the desired timestamp. Read-only operation - doesn't remove anything from the buffer
 * or anything like that - you can call this as often as you want.
 *
 * @ingroup aux_util
 */
void
m_relation_history_get(struct m_relation_history *rh, struct xrt_space_relation *out_relation, uint64_t at_time_ns);

/*!
 * Destroys an opaque relation_history object.
 *
 * @ingroup aux_util
 */
void
m_relation_history_destroy(struct m_relation_history **rh);

#ifdef __cplusplus
}
#endif
