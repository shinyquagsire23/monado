// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Defines for Levenberg-Marquardt kinematic optimizer
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */
#pragma once

// #include <Eigen/Core>
// #include <Eigen/Geometry>
#include "math/m_mathinclude.h"
#include "../kine_common.hpp"
#include <type_traits>

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

static constexpr size_t kMetacarpalBoneDim = 3;
static constexpr size_t kProximalBoneDim = 2;
static constexpr size_t kFingerDim = kProximalBoneDim + kMetacarpalBoneDim + 2;
static constexpr size_t kThumbDim = kMetacarpalBoneDim + 2;
static constexpr size_t kHandSizeDim = 1;
static constexpr size_t kHandTranslationDim = 3;
static constexpr size_t kHandOrientationDim = 3;



static constexpr size_t kHRTC_HandSize = 1;
static constexpr size_t kHRTC_RootBoneTranslation = 3;
static constexpr size_t kHRTC_RootBoneOrientation = 3; // Direct difference between the two angle-axis rotations. This
                                                       // works well enough because the rotation should be small.

static constexpr size_t kHRTC_ThumbMCPSwingTwist = 3;
static constexpr size_t kHRTC_ThumbCurls = 2;

static constexpr size_t kHRTC_ProximalSimilarity = 2;

static constexpr size_t kHRTC_FingerMCPSwingTwist = 3;
static constexpr size_t kHRTC_FingerPXMSwing = 2;
static constexpr size_t kHRTC_FingerCurls = 2;
static constexpr size_t kHRTC_CurlSimilarity = 1;

static constexpr size_t kHandResidualOneSideSize = 21 * 2;

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


// Factors to multiply different values by to get a smooth hand trajectory without introducing too much latency

// 1.0 is good, a little jittery.
// Anything above 3.0 generally breaks.
static constexpr HandScalar kStabilityRoot = 1.0;
static constexpr HandScalar kStabilityCurlRoot = kStabilityRoot * 0.03f;
static constexpr HandScalar kStabilityOtherRoot = kStabilityRoot * 0.03f;

static constexpr HandScalar kStabilityThumbMCPSwing = kStabilityCurlRoot * 1.5f;
static constexpr HandScalar kStabilityThumbMCPTwist = kStabilityCurlRoot * 1.5f;

static constexpr HandScalar kStabilityFingerMCPSwing = kStabilityCurlRoot * 3.0f;
static constexpr HandScalar kStabilityFingerMCPTwist = kStabilityCurlRoot * 3.0f;

static constexpr HandScalar kStabilityFingerPXMSwingX = kStabilityCurlRoot * 1.0f;
static constexpr HandScalar kStabilityFingerPXMSwingY = kStabilityCurlRoot * 1.6f;

static constexpr HandScalar kStabilityRootPosition = kStabilityOtherRoot * 30;
static constexpr HandScalar kStabilityHandSize = kStabilityOtherRoot * 1000;

static constexpr HandScalar kStabilityHandOrientation = kStabilityOtherRoot * 3;


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
	Scalar x;
	Scalar y;
	Scalar z;
	Scalar w;

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
	Scalar x;
	Scalar y;
	Scalar z;

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
};

template <typename Scalar> struct Vec2
{
	Scalar x;
	Scalar y;

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
	T *out_residual;
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



} // namespace xrt::tracking::hand::mercury::lm
