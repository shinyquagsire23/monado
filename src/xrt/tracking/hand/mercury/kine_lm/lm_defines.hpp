// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Defines for Levenberg-Marquardt kinematic optimizer
 * @author Moses Turner <moses@collabora.com>
 * @author Charlton Rodda <charlton.rodda@collabora.com>
 * @ingroup tracking
 */
#pragma once

#include "math/m_mathinclude.h"
#include "math/m_eigen_interop.hpp"
#include "util/u_logging.h"
#include "../kine_common.hpp"

namespace xrt::tracking::hand::mercury::lm {

#define LM_TRACE(lmh, ...) U_LOG_IFL_T(lmh.log_level, __VA_ARGS__)
#define LM_DEBUG(lmh, ...) U_LOG_IFL_D(lmh.log_level, __VA_ARGS__)
#define LM_INFO(lmh, ...) U_LOG_IFL_I(lmh.log_level, __VA_ARGS__)
#define LM_WARN(lmh, ...) U_LOG_IFL_W(lmh.log_level, __VA_ARGS__)
#define LM_ERROR(lmh, ...) U_LOG_IFL_E(lmh.log_level, __VA_ARGS__)

// Inlines.
template <typename T>
inline T
rad(T degrees)
{
	return degrees * T(M_PI / 180.f);
}

// Number of joints that our ML models output.
static constexpr size_t kNumNNJoints = 21;

static constexpr size_t kNumFingers = 5;

// This is a lie for the thumb; we usually do the hidden metacarpal trick there
static constexpr size_t kNumJointsInFinger = 5;

static constexpr size_t kNumOrientationsInFinger = 4;

// These defines look silly, but they are _extremely_ useful for doing work on this optimizer. Please don't remove them.
#define USE_HAND_SIZE
#define USE_HAND_TRANSLATION
#define USE_HAND_ORIENTATION
#define USE_EVERYTHING_ELSE

// Not tested/tuned well enough; might make tracking slow.
#undef USE_HAND_PLAUSIBILITY
// Should work, but our neural nets aren't good enough yet.
#undef USE_HAND_CURLS

#undef RESIDUALS_HACKING

static constexpr size_t kMetacarpalBoneDim = 3;
static constexpr size_t kProximalBoneDim = 2;
static constexpr size_t kFingerDim = kProximalBoneDim + 2;
static constexpr size_t kThumbDim = kMetacarpalBoneDim + 2;
static constexpr size_t kHandSizeDim = 1;
static constexpr size_t kHandTranslationDim = 3;
static constexpr size_t kHandOrientationDim = 3;


// HRTC = Hand Residual Temporal Consistency
static constexpr size_t kHRTC_HandSize = 1;
static constexpr size_t kHRTC_RootBoneTranslation = 3;
static constexpr size_t kHRTC_RootBoneOrientation = 3; // Direct difference between the two angle-axis rotations. This
                                                       // works well enough because the rotation should be small.

static constexpr size_t kHRTC_ThumbMCPSwingTwist = 3;
static constexpr size_t kHRTC_ThumbCurls = 2;

static constexpr size_t kHRTC_ProximalSimilarity = 2;

static constexpr size_t kHRTC_FingerMCPSwingTwist = 0;
static constexpr size_t kHRTC_FingerPXMSwing = 2;
static constexpr size_t kHRTC_FingerCurls = 2;
static constexpr size_t kHRTC_CurlSimilarity = 1;


static constexpr size_t kHandResidualOneSideXY = (kNumNNJoints * 2);
static constexpr size_t kHandResidualOneSideDepth = 20; // one less because midxpm joint isn't used
#ifdef USE_HAND_CURLS
static constexpr size_t kHandResidualOneSideMatchCurls = 4;
#else
static constexpr size_t kHandResidualOneSideMatchCurls = 0;
#endif
static constexpr size_t kHandResidualOneSideSize =
    kHandResidualOneSideXY + kHandResidualOneSideDepth + kHandResidualOneSideMatchCurls;

static constexpr size_t kHandResidualTemporalConsistencyOneFingerSize = //
    kHRTC_FingerMCPSwingTwist +                                         //
    kHRTC_FingerPXMSwing +                                              //
    kHRTC_FingerCurls +                                                 //
#ifdef USE_HAND_PLAUSIBILITY                                            //
    kHRTC_CurlSimilarity +                                              //
#endif                                                                  //
    0;

static constexpr size_t kHandResidualTemporalConsistencySize = //
    kHRTC_RootBoneTranslation +                                //
    kHRTC_RootBoneOrientation +                                //
    kHRTC_ThumbMCPSwingTwist +                                 //
    kHRTC_ThumbCurls +                                         //
#ifdef USE_HAND_PLAUSIBILITY                                   //
    kHRTC_ProximalSimilarity +                                 //
#endif                                                         //
    (kHandResidualTemporalConsistencyOneFingerSize * 4) +      //
    0;


class HandStability
{
public:
	HandScalar stabilityRoot;
	HandScalar stabilityCurlRoot;
	HandScalar stabilityOtherRoot;

	HandScalar stabilityThumbMCPSwing;
	HandScalar stabilityThumbMCPTwist;

	HandScalar stabilityFingerMCPSwing;
	HandScalar stabilityFingerMCPTwist;

	HandScalar stabilityFingerPXMSwingX;
	HandScalar stabilityFingerPXMSwingY;

	HandScalar stabilityRootPosition;
	HandScalar stabilityHandSize;

	HandScalar stabilityHandOrientationZ;
	HandScalar stabilityHandOrientationXY;
	HandStability(float root = 15.0f)
	{
		this->stabilityRoot = root;
		this->stabilityCurlRoot = this->stabilityRoot * 0.03f;
		this->stabilityOtherRoot = this->stabilityRoot * 0.03f;

		this->stabilityThumbMCPSwing = this->stabilityCurlRoot * 1.5f;
		this->stabilityThumbMCPTwist = this->stabilityCurlRoot * 1.5f;

		this->stabilityFingerMCPSwing = this->stabilityCurlRoot * 3.0f;
		this->stabilityFingerMCPTwist = this->stabilityCurlRoot * 3.0f;

		this->stabilityFingerPXMSwingX = this->stabilityCurlRoot * 0.6f;
		this->stabilityFingerPXMSwingY = this->stabilityCurlRoot * 1.6f;

		this->stabilityRootPosition = this->stabilityOtherRoot * 25;
		this->stabilityHandSize = this->stabilityOtherRoot * 1000;

		this->stabilityHandOrientationZ = this->stabilityOtherRoot * 0.5;
		this->stabilityHandOrientationXY = this->stabilityOtherRoot * 0.8;
	}
};


static constexpr HandScalar kPlausibilityRoot = 1.0;
static constexpr HandScalar kPlausibilityProximalSimilarity = 0.05f * kPlausibilityRoot;

static constexpr HandScalar kPlausibilityCurlSimilarityHard = 0.10f * kPlausibilityRoot;
static constexpr HandScalar kPlausibilityCurlSimilaritySoft = 0.05f * kPlausibilityRoot;


constexpr size_t
calc_input_size(bool optimize_hand_size)
{
	size_t out = 0;

#ifdef USE_HAND_TRANSLATION
	out += kHandTranslationDim;
#endif

#ifdef USE_HAND_ORIENTATION
	out += kHandOrientationDim;
#endif

#ifdef USE_EVERYTHING_ELSE
	out += kThumbDim;
	out += (kFingerDim * 4);
#endif

#ifdef USE_HAND_SIZE
	if (optimize_hand_size) {
		out += kHandSizeDim;
	}
#endif

	return out;
}

constexpr size_t
calc_residual_size(bool stability, bool optimize_hand_size, int num_views)
{
	size_t out = 0;
	for (int i = 0; i < num_views; i++) {
		out += kHandResidualOneSideSize;
	}

	if (stability) {
		out += kHandResidualTemporalConsistencySize;
	}

	if (optimize_hand_size) {
		out += kHRTC_HandSize;
	}

	return out;
}

// Some templatable spatial types.
// Heavily inspired by Eigen - one can definitely use Eigen instead, but here I'd rather have more control

template <typename Scalar> struct Quat
{
	Scalar x = {};
	Scalar y = {};
	Scalar z = {};
	Scalar w = {};

	/// Default constructor - DOES NOT INITIALIZE VALUES
	constexpr Quat() {}

	/// Copy constructor
	constexpr Quat(Quat const &) noexcept(std::is_nothrow_copy_constructible_v<Scalar>) = default;

	/// Move constructor
	Quat(Quat &&) noexcept(std::is_nothrow_move_constructible_v<Scalar>) = default;

	/// Copy assignment
	Quat &
	operator=(Quat const &) = default;

	/// Move assignment
	Quat &
	operator=(Quat &&) noexcept = default;

	/// Construct from x, y, z, w scalars
	template <typename Other>
	constexpr Quat(Other x, Other y, Other z, Other w) noexcept // NOLINT(bugprone-easily-swappable-parameters)
	    : x{Scalar(x)}, y{Scalar(y)}, z{Scalar(z)}, w{Scalar(w)}
	{}

	/// So that we can copy a regular Vec2 into the real part of a Jet Vec2
	template <typename Other> Quat(Quat<Other> const &other) : Quat(other.x, other.y, other.z, other.w) {}

	static Quat
	Identity()
	{
		return Quat(0.f, 0.f, 0.f, 1.f);
	}
};

template <typename Scalar> struct Vec3
{
	// Note that these are not initialized, for performance reasons.
	// If you want them initialized, use Zero() or something else
	Scalar x = {};
	Scalar y = {};
	Scalar z = {};

	/// Default constructor - DOES NOT INITIALIZE VALUES
	constexpr Vec3() {}
	/// Copy constructor
	constexpr Vec3(Vec3 const &other) noexcept(std::is_nothrow_copy_constructible_v<Scalar>) = default;

	/// Move constructor
	Vec3(Vec3 &&) noexcept(std::is_nothrow_move_constructible_v<Scalar>) = default;

	/// Copy assignment
	Vec3 &
	operator=(Vec3 const &) = default;

	/// Move assignment
	Vec3 &
	operator=(Vec3 &&) noexcept = default;


	template <typename Other>
	constexpr Vec3(Other x, Other y, Other z) noexcept // NOLINT(bugprone-easily-swappable-parameters)
	    : x{Scalar(x)}, y{Scalar(y)}, z{Scalar(z)}
	{}

	template <typename Other> Vec3(Vec3<Other> const &other) : Vec3(other.x, other.y, other.z) {}

	static Vec3
	Zero()
	{
		return Vec3(0.f, 0.f, 0.f);
	}

	Scalar
	norm_sqrd() const
	{
		Scalar len_sqrd = (Scalar)(0);

		len_sqrd += this->x * this->x;
		len_sqrd += this->y * this->y;
		len_sqrd += this->z * this->z;
		return len_sqrd;
	}

	// Norm, vector length, whatever.
	// WARNING: Can return NaNs in the derivative part of Jets if magnitude is 0, because d/dx(sqrt(x)) at x=0 is
	// undefined.
	// There's no norm_safe because generally you need to add zero-checks somewhere *before* calling
	// this, and it's not possible to produce correct derivatives from here.
	Scalar
	norm() const
	{
		Scalar len_sqrd = this->norm_sqrd();

		return sqrt(len_sqrd);
	}

	// WARNING: Will return NaNs if vector magnitude is zero due to zero division.
	// Do not call this on vectors with zero norm.
	Vec3
	normalized() const
	{
		Scalar norm = this->norm();

		Vec3<Scalar> retval;
		retval.x = this->x / norm;
		retval.y = this->y / norm;
		retval.z = this->z / norm;
		return retval;
	}
};

template <typename Scalar> struct Vec2
{
	Scalar x = {};
	Scalar y = {};

	/// Default constructor - DOES NOT INITIALIZE VALUES
	constexpr Vec2() noexcept {}

	/// Copy constructor
	constexpr Vec2(Vec2 const &) noexcept(std::is_nothrow_copy_constructible_v<Scalar>) = default;

	/// Move constructor
	constexpr Vec2(Vec2 &&) noexcept(std::is_nothrow_move_constructible_v<Scalar>) = default;

	/// Copy assignment
	Vec2 &
	operator=(Vec2 const &) = default;

	/// Move assignment
	Vec2 &
	operator=(Vec2 &&) noexcept = default;

	/// So that we can copy a regular Vec2 into the real part of a Jet Vec2
	template <typename Other>
	Vec2(Other x, Other y) // NOLINT(bugprone-easily-swappable-parameters)
	    noexcept(std::is_nothrow_constructible_v<Scalar, Other>)
	    : x{Scalar(x)}, y{Scalar(y)}
	{}

	template <typename Other>
	Vec2(Vec2<Other> const &other) noexcept(std::is_nothrow_constructible_v<Scalar, Other>) : Vec2(other.x, other.y)
	{}

	static constexpr Vec2
	Zero()
	{
		return Vec2(0.f, 0.f);
	}
};

template <typename T> struct ResidualHelper
{
	T *out_residual = nullptr;
	size_t out_residual_idx = 0;

	ResidualHelper(T *residual) : out_residual(residual)
	{
		out_residual_idx = 0;
	}

	void
	AddValue(T const &value)
	{
		this->out_residual[out_residual_idx++] = value;
	}
};


template <typename T> struct OptimizerMetacarpalBone
{
	Vec2<T> swing = {};
	T twist = {};
};

template <typename T> struct OptimizerFinger
{
	OptimizerMetacarpalBone<T> metacarpal = {};
	Vec2<T> proximal_swing = {};
	// Not Vec2.
	T rots[2] = {};
};

template <typename T> struct OptimizerThumb
{
	OptimizerMetacarpalBone<T> metacarpal = {};
	// Again not Vec2.
	T rots[2] = {};
};

template <typename T> struct OptimizerHand
{
	T hand_size;
	Vec3<T> wrist_post_location = {};
	Vec3<T> wrist_post_orientation_aax = {};

	Vec3<T> wrist_final_location = {};
	Quat<T> wrist_final_orientation = {};

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
	minmax hand_size = {};

	minmax thumb_mcp_swing_x = {};
	minmax thumb_mcp_swing_y = {};
	minmax thumb_mcp_twist = {};
	minmax thumb_curls[2] = {};

	FingerLimit fingers[4] = {};

	HandLimit()
	{
		hand_size = {MIN_HAND_SIZE, MAX_HAND_SIZE};

		thumb_mcp_swing_x = {rad<HandScalar>(-60), rad<HandScalar>(60)};
		thumb_mcp_swing_y = {rad<HandScalar>(-60), rad<HandScalar>(60)};
		thumb_mcp_twist = {rad<HandScalar>(-35), rad<HandScalar>(35)};

		for (int i = 0; i < 2; i++) {
			thumb_curls[i] = {rad<HandScalar>(-90), rad<HandScalar>(40)};
		}


		HandScalar margin = 0.0001;

		fingers[0].mcp_swing_y = {HandScalar(-0.19) - margin, HandScalar(-0.19) + margin};
		fingers[1].mcp_swing_y = {HandScalar(0.00) - margin, HandScalar(0.00) + margin};
		fingers[2].mcp_swing_y = {HandScalar(0.19) - margin, HandScalar(0.19) + margin};
		fingers[3].mcp_swing_y = {HandScalar(0.38) - margin, HandScalar(0.38) + margin};


		fingers[0].mcp_swing_x = {HandScalar(-0.02) - margin, HandScalar(-0.02) + margin};
		fingers[1].mcp_swing_x = {HandScalar(0.00) - margin, HandScalar(0.00) + margin};
		fingers[2].mcp_swing_x = {HandScalar(0.02) - margin, HandScalar(0.02) + margin};
		fingers[3].mcp_swing_x = {HandScalar(0.04) - margin, HandScalar(0.04) + margin};


		for (int finger_idx = 0; finger_idx < 4; finger_idx++) {
			FingerLimit &finger = fingers[finger_idx];

			// finger.mcp_swing_x = {rad<HandScalar>(-0.0001), rad<HandScalar>(0.0001)};
			finger.mcp_twist = {rad<HandScalar>(-4), rad<HandScalar>(4)};

			finger.pxm_swing_x = {rad<HandScalar>(-100), rad<HandScalar>(20)}; // ??? why is it reversed
			finger.pxm_swing_y = {rad<HandScalar>(-20), rad<HandScalar>(20)};

			for (int i = 0; i < 2; i++) {
				finger.curls[i] = {rad<HandScalar>(-90), rad<HandScalar>(0)};
			}
		}
	}
};

static const class HandLimit the_limit = {};


template <typename T> struct StereographicObservation
{
	Vec2<T> obs[kNumNNJoints];
};


template <typename T> struct DepthObservation
{
	T depth_value[kNumNNJoints];
};

template <typename T> struct ResidualTracker
{
	T *out_residual = nullptr;
	size_t out_residual_idx = {};

	ResidualTracker(T *residual) : out_residual(residual) {}

	void
	AddValue(T const &value)
	{
		this->out_residual[out_residual_idx++] = value;
	}
};


struct KinematicHandLM
{
	bool first_frame = true;
	bool use_stability = false;
	bool optimize_hand_size = true;
	bool is_right = false;
	float smoothing_factor;
	int num_observation_views = 0;
	one_frame_input *observation = nullptr;

	HandScalar target_hand_size = {};
	HandScalar hand_size_err_mul = {};
	HandScalar depth_err_mul = {};


	u_logging_level log_level = U_LOGGING_INFO;

	// Squashed final pose from last frame. We start from here.
	// At some point this might turn into a pose-prediction instead, we'll see :)
	Quat<HandScalar> this_frame_pre_rotation = {};
	Vec3<HandScalar> this_frame_pre_position = {};

	OptimizerHand<HandScalar> last_frame = {};

	// The pose that will take you from the right camera's space to the left camera's space.
	xrt_pose left_in_right = {};

	// The translation part of the same pose, just easier for Ceres to consume
	Vec3<HandScalar> left_in_right_translation = {};

	// The orientation part of the same pose, just easier for Ceres to consume
	Quat<HandScalar> left_in_right_orientation = {};

	Eigen::Matrix<HandScalar, calc_input_size(true), 1> TinyOptimizerInput = {};
};

template <typename T> struct Translations55
{
	Vec3<T> t[kNumFingers][kNumJointsInFinger] = {};
};

template <typename T> struct Orientations54
{
	Quat<T> q[kNumFingers][kNumJointsInFinger] = {};
};

template <bool optimize_hand_size> struct CostFunctor
{
	KinematicHandLM &parent;
	size_t num_residuals_;

	template <typename T>
	bool
	operator()(const T *const x, T *residual) const;

	CostFunctor(KinematicHandLM &in_last_hand, size_t const &num_residuals)
	    : parent(in_last_hand), num_residuals_(num_residuals)
	{}

	size_t
	NumResiduals() const
	{
		return num_residuals_;
	}
};


} // namespace xrt::tracking::hand::mercury::lm
