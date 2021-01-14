// Copyright 2020, Hesham Wahba.
// Copyright 2020, Nova King.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "utility_northstar.h"
#include "../ns_hmd.h"
#include <map>


class OpticalSystem
{
public:
	OpticalSystem(){};

	OpticalSystem(const OpticalSystem &_in);

	void
	LoadOpticalData(struct ns_v1_eye *eye);

	Vector3
	GetEyePosition()
	{
		return eyePosition;
	}

	Vector2
	RenderUVToDisplayUV(const Vector3 &inputUV);

	Vector2
	RenderUVToDisplayUV(const Vector2 &inputUV);

	Vector2
	SolveDisplayUVToRenderUV(const Vector2 &inputUV, Vector2 const &initialGuess, int iterations);

	Vector2
	DisplayUVToRenderUVPreviousSeed(Vector2 inputUV);

	void
	RegenerateMesh();

	void
	UpdateEyePosition(const Vector3 pos)
	{
		eyePosition.x = pos.x;
		eyePosition.y = pos.y;
		eyePosition.z = pos.z;
	}

	const Vector4
	GetCameraProjection()
	{
		return cameraProjection;
	}

	void
	setiters(int init, int opt)
	{
		m_iniSolverIters = init;
		m_optSolverIters = opt;
	}

	void
	UpdateClipToWorld(Matrix4x4 eyeRotationMatrix)
	{
		Matrix4x4 eyeToWorld = Matrix4x4::Translate(eyePosition) * eyeRotationMatrix;
		eyeToWorld.m02 *= -1;
		eyeToWorld.m12 *= -1;
		eyeToWorld.m22 *= -1;
		clipToWorld = eyeToWorld * cameraProjection.ComposeProjection().Inverse();
	}

	Vector3 eyePosition;

	inline void
	ViewportPointToRayDirection(Vector2 UV, Vector3 cameraPosition, Matrix4x4 clipToWorld, Vector3 &out)
	{
		Vector3 tmp;
		tmp.x = UV.x - 0.5f;
		tmp.y = UV.y - 0.5f;
		tmp.z = 0.f;
		Vector3 dir = clipToWorld.MultiplyPoint(tmp * 2.f) - cameraPosition;

		float mag = dir.Magnitude();
		out = dir / mag;
		return;
	}

private:
	float ellipseMinorAxis;
	float ellipseMajorAxis;
	Vector3 screenForward;
	Vector3 screenPosition;

	Vector4 cameraProjection;
	Matrix4x4 worldToSphereSpace;
	Matrix4x4 sphereToWorldSpace;
	Matrix4x4 worldToScreenSpace;
	Matrix4x4 clipToWorld;

	int m_iniSolverIters;
	int m_optSolverIters;

	std::map<float, std::map<float, Vector2> > m_requestedUVs;
};

// supporting functions
inline Vector3
Project(Vector3 v1, Vector3 v2)
{
	Vector3 v2Norm = (v2 / v2.Magnitude());
	return v2Norm * Vector3::Dot(v1, v2Norm);
}

inline float
intersectLineSphere(Vector3 Origin, Vector3 Direction, Vector3 spherePos, float SphereRadiusSqrd, bool frontSide = true)
{
	Vector3 L = spherePos - Origin;
	Vector3 offsetFromSphereCenterToRay = Project(L, Direction) - L;
	return (offsetFromSphereCenterToRay.sqrMagnitude() <= SphereRadiusSqrd)
	           ? Vector3::Dot(L, Direction) - (sqrt(SphereRadiusSqrd - offsetFromSphereCenterToRay.sqrMagnitude()) *
	                                           (frontSide ? 1.f : -1.f))
	           : -1.f;
}

inline float
intersectPlane(Vector3 n, Vector3 p0, Vector3 l0, Vector3 l)
{

	float denom = Vector3::Dot((Vector3::Zero() - n), l);
	if (denom > 1.4e-45f) {
		Vector3 p0l0 = p0 - l0;
		float t = Vector3::Dot(p0l0, (Vector3::Zero() - n)) / denom;
		return t;
	}
	return -1.f;
}
