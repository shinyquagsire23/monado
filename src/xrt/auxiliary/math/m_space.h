// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for manipulating @ref xrt_pose, @ref xrt_space_relation and
 *         @ref xrt_relation_chain structs.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_math
 */

#pragma once

#include "xrt/xrt_defines.h"

#include "math/m_api.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @addtogroup aux_math
 * @{
 */

/*
 *
 * Pose functions.
 *
 */

static inline bool
m_pose_is_identity(const struct xrt_pose *pose)
{
	struct xrt_pose p = *pose;

	return ((p.position.x == 0.0f || p.position.x == -0.0f) &&       // x
	        (p.position.y == 0.0f || p.position.y == -0.0f) &&       // y
	        (p.position.z == 0.0f || p.position.z == -0.0f) &&       // z
	        (p.orientation.x == 0.0f || p.orientation.x == -0.0f) && // x
	        (p.orientation.y == 0.0f || p.orientation.y == -0.0f) && // y
	        (p.orientation.z == 0.0f || p.orientation.z == -0.0f) && // z
	        (p.orientation.w == 1.0f || p.orientation.w == -1.0f)    // w
	);
}


/*
 *
 * Space relation functions.
 *
 */

static inline void
m_space_relation_from_pose(const struct xrt_pose *pose, struct xrt_space_relation *out_relation)
{
	enum xrt_space_relation_flags flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                                      XRT_SPACE_RELATION_POSITION_VALID_BIT);
	struct xrt_space_relation relation = {
	    flags,
	    *pose,
	    XRT_VEC3_ZERO,
	    XRT_VEC3_ZERO,
	};

	*out_relation = relation;
}

static inline void
m_space_relation_ident(struct xrt_space_relation *out_relation)
{
	struct xrt_pose identity = XRT_POSE_IDENTITY;

	m_space_relation_from_pose(&identity, out_relation);
}

void
m_space_relation_invert(struct xrt_space_relation *relation, struct xrt_space_relation *out_relation);

/*!
 * Linearly interpolate between two relations @p a and @p b. Uses slerp for
 * their orientations. Sets @p flags in @p out_relation.
 */
void
m_space_relation_interpolate(struct xrt_space_relation *a,
                             struct xrt_space_relation *b,
                             float t,
                             enum xrt_space_relation_flags flags,
                             struct xrt_space_relation *out_relation);

/*
 *
 * Relation chain functions.
 *
 */

/*!
 * Reserve a step in the chain and return a pointer to the relation.
 *
 * @note The data pointed to by the returned pointer is not initialized:
 * you must populate it before using @ref m_relation_chain_resolve
 *
 * @public @memberof xrt_relation_chain
 */
static inline struct xrt_space_relation *
m_relation_chain_reserve(struct xrt_relation_chain *xrc)
{
	if (xrc->step_count < XRT_RELATION_CHAIN_CAPACITY) {
		return &xrc->steps[xrc->step_count++];
	}
	return NULL;
}

/*!
 * Append a new relation
 *
 * @public @memberof xrt_relation_chain
 */
static inline void
m_relation_chain_push_relation(struct xrt_relation_chain *xrc, const struct xrt_space_relation *relation)
{
	if (xrc->step_count >= XRT_RELATION_CHAIN_CAPACITY) {
		return;
	}

	xrc->steps[xrc->step_count++] = *relation;
}

/*!
 * Append the inverse of the provided relation.
 *
 * Validity flags stay the same, only the pose and velocities are inverted.
 *
 * @public @memberof xrt_relation_chain
 */
static inline void
m_relation_chain_push_inverted_relation(struct xrt_relation_chain *xrc, const struct xrt_space_relation *relation)
{
	struct xrt_space_relation r = *relation;

	struct xrt_space_relation invert;
	m_space_relation_invert(&r, &invert);
	m_relation_chain_push_relation(xrc, &invert);
}

/*!
 * Append a new pose as a relation without velocity
 *
 * @public @memberof xrt_relation_chain
 */
static inline void
m_relation_chain_push_pose(struct xrt_relation_chain *xrc, const struct xrt_pose *pose)
{
	struct xrt_space_relation relation;
	m_space_relation_from_pose(pose, &relation);
	m_relation_chain_push_relation(xrc, &relation);
}

/*!
 * Append a new pose as a relation without velocity, if it is not the identity pose.
 *
 * @public @memberof xrt_relation_chain
 */
static inline void
m_relation_chain_push_pose_if_not_identity(struct xrt_relation_chain *xrc, const struct xrt_pose *pose)
{
	struct xrt_pose p = *pose;

	if (m_pose_is_identity(&p)) {
		return;
	}

	m_relation_chain_push_pose(xrc, &p);
}

/*!
 * Append the inverse of a pose as a relation without velocity, if it is not the identity pose.
 *
 * Validity flags stay the same, only the pose is inverted.
 *
 * @public @memberof xrt_relation_chain
 */
static inline void
m_relation_chain_push_inverted_pose_if_not_identity(struct xrt_relation_chain *xrc, const struct xrt_pose *pose)
{
	struct xrt_pose p = *pose;

	if (m_pose_is_identity(&p)) {
		return;
	}

	struct xrt_pose invert;
	math_pose_invert(&p, &invert);
	m_relation_chain_push_pose(xrc, &invert);
}

/*!
 * Compute the equivalent single relation from flattening a relation chain.
 *
 * The input chain is not modified.
 *
 * @public @memberof xrt_relation_chain
 */
void
m_relation_chain_resolve(const struct xrt_relation_chain *xrc, struct xrt_space_relation *out_relation);

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
