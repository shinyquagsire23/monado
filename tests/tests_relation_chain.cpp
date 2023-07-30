// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Test xrt_relation_chain functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_compiler.h"

#include "math/m_api.h"
#include "math/m_space.h"

#include "catch/catch.hpp"


/*
 *
 * Constants
 *
 */

constexpr xrt_pose kPoseIdentity = XRT_POSE_IDENTITY;

constexpr xrt_pose kPoseOneY = {
    XRT_QUAT_IDENTITY,
    {0.0f, 1.0f, 0.0f},
};

constexpr xrt_space_relation_flags kFlagsNotValid = {};

constexpr xrt_space_relation_flags kFlagsValid = (xrt_space_relation_flags)( //
    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |                               //
    XRT_SPACE_RELATION_POSITION_VALID_BIT);                                  //

constexpr xrt_space_relation_flags kFlagsValidTracked = (xrt_space_relation_flags)( //
    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |                                      //
    XRT_SPACE_RELATION_POSITION_VALID_BIT |                                         //
    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |                                    //
    XRT_SPACE_RELATION_POSITION_TRACKED_BIT);                                       //


constexpr xrt_space_relation kSpaceRelationNotValid = {
    kFlagsNotValid,
    kPoseOneY,
    XRT_VEC3_ZERO,
    XRT_VEC3_ZERO,
};

constexpr xrt_space_relation kSpaceRelationOneY = {
    kFlagsValid,
    kPoseOneY,
    XRT_VEC3_ZERO,
    XRT_VEC3_ZERO,
};

constexpr xrt_space_relation kSpaceRelationOneYTracked = {
    kFlagsValidTracked,
    kPoseOneY,
    XRT_VEC3_ZERO,
    XRT_VEC3_ZERO,
};

constexpr xrt_space_relation kSpaceRelationOnlyOrientation = {
    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT,
    {XRT_QUAT_IDENTITY, XRT_VEC3_ZERO},
    XRT_VEC3_ZERO,
    XRT_VEC3_ZERO,
};

constexpr xrt_space_relation kSpaceRelationOnlyPosition = {
    XRT_SPACE_RELATION_POSITION_VALID_BIT,
    {
        {0.0f, 0.0f, 0.0f, 0.0f}, // Invalid
        {0.0f, 1.0f, 0.0f},
    },
    XRT_VEC3_ZERO,
    XRT_VEC3_ZERO,
};


/*
 *
 * For flags testing
 *
 */

enum Functions
{
	NV,               // (Non-Identity) (Space Relation) Not Valid Not Tracked
	VT,               // (Non-Identity) (Space Relation) Valid Tracked
	VNT,              // (Non-Identity) (Space Relation) Valid Not Tracked
	P,                // (Non-Identity) Pose
	IP,               // Identity Pose
	ONLY_ORIENTATION, // (Non-Identity) (Space Relation) Only orientation
	ONLY_POSITION,    // (Non-Identity) (Space Relation) Only position
};

static void
stringify(std::string &str, Functions fn)
{
	if (str.size() > 0) {
		str += ", ";
	}

	switch (fn) {
	case NV: str += "NV"; break;
	case VT: str += "VT"; break;
	case VNT: str += "VNT"; break;
	case P: str += "P"; break;
	case IP: str += "IP"; break;
	case ONLY_ORIENTATION: str += "ONLY_ORIENTATION"; break;
	case ONLY_POSITION: str += "ONLY_POSITION"; break;
	default: assert(false);
	}
}

static void
run_func(struct xrt_relation_chain *xrc, Functions fn)
{
	switch (fn) {
	case NV: m_relation_chain_push_relation(xrc, &kSpaceRelationNotValid); return;
	case VT: m_relation_chain_push_relation(xrc, &kSpaceRelationOneYTracked); return;
	case VNT: m_relation_chain_push_relation(xrc, &kSpaceRelationOneY); return;
	case P: m_relation_chain_push_pose_if_not_identity(xrc, &kPoseOneY); return;
	case IP: m_relation_chain_push_pose_if_not_identity(xrc, &kPoseIdentity); return;
	case ONLY_ORIENTATION: m_relation_chain_push_relation(xrc, &kSpaceRelationOnlyOrientation); return;
	case ONLY_POSITION: m_relation_chain_push_relation(xrc, &kSpaceRelationOnlyPosition); return;
	default: assert(false);
	}
}

#define TEST_FLAGS(EXPECTED, ...)                                                                                      \
	do {                                                                                                           \
		struct xrt_space_relation result = XRT_STRUCT_INIT;                                                    \
		struct xrt_relation_chain xrc = XRT_STRUCT_INIT;                                                       \
		Functions fns[] = {__VA_ARGS__};                                                                       \
		std::string str = {};                                                                                  \
		for (Functions fn : fns) {                                                                             \
			run_func(&xrc, fn);                                                                            \
			stringify(str, fn);                                                                            \
		}                                                                                                      \
		CAPTURE(str);                                                                                          \
		m_relation_chain_resolve(&xrc, &result);                                                               \
		CHECK(result.relation_flags == EXPECTED);                                                              \
	} while (false)


/*
 *
 * Tests
 *
 */

TEST_CASE("Relation Chain Flags")
{
	SECTION("Not Valid")
	{
		TEST_FLAGS(kFlagsNotValid, VT, NV, VT);
		TEST_FLAGS(kFlagsNotValid, VT, VT, VT, NV);
		TEST_FLAGS(kFlagsNotValid, P, NV, VNT);

		TEST_FLAGS(kFlagsNotValid, NV, ONLY_ORIENTATION);
		TEST_FLAGS(kFlagsNotValid, NV, ONLY_POSITION);
		TEST_FLAGS(kFlagsNotValid, ONLY_ORIENTATION, NV);
		TEST_FLAGS(kFlagsNotValid, ONLY_POSITION, NV);
	}

	/*!
	 * @todo: These should all not return tracked.
	 */
	SECTION("Wrongly Tracked")
	{
		TEST_FLAGS(kFlagsValidTracked, VNT, IP, VT);
		TEST_FLAGS(kFlagsValidTracked, VNT, P, VT);
		TEST_FLAGS(kFlagsValidTracked, P, VT, P, VNT);
		TEST_FLAGS(kFlagsValidTracked, VT, VT, VNT, VT);
		TEST_FLAGS(kFlagsValidTracked, IP, VT, P, VNT, P, VT);

		TEST_FLAGS(kFlagsValidTracked, VT, ONLY_ORIENTATION);
		TEST_FLAGS(kFlagsValidTracked, VT, ONLY_POSITION);
		TEST_FLAGS(kFlagsValidTracked, ONLY_ORIENTATION, VT);
		TEST_FLAGS(kFlagsValidTracked, ONLY_POSITION, VT);

		TEST_FLAGS(kFlagsValidTracked, P, VT, ONLY_ORIENTATION, P);
		TEST_FLAGS(kFlagsValidTracked, P, VT, ONLY_POSITION, P);
		TEST_FLAGS(kFlagsValidTracked, P, ONLY_ORIENTATION, VT, P);
		TEST_FLAGS(kFlagsValidTracked, P, ONLY_POSITION, VT, P);
	}

	SECTION("Tracked")
	{
		TEST_FLAGS(kFlagsValidTracked, P, VT, P);
		TEST_FLAGS(kFlagsValidTracked, P, VT, P, VT);
		TEST_FLAGS(kFlagsValidTracked, VT, IP, P);
		TEST_FLAGS(kFlagsValidTracked, IP, VT, P);
		TEST_FLAGS(kFlagsValidTracked, P, VT, IP, P);
		TEST_FLAGS(kFlagsValidTracked, P, IP, VT, P);
		TEST_FLAGS(kFlagsValidTracked, IP, IP, VT, IP, IP);
	}

	SECTION("Non-Tracked")
	{
		TEST_FLAGS(kFlagsValid, P, VNT, P);
		TEST_FLAGS(kFlagsValid, VNT, VNT, VNT);
		TEST_FLAGS(kFlagsValid, VNT, P);
		TEST_FLAGS(kFlagsValid, P, VNT);
		TEST_FLAGS(kFlagsValid, VNT, IP);
		TEST_FLAGS(kFlagsValid, IP, VNT);
		TEST_FLAGS(kFlagsValid, VNT, IP, P);
		TEST_FLAGS(kFlagsValid, IP, VNT, P);
		TEST_FLAGS(kFlagsValid, P, VNT, IP, P);
		TEST_FLAGS(kFlagsValid, P, IP, VNT, P);

		TEST_FLAGS(kFlagsValid, P, ONLY_ORIENTATION, IP, P);
		TEST_FLAGS(kFlagsValid, P, ONLY_POSITION, IP, P);

		TEST_FLAGS(kFlagsValid, ONLY_ORIENTATION, VNT);
		TEST_FLAGS(kFlagsValid, ONLY_POSITION, VNT);
		TEST_FLAGS(kFlagsValid, VNT, ONLY_ORIENTATION);
		TEST_FLAGS(kFlagsValid, VNT, ONLY_POSITION);

		TEST_FLAGS(kFlagsValid, ONLY_ORIENTATION, P, VNT);
		TEST_FLAGS(kFlagsValid, ONLY_POSITION, P, VNT);
		TEST_FLAGS(kFlagsValid, VNT, ONLY_ORIENTATION, P);
		TEST_FLAGS(kFlagsValid, VNT, ONLY_POSITION, P);
	}
}
