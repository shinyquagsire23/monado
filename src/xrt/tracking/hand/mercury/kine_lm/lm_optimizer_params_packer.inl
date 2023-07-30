// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Util to reinterpret Ceres parameter vectors as hand model parameters
 * @author Moses Turner <moses@collabora.com>
 * @author Charlton Rodda <charlton.rodda@collabora.com>
 * @ingroup tracking
 */

// #include <iostream>
// #include <cmath>
#include "util/u_logging.h"
#include "math/m_api.h"

#include "lm_interface.hpp"
#include "lm_defines.hpp"
#include "lm_rotations.inl"

namespace xrt::tracking::hand::mercury::lm {



template <typename T>
inline T
LMToModel(T lm, minmax mm)
{
	return mm.min + ((sin(lm) + T(1)) * ((mm.max - mm.min) * T(.5)));
}

template <typename T>
inline T
ModelToLM(T model, minmax mm)
{
	return asin((2 * (model - mm.min) / (mm.max - mm.min)) - 1);
}

// Input vector,
template <typename T>
void
OptimizerHandUnpackFromVector(const T *in, const KinematicHandLM &state, OptimizerHand<T> &out)
{

	const Quat<T> pre_wrist_orientation(state.this_frame_pre_rotation);
	const Vec3<T> pre_wrist_position(state.this_frame_pre_position);

	size_t acc_idx = 0;
#ifdef USE_HAND_TRANSLATION
	out.wrist_post_location.x = in[acc_idx++];
	out.wrist_post_location.y = in[acc_idx++];
	out.wrist_post_location.z = in[acc_idx++];

	out.wrist_final_location.x = out.wrist_post_location.x + T(pre_wrist_position.x);
	out.wrist_final_location.y = out.wrist_post_location.y + T(pre_wrist_position.y);
	out.wrist_final_location.z = out.wrist_post_location.z + T(pre_wrist_position.z);

#endif

#ifdef USE_HAND_ORIENTATION
	out.wrist_post_orientation_aax.x = in[acc_idx++];
	out.wrist_post_orientation_aax.y = in[acc_idx++];
	out.wrist_post_orientation_aax.z = in[acc_idx++];

	Quat<T> post_wrist_orientation = {};

	AngleAxisToQuaternion<T>(out.wrist_post_orientation_aax, post_wrist_orientation);

	Quat<T> pre_wrist_orientation_t(pre_wrist_orientation);

	QuaternionProduct<T>(pre_wrist_orientation_t, post_wrist_orientation, out.wrist_final_orientation);
#endif

#ifdef USE_EVERYTHING_ELSE

	out.thumb.metacarpal.swing.x = LMToModel(in[acc_idx++], the_limit.thumb_mcp_swing_x);
	out.thumb.metacarpal.swing.y = LMToModel(in[acc_idx++], the_limit.thumb_mcp_swing_y);
	out.thumb.metacarpal.twist = LMToModel(in[acc_idx++], the_limit.thumb_mcp_twist);

	out.thumb.rots[0] = LMToModel(in[acc_idx++], the_limit.thumb_curls[0]);
	out.thumb.rots[1] = LMToModel(in[acc_idx++], the_limit.thumb_curls[1]);

	for (int finger_idx = 0; finger_idx < 4; finger_idx++) {
		// Note that we are not unpacking the metacarpal swing/twist as it is constant.
		out.finger[finger_idx].proximal_swing.x =
		    LMToModel(in[acc_idx++], the_limit.fingers[finger_idx].pxm_swing_x);
		out.finger[finger_idx].proximal_swing.y =
		    LMToModel(in[acc_idx++], the_limit.fingers[finger_idx].pxm_swing_y);

		out.finger[finger_idx].rots[0] = LMToModel(in[acc_idx++], the_limit.fingers[finger_idx].curls[0]);
		out.finger[finger_idx].rots[1] = LMToModel(in[acc_idx++], the_limit.fingers[finger_idx].curls[1]);
	}
#endif

#ifdef USE_HAND_SIZE
	if (state.optimize_hand_size) {
		out.hand_size = LMToModel(in[acc_idx++], the_limit.hand_size);
	} else {
		out.hand_size = T(state.target_hand_size);
	}
#endif
}

template <typename T>
void
OptimizerHandPackIntoVector(OptimizerHand<T> &in, bool use_hand_size, T *out)
{
	size_t acc_idx = 0;

#ifdef USE_HAND_TRANSLATION
	out[acc_idx++] = in.wrist_post_location.x;
	out[acc_idx++] = in.wrist_post_location.y;
	out[acc_idx++] = in.wrist_post_location.z;
#endif
#ifdef USE_HAND_ORIENTATION
	out[acc_idx++] = in.wrist_post_orientation_aax.x;
	out[acc_idx++] = in.wrist_post_orientation_aax.y;
	out[acc_idx++] = in.wrist_post_orientation_aax.z;
#endif
#ifdef USE_EVERYTHING_ELSE
	out[acc_idx++] = ModelToLM(in.thumb.metacarpal.swing.x, the_limit.thumb_mcp_swing_x);
	out[acc_idx++] = ModelToLM(in.thumb.metacarpal.swing.y, the_limit.thumb_mcp_swing_y);
	out[acc_idx++] = ModelToLM(in.thumb.metacarpal.twist, the_limit.thumb_mcp_twist);

	out[acc_idx++] = ModelToLM(in.thumb.rots[0], the_limit.thumb_curls[0]);
	out[acc_idx++] = ModelToLM(in.thumb.rots[1], the_limit.thumb_curls[1]);

	for (int finger_idx = 0; finger_idx < 4; finger_idx++) {
		// Note that we are not packing the metacarpal swing/twist as it is constant.
		out[acc_idx++] =
		    ModelToLM(in.finger[finger_idx].proximal_swing.x, the_limit.fingers[finger_idx].pxm_swing_x);
		out[acc_idx++] =
		    ModelToLM(in.finger[finger_idx].proximal_swing.y, the_limit.fingers[finger_idx].pxm_swing_y);

		out[acc_idx++] = ModelToLM(in.finger[finger_idx].rots[0], the_limit.fingers[finger_idx].curls[0]);
		out[acc_idx++] = ModelToLM(in.finger[finger_idx].rots[1], the_limit.fingers[finger_idx].curls[1]);
	}
#endif

#ifdef USE_HAND_SIZE
	if (use_hand_size) {
		out[acc_idx++] = ModelToLM(in.hand_size, the_limit.hand_size);
	}
#endif
}

template <typename T>
void
OptimizerHandInit(OptimizerHand<T> &opt, Quat<T> &pre_rotation)
{
	opt.hand_size = (T)STANDARD_HAND_SIZE;

	opt.wrist_post_orientation_aax.x = (T)(0);
	opt.wrist_post_orientation_aax.y = (T)(0);
	opt.wrist_post_orientation_aax.z = (T)(0);


	// opt.wrist_pre_orientation_quat = pre_rotation;

	opt.wrist_post_location.x = (T)(0);
	opt.wrist_post_location.y = (T)(0);
	opt.wrist_post_location.z = (T)(0);


	for (int i = 0; i < 4; i++) {
		//!@todo needed?
		opt.finger[i].metacarpal.swing.x = T(0);
		opt.finger[i].metacarpal.twist = T(0);

		opt.finger[i].proximal_swing.x = T(rad<HandScalar>(15.0f));
		opt.finger[i].rots[0] = T(rad<HandScalar>(-5));
		opt.finger[i].rots[1] = T(rad<HandScalar>(-5));
	}

	opt.thumb.metacarpal.swing.x = (T)(0);
	opt.thumb.metacarpal.swing.y = (T)(0);
	opt.thumb.metacarpal.twist = (T)(0);

	opt.thumb.rots[0] = T(rad<HandScalar>(-5));
	opt.thumb.rots[1] = T(rad<HandScalar>(-59));

	opt.finger[0].metacarpal.swing.y = (T)(-0.19);
	opt.finger[1].metacarpal.swing.y = (T)(0);
	opt.finger[2].metacarpal.swing.y = (T)(0.19);
	opt.finger[3].metacarpal.swing.y = (T)(0.38);

	opt.finger[0].metacarpal.swing.x = (T)(-0.02);
	opt.finger[1].metacarpal.swing.x = (T)(0);
	opt.finger[2].metacarpal.swing.x = (T)(0.02);
	opt.finger[3].metacarpal.swing.x = (T)(0.04);

	opt.finger[0].proximal_swing.y = (T)(-0.01);
	opt.finger[1].proximal_swing.y = (T)(0);
	opt.finger[2].proximal_swing.y = (T)(0.01);
	opt.finger[3].proximal_swing.y = (T)(0.02);
}


} // namespace xrt::tracking::hand::mercury::lm
