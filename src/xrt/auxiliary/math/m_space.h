// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for manipulating @ref xrt_pose, @ref xrt_space_relation and
 *         @ref xrt_space_graph structs.
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
 * @ingroup aux_math
 * @{
 */

/*
 *
 * Pose functions.
 *
 */

static inline bool
m_pose_is_identity(struct xrt_pose *pose)
{
	struct xrt_pose p = *pose;

	if ((p.position.x == 0.0f || p.position.x == -0.0f) &&       // x
	    (p.position.y == 0.0f || p.position.y == -0.0f) &&       // y
	    (p.position.z == 0.0f || p.position.z == -0.0f) &&       // z
	    (p.orientation.x == 0.0f || p.orientation.x == -0.0f) && // x
	    (p.orientation.y == 0.0f || p.orientation.y == -0.0f) && // y
	    (p.orientation.z == 0.0f || p.orientation.z == -0.0f) && // z
	    (p.orientation.w == 1.0f || p.orientation.w == -1.0f)    // w
	) {
		return true;
	}

	return false;
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
	    {0, 0, 0},
	    {0, 0, 0},
	};

	*out_relation = relation;
}

static inline void
m_space_relation_ident(struct xrt_space_relation *out_relation)
{
	struct xrt_pose identity = {
	    {0, 0, 0, 1},
	    {0, 0, 0},
	};

	m_space_relation_from_pose(&identity, out_relation);
}

void
m_space_relation_invert(struct xrt_space_relation *relation, struct xrt_space_relation *out_relation);

/*
 *
 * Space graph functions.
 *
 */

/*!
 * Reserve a step in the graph and return a pointer to the relation.
 */
static inline struct xrt_space_relation *
m_space_graph_reserve(struct xrt_space_graph *xsg)
{
	if (xsg->num_steps < XRT_SPACE_GRAPHS_MAX) {
		return &xsg->steps[xsg->num_steps++];
	} else {
		return NULL;
	}
}

/*!
 * Flattens a space graph into a single relation.
 */
static inline void
m_space_graph_add_relation(struct xrt_space_graph *xsg, const struct xrt_space_relation *relation)
{
	if (xsg->num_steps >= XRT_SPACE_GRAPHS_MAX) {
		return;
	}

	xsg->steps[xsg->num_steps++] = *relation;
}

static inline void
m_space_graph_add_inverted_relation(struct xrt_space_graph *xsg, const struct xrt_space_relation *relation)
{
	struct xrt_space_relation r = *relation;

	struct xrt_space_relation invert;
	m_space_relation_invert(&r, &invert);
	m_space_graph_add_relation(xsg, &invert);
}

static inline void
m_space_graph_add_pose(struct xrt_space_graph *xsg, const struct xrt_pose *pose)
{
	struct xrt_space_relation relation;
	m_space_relation_from_pose(pose, &relation);
	m_space_graph_add_relation(xsg, &relation);
}

static inline void
m_space_graph_add_pose_if_not_identity(struct xrt_space_graph *xsg, const struct xrt_pose *pose)
{
	struct xrt_pose p = *pose;

	if (m_pose_is_identity(&p)) {
		return;
	}

	m_space_graph_add_pose(xsg, &p);
}

static inline void
m_space_graph_add_inverted_pose_if_not_identity(struct xrt_space_graph *xsg, const struct xrt_pose *pose)
{
	struct xrt_pose p = *pose;

	if (m_pose_is_identity(&p)) {
		return;
	}

	struct xrt_pose invert;
	math_pose_invert(&p, &invert);
	m_space_graph_add_pose(xsg, &invert);
}

/*!
 * Flattens a space graph into a single relation.
 */
void
m_space_graph_resolve(const struct xrt_space_graph *xsg, struct xrt_space_relation *out_relation);

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
