// Copyright 2020, Hesham Wahba.
// Copyright 2020, Nova King.
// SPDX-License-Identifier: BSD-3-Clause

#include "deformation_northstar.h"


OpticalSystem::OpticalSystem(const OpticalSystem &_in)
{
	ellipseMinorAxis = _in.ellipseMinorAxis;
	ellipseMajorAxis = _in.ellipseMajorAxis;
	screenForward = _in.screenForward;
	screenPosition = _in.screenPosition;
	eyePosition = _in.eyePosition;
	worldToSphereSpace = _in.worldToSphereSpace;
	sphereToWorldSpace = _in.sphereToWorldSpace;
	worldToScreenSpace = _in.worldToScreenSpace;
	clipToWorld = _in.clipToWorld;
	cameraProjection = _in.cameraProjection;
}

void
OpticalSystem::LoadOpticalData(struct ns_v1_eye *eye)
{

	ellipseMinorAxis = eye->ellipse_minor_axis;
	ellipseMajorAxis = eye->ellipse_major_axis;

	screenForward.x = eye->screen_forward.x;
	screenForward.y = eye->screen_forward.y;
	screenForward.z = eye->screen_forward.z;

	screenPosition.x = eye->screen_position.x;
	screenPosition.y = eye->screen_position.y;
	screenPosition.z = eye->screen_position.z;

	eyePosition.x = eye->eye_pose.position.x;
	eyePosition.y = eye->eye_pose.position.y;
	eyePosition.z = eye->eye_pose.position.z;

	sphereToWorldSpace.m00 = eye->sphere_to_world_space.v[0];
	sphereToWorldSpace.m01 = eye->sphere_to_world_space.v[1];
	sphereToWorldSpace.m02 = eye->sphere_to_world_space.v[2];
	sphereToWorldSpace.m03 = eye->sphere_to_world_space.v[3];
	sphereToWorldSpace.m10 = eye->sphere_to_world_space.v[4];
	sphereToWorldSpace.m11 = eye->sphere_to_world_space.v[5];
	sphereToWorldSpace.m12 = eye->sphere_to_world_space.v[6];
	sphereToWorldSpace.m13 = eye->sphere_to_world_space.v[7];
	sphereToWorldSpace.m20 = eye->sphere_to_world_space.v[8];
	sphereToWorldSpace.m21 = eye->sphere_to_world_space.v[9];
	sphereToWorldSpace.m22 = eye->sphere_to_world_space.v[10];
	sphereToWorldSpace.m23 = eye->sphere_to_world_space.v[11];
	sphereToWorldSpace.m30 = 0.0f;
	sphereToWorldSpace.m31 = 0.0f;
	sphereToWorldSpace.m32 = 0.0f;
	sphereToWorldSpace.m33 = 1.0f;

	worldToScreenSpace.m00 = eye->world_to_screen_space.v[0];
	worldToScreenSpace.m01 = eye->world_to_screen_space.v[1];
	worldToScreenSpace.m02 = eye->world_to_screen_space.v[2];
	worldToScreenSpace.m03 = eye->world_to_screen_space.v[3];
	worldToScreenSpace.m10 = eye->world_to_screen_space.v[4];
	worldToScreenSpace.m11 = eye->world_to_screen_space.v[5];
	worldToScreenSpace.m12 = eye->world_to_screen_space.v[6];
	worldToScreenSpace.m13 = eye->world_to_screen_space.v[7];
	worldToScreenSpace.m20 = eye->world_to_screen_space.v[8];
	worldToScreenSpace.m21 = eye->world_to_screen_space.v[9];
	worldToScreenSpace.m22 = eye->world_to_screen_space.v[10];
	worldToScreenSpace.m23 = eye->world_to_screen_space.v[11];
	worldToScreenSpace.m30 = 0.0f;
	worldToScreenSpace.m31 = 0.0f;
	worldToScreenSpace.m32 = 0.0f;
	worldToScreenSpace.m33 = 1.0f;

	cameraProjection.w = eye->camera_projection.w;
	cameraProjection.x = eye->camera_projection.x;
	cameraProjection.y = eye->camera_projection.y;
	cameraProjection.z = eye->camera_projection.z;

	worldToSphereSpace = sphereToWorldSpace.Inverse();

	UpdateClipToWorld(Matrix4x4::Identity());
}

void
OpticalSystem::RegenerateMesh()
{

	// m_logger->DriverLog("Regenerate mesh called.");

	std::map<float, std::map<float, Vector2>>::iterator outerIter;
	std::map<float, Vector2>::iterator innerIter;

	// walk all the requested stored UVs and regeneate the response values
	// you probably should have updated the eye position before doing this
	// :-D
	for (outerIter = m_requestedUVs.begin(); outerIter != m_requestedUVs.end(); outerIter++) {
		for (innerIter = outerIter->second.begin(); innerIter != outerIter->second.end(); innerIter++) {
			Vector2 result = SolveDisplayUVToRenderUV(Vector2(outerIter->first, innerIter->first),
			                                          Vector2(innerIter->second.x, innerIter->second.y),
			                                          m_iniSolverIters);
			innerIter->second.x = result.x;
			innerIter->second.y = result.y;
		}
	}
}

Vector2
OpticalSystem::RenderUVToDisplayUV(const Vector2 &inputUV)
{
	Vector3 rayDir;
	ViewportPointToRayDirection(inputUV, eyePosition, clipToWorld, rayDir);
	Vector2 curDisplayUV = RenderUVToDisplayUV(rayDir);
	return curDisplayUV;
}

Vector2
OpticalSystem::RenderUVToDisplayUV(const Vector3 &inputUV)
{

	Vector3 sphereSpaceRayOrigin = worldToSphereSpace.MultiplyPoint(eyePosition);
	Vector3 sphereSpaceRayDirection =
	    (worldToSphereSpace.MultiplyPoint(eyePosition + inputUV) - sphereSpaceRayOrigin);
	sphereSpaceRayDirection = sphereSpaceRayDirection / sphereSpaceRayDirection.Magnitude();

	float intersectionTime =
	    intersectLineSphere(sphereSpaceRayOrigin, sphereSpaceRayDirection, Vector3::Zero(), 0.5f * 0.5f, false);

	if (intersectionTime < 0.f) {
		// m_logger->DriverLog("No line->ellipsoid intersection. %g %g",
		// inputUV.x, inputUV.y);
		return Vector2::zero();
	}
	Vector3 sphereSpaceIntersection = sphereSpaceRayOrigin + (sphereSpaceRayDirection * intersectionTime);

	// Ellipsoid  Normals
	Vector3 sphereSpaceNormal = (Vector3::Zero() - sphereSpaceIntersection) / sphereSpaceIntersection.Magnitude();
	sphereSpaceNormal.x = sphereSpaceNormal.x / powf(ellipseMinorAxis / 2.f, 2.f);
	sphereSpaceNormal.y = sphereSpaceNormal.y / powf(ellipseMinorAxis / 2.f, 2.f);
	sphereSpaceNormal.z = sphereSpaceNormal.z / powf(ellipseMajorAxis / 2.f, 2.f);
	sphereSpaceNormal = sphereSpaceNormal / sphereSpaceNormal.Magnitude();

	Vector3 worldSpaceIntersection = sphereToWorldSpace.MultiplyPoint(sphereSpaceIntersection);
	Vector3 worldSpaceNormal = sphereToWorldSpace.MultiplyVector(sphereSpaceNormal);
	worldSpaceNormal = worldSpaceNormal / worldSpaceNormal.Magnitude();

	Ray firstBounce(worldSpaceIntersection, Vector3::Reflect(inputUV, worldSpaceNormal));
	intersectionTime = intersectPlane(screenForward, screenPosition, firstBounce.m_Origin, firstBounce.m_Direction);

	if (intersectionTime < 0.f) {
		// m_logger->DriverLog("No bounce->screen intersection. %g %g",
		// inputUV.x, inputUV.y);
		return Vector2::zero();
	}
	Vector3 planeIntersection = firstBounce.GetPoint(intersectionTime);

	Vector3 ScreenUVZ = worldToScreenSpace.MultiplyPoint3x4(planeIntersection);

	Vector2 ScreenUV;
	ScreenUV.x = ScreenUVZ.x;
	ScreenUV.y = ScreenUVZ.y;

	Vector2 ScreenUV_Real;

	ScreenUV_Real.y = 1.f - (ScreenUV.x + 0.5f);
	ScreenUV_Real.x = 1.f - (ScreenUV.y + 0.5f);

	return ScreenUV_Real;
}

Vector2
OpticalSystem::SolveDisplayUVToRenderUV(const Vector2 &inputUV, Vector2 const &initialGuess, int iterations)
{

	static const float epsilon = 0.0001f;
	Vector2 curCameraUV;
	curCameraUV.x = initialGuess.x;
	curCameraUV.y = initialGuess.y;
	Vector2 curDisplayUV;

	for (int i = 0; i < iterations; i++) {
		Vector3 rayDir;

		// we can do all three calls below to RenderUVToDisplayUV at the
		// same time via SIMD or better yet we can vectorize across all
		// the uvs if we have a list of them
		curDisplayUV = RenderUVToDisplayUV(curCameraUV);
		Vector2 displayUVGradX =
		    (RenderUVToDisplayUV(curCameraUV + (Vector2(1, 0) * epsilon)) - curDisplayUV) / epsilon;
		Vector2 displayUVGradY =
		    (RenderUVToDisplayUV(curCameraUV + (Vector2(0, 1) * epsilon)) - curDisplayUV) / epsilon;

		Vector2 error = curDisplayUV - inputUV;
		Vector2 step = Vector2::zero();

		if ((!displayUVGradX.x) == 0.f || (!displayUVGradX.y) == 0.f) {
			step = step + (displayUVGradX * error.x);
		}
		if ((!displayUVGradY.x) == 0.f || (!displayUVGradY.y) == 0.f) {
			step = step + (displayUVGradY * error.y);
		}

		curCameraUV.x = curCameraUV.x - (step.x / 7.f);
		curCameraUV.y = curCameraUV.y - (step.y / 7.f);
	}

	return curCameraUV;
}


Vector2
OpticalSystem::DisplayUVToRenderUVPreviousSeed(Vector2 inputUV)
{
	// if we don't find a point we generate it and add it to our list
	Vector2 curDisplayUV;

	std::map<float, std::map<float, Vector2>>::iterator outerIter;
	outerIter = m_requestedUVs.find(inputUV.x);
	if (outerIter == m_requestedUVs.end()) {
		// if the outer value is not there we know the inner is not
		// so we just slam both in and call it a day
		std::map<float, Vector2> inner;
		curDisplayUV = SolveDisplayUVToRenderUV(inputUV, Vector2(0.5f, 0.5f), m_iniSolverIters);

		inner.insert(std::pair<float, Vector2>(inputUV.y, curDisplayUV));
		m_requestedUVs.insert(std::pair<float, std::map<float, Vector2>>(inputUV.x, inner));
		// Logger->DriverLog("NorthStar  Generated UV %g %g ",
		// inputUV.x, inputUV.y);

	} else {
		std::map<float, Vector2>::iterator innerIter;
		innerIter = outerIter->second.find(inputUV.y);

		if (innerIter == outerIter->second.end()) {
			// we assume there is no reason to ask for the same
			// value so no need to check if it exists already so
			// just add it.
			curDisplayUV = SolveDisplayUVToRenderUV(inputUV, Vector2(0.5f, 0.5f), m_iniSolverIters);

			outerIter->second.insert(std::pair<float, Vector2>(inputUV.y, curDisplayUV));
			// Logger->DriverLog("NorthStar  Generated UV %g %g ",
			// outerIter->first, innerIter->first);
		} else {
			// return the value we found
			// hopefully we have remashed at least once otherwise
			// we are giving back the same points again
			// curDisplayUV.x = innerIter->second.x;
			// curDisplayUV.y = innerIter->second.y;

			curDisplayUV = SolveDisplayUVToRenderUV(
			    inputUV, Vector2(innerIter->second.x, innerIter->second.y), m_optSolverIters);

			// Logger->DriverLog("NorthStar  Found UV %g %g ",
			// outerIter->first, innerIter->first);
		}
	}
	return curDisplayUV;
}


extern "C" struct ns_optical_system *
ns_create_optical_system(struct ns_v1_eye *eye)
{
	OpticalSystem *opticalSystem = new OpticalSystem();
	opticalSystem->LoadOpticalData(eye);
	opticalSystem->setiters(50, 50);
	opticalSystem->RegenerateMesh();
	return (struct ns_optical_system *)opticalSystem;
}

extern "C" void
ns_display_uv_to_render_uv(struct ns_uv in, struct ns_uv *out, struct ns_v1_eye *eye)
{
	OpticalSystem *opticalSystem = (OpticalSystem *)eye->optical_system;
	Vector2 inUV = Vector2(in.u, 1.f - in.v);
	Vector2 outUV = opticalSystem->DisplayUVToRenderUVPreviousSeed(inUV);
	out->u = outUV.x;
	out->v = outUV.y;
}
