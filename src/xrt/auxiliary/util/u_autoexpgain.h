// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Automatically compute exposure and gain values from an image stream
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_frame.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//! An auto exposure/gain strategy tunes the algorithm for specific objectives.
enum u_aeg_strategy
{
	U_AEG_STRATEGY_TRACKING = 0,  //!< Lower exposure and gain at the cost of darker images.
	U_AEG_STRATEGY_DYNAMIC_RANGE, //!< Tries to maximize the image information
	U_AEG_STRATEGY_COUNT
};

struct u_autoexpgain;

/*!
 * Create auto exposure and gain (AEG) algorithm object.
 *
 * @param strategy What objective is preferred for the algorithm.
 * @param enabled_from_start Update exposure/gain from the start.
 * @param frame_delay About how many frames does it take for exp and gain to settle in.
 * @return struct u_autoexpgain* Created object
 */
struct u_autoexpgain *
u_autoexpgain_create(enum u_aeg_strategy strategy, bool enabled_from_start, int frame_delay);

//! Setup UI for the AEG algorithm
void
u_autoexpgain_add_vars(struct u_autoexpgain *aeg, void *root, char *prefix);

//! Update the AEG with a frame
void
u_autoexpgain_update(struct u_autoexpgain *aeg, struct xrt_frame *xf);

//! Get currently computed exposure value in usecs.
float
u_autoexpgain_get_exposure(struct u_autoexpgain *aeg);

//! Get currently computed gain value in the [0, 255] range.
float
u_autoexpgain_get_gain(struct u_autoexpgain *aeg);

//! Destroy AEG object
void
u_autoexpgain_destroy(struct u_autoexpgain **aeg);

#ifdef __cplusplus
}
#endif
