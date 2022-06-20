// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Util to reinterpret Ceres parameter vectors as hand model parameters
 * @author Moses Turner <moses@collabora.com>
 * @author Charlton Rodda <charlton.rodda@collabora.com>
 * @ingroup tracking
 */

#include "util/u_logging.h"
#include "math/m_api.h"

#include "lm_interface.hpp"
#include "lm_defines.hpp"
#include <cmath>
#include <iostream>

// #include "lm_rotations.hpp"

namespace xrt::tracking::hand::mercury::lm {

template <typename T> struct OptimizerMetacarpalBone
{
	Vec2<T> swing;
	T twist;
};

template <typename T> struct OptimizerFinger
{
	OptimizerMetacarpalBone<T> metacarpal;
	Vec2<T> proximal_swing;
	// Not Vec2.
	T rots[2];
};

template <typename T> struct OptimizerThumb
{
	OptimizerMetacarpalBone<T> metacarpal;
	// Again not Vec2.
	T rots[2];
};

template <typename T> struct OptimizerHand
{
	T hand_size;
	Vec3<T> wrist_location;
	// This is constant, a ceres::Rotation.h quat,, taken from last frame.
	Quat<T> wrist_pre_orientation_quat;
	// This is optimized - angle-axis rotation vector. Starts at 0, loss goes up the higher it goes because it
	// indicates more of a rotation.
	Vec3<T> wrist_post_orientation_aax;
	OptimizerThumb<T> thumb = {};
	OptimizerFinger<T> finger[4] = {};
};


struct minmax
{
	HandScalar min = 0;
	HandScalar max = 0;
};

class FingerLimit
{
public:
	minmax mcp_swing_x = {};
	minmax mcp_swing_y = {};
	minmax mcp_twist = {};

	minmax pxm_swing_x = {};
	minmax pxm_swing_y = {};

	minmax curls[2] = {}; // int, dst
};

class HandLimit
{
public:
	minmax hand_size;

	minmax thumb_mcp_swing_x, thumb_mcp_swing_y, thumb_mcp_twist;
	minmax thumb_curls[2];

	FingerLimit fingers[4];

	HandLimit()
	{
		hand_size = {0.095 - 0.03, 0.095 + 0.03};

		thumb_mcp_swing_x = {rad<HandScalar>(-60), rad<HandScalar>(60)};
		thumb_mcp_swing_y = {rad<HandScalar>(-60), rad<HandScalar>(60)};
		thumb_mcp_twist = {rad<HandScalar>(-35), rad<HandScalar>(35)};

		for (int i = 0; i < 2; i++) {
			thumb_curls[i] = {rad<HandScalar>(-90), rad<HandScalar>(40)};
		}


		constexpr double margin = 0.09;

		fingers[0].mcp_swing_y = {-0.19 - margin, -0.19 + margin};
		fingers[1].mcp_swing_y = {0.00 - margin, 0.00 + margin};
		fingers[2].mcp_swing_y = {0.19 - margin, 0.19 + margin};
		fingers[3].mcp_swing_y = {0.38 - margin, 0.38 + margin};


		for (int finger_idx = 0; finger_idx < 4; finger_idx++) {
			FingerLimit &finger = fingers[finger_idx];

			finger.mcp_swing_x = {rad<HandScalar>(-10), rad<HandScalar>(10)};
			finger.mcp_twist = {rad<HandScalar>(-4), rad<HandScalar>(4)};

			finger.pxm_swing_x = {rad<HandScalar>(-100), rad<HandScalar>(20)}; // ??? why is it reversed
			finger.pxm_swing_y = {rad<HandScalar>(-20), rad<HandScalar>(20)};

			for (int i = 0; i < 2; i++) {
				finger.curls[i] = {rad<HandScalar>(-90), rad<HandScalar>(10)};
			}
		}
	}
};

static const class HandLimit the_limit = {};


constexpr HandScalar hand_size_min = 0.095 - 0.03;
constexpr HandScalar hand_size_max = 0.095 + 0.03;

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
OptimizerHandUnpackFromVector(const T *in, bool use_hand_size, T hand_size, OptimizerHand<T> &out)
{

	size_t acc_idx = 0;
#ifdef USE_HAND_TRANSLATION
	out.wrist_location.x = in[acc_idx++];
	out.wrist_location.y = in[acc_idx++];
	out.wrist_location.z = in[acc_idx++];
#endif
#ifdef USE_HAND_ORIENTATION
	out.wrist_post_orientation_aax.x = in[acc_idx++];
	out.wrist_post_orientation_aax.y = in[acc_idx++];
	out.wrist_post_orientation_aax.z = in[acc_idx++];
#endif

#ifdef USE_EVERYTHING_ELSE

	out.thumb.metacarpal.swing.x = LMToModel(in[acc_idx++], the_limit.thumb_mcp_swing_x);
	out.thumb.metacarpal.swing.y = LMToModel(in[acc_idx++], the_limit.thumb_mcp_swing_y);
	out.thumb.metacarpal.twist = LMToModel(in[acc_idx++], the_limit.thumb_mcp_twist);

	out.thumb.rots[0] = LMToModel(in[acc_idx++], the_limit.thumb_curls[0]);
	out.thumb.rots[1] = LMToModel(in[acc_idx++], the_limit.thumb_curls[1]);

	for (int finger_idx = 0; finger_idx < 4; finger_idx++) {

		out.finger[finger_idx].metacarpal.swing.x =
		    LMToModel(in[acc_idx++], the_limit.fingers[finger_idx].mcp_swing_x);

		out.finger[finger_idx].metacarpal.swing.y =
		    LMToModel(in[acc_idx++], the_limit.fingers[finger_idx].mcp_swing_y);

		out.finger[finger_idx].metacarpal.twist =
		    LMToModel(in[acc_idx++], the_limit.fingers[finger_idx].mcp_twist);


		out.finger[finger_idx].proximal_swing.x =
		    LMToModel(in[acc_idx++], the_limit.fingers[finger_idx].pxm_swing_x);
		out.finger[finger_idx].proximal_swing.y =
		    LMToModel(in[acc_idx++], the_limit.fingers[finger_idx].pxm_swing_y);

		out.finger[finger_idx].rots[0] = LMToModel(in[acc_idx++], the_limit.fingers[finger_idx].curls[0]);
		out.finger[finger_idx].rots[1] = LMToModel(in[acc_idx++], the_limit.fingers[finger_idx].curls[1]);
	}
#endif

#ifdef USE_HAND_SIZE
	if (use_hand_size) {
		out.hand_size = LMToModel(in[acc_idx++], the_limit.hand_size);
	} else {
		out.hand_size = hand_size;
	}
#endif
}

template <typename T>
void
OptimizerHandPackIntoVector(OptimizerHand<T> &in, bool use_hand_size, T *out)
{
	size_t acc_idx = 0;

#ifdef USE_HAND_TRANSLATION
	out[acc_idx++] = in.wrist_location.x;
	out[acc_idx++] = in.wrist_location.y;
	out[acc_idx++] = in.wrist_location.z;
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
		out[acc_idx++] =
		    ModelToLM(in.finger[finger_idx].metacarpal.swing.x, the_limit.fingers[finger_idx].mcp_swing_x);
		out[acc_idx++] =
		    ModelToLM(in.finger[finger_idx].metacarpal.swing.y, the_limit.fingers[finger_idx].mcp_swing_y);
		out[acc_idx++] =
		    ModelToLM(in.finger[finger_idx].metacarpal.twist, the_limit.fingers[finger_idx].mcp_twist);

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
	opt.hand_size = (T)(0.095);

	opt.wrist_post_orientation_aax.x = (T)(0);
	opt.wrist_post_orientation_aax.y = (T)(0);
	opt.wrist_post_orientation_aax.z = (T)(0);

	// opt.store_wrist_pre_orientation_quat = pre_rotation;

	opt.wrist_pre_orientation_quat.w = (T)pre_rotation.w;
	opt.wrist_pre_orientation_quat.x = (T)pre_rotation.x;
	opt.wrist_pre_orientation_quat.y = (T)pre_rotation.y;
	opt.wrist_pre_orientation_quat.z = (T)pre_rotation.z;

	opt.wrist_location.x = (T)(0);
	opt.wrist_location.y = (T)(0);
	opt.wrist_location.z = (T)(-0.3);


	for (int i = 0; i < 4; i++) {
		//!@todo needed?
		opt.finger[i].metacarpal.swing.x = T(0);
		opt.finger[i].metacarpal.twist = T(0);

		opt.finger[i].proximal_swing.x = rad<T>((T)(15));
		opt.finger[i].rots[0] = rad<T>((T)(-5));
		opt.finger[i].rots[1] = rad<T>((T)(-5));
	}

	opt.thumb.metacarpal.swing.x = (T)(0);
	opt.thumb.metacarpal.swing.y = (T)(0);
	opt.thumb.metacarpal.twist = (T)(0);

	opt.thumb.rots[0] = rad<T>((T)(-5));
	opt.thumb.rots[1] = rad<T>((T)(-59));

	opt.finger[0].metacarpal.swing.y = (T)(-0.19);
	opt.finger[1].metacarpal.swing.y = (T)(0);
	opt.finger[2].metacarpal.swing.y = (T)(0.19);
	opt.finger[3].metacarpal.swing.y = (T)(0.38);

	opt.finger[0].proximal_swing.y = (T)(-0.01);
	opt.finger[1].proximal_swing.y = (T)(0);
	opt.finger[2].proximal_swing.y = (T)(0.01);
	opt.finger[3].proximal_swing.y = (T)(0.02);
}

// Applies the post axis-angle rotation to the pre quat, then zeroes out the axis-angle.
template <typename T>
void
OptimizerHandSquashRotations(OptimizerHand<T> &opt, Quat<T> &out_orientation)
{

	// Hmmmmm, is this at all the right thing to do? :
	opt.wrist_pre_orientation_quat.w = (T)out_orientation.w;
	opt.wrist_pre_orientation_quat.x = (T)out_orientation.x;
	opt.wrist_pre_orientation_quat.y = (T)out_orientation.y;
	opt.wrist_pre_orientation_quat.z = (T)out_orientation.z;

	Quat<T> &pre_rotation = opt.wrist_pre_orientation_quat;

	Quat<T> post_rotation;

	AngleAxisToQuaternion(opt.wrist_post_orientation_aax, post_rotation);

	Quat<T> tmp_new_pre_rotation;

	QuaternionProduct(pre_rotation, post_rotation, tmp_new_pre_rotation);

	out_orientation.w = tmp_new_pre_rotation.w;
	out_orientation.x = tmp_new_pre_rotation.x;
	out_orientation.y = tmp_new_pre_rotation.y;
	out_orientation.z = tmp_new_pre_rotation.z;

	opt.wrist_post_orientation_aax.x = T(0);
	opt.wrist_post_orientation_aax.y = T(0);
	opt.wrist_post_orientation_aax.z = T(0);
}


} // namespace xrt::tracking::hand::mercury::lm
