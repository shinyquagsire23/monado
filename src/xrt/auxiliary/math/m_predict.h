// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple function to predict a new pose from a given pose.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_math
 */

#pragma once

#include "xrt/xrt_defines.h"


/*!
 * Using the given @p xrt_space_relation predicts a new @p xrt_space_relation
 * @p delta_s into the future.
 *
 * Assumes that angular velocity is relative to the space the relation is in,
 * not relative to relation::pose.
 *
 * @aux_math
 */
void
m_predict_relation(const struct xrt_space_relation *rel, double delta_s, struct xrt_space_relation *out_rel);
