// Copyright 2020, Hesham Wahba.
// Copyright 2020, Nova King.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <math.h>
using namespace std;

// min set of functions needed
const float LP_PI = 3.14159265f; // float error, will be 3.141592741f.
const float kEpsilon = 0.00001f;


// Min Port of a Vector3 class for the deformation rendering system.
class Vector3
{
public:
	inline Vector3(const Vector3 &in)
	{
		x = in.x;
		y = in.y;
		z = in.z;
	};

	inline Vector3()
	{
		x = 0.f;
		y = 0.f;
		z = 0.f;
	};

	inline Vector3(float _x, float _y, float _z)
	{
		x = _x;
		y = _y;
		z = _z;
	};

	inline Vector3(float _x, float _y)
	{
		x = _x;
		y = _y;
		z = 0.f;
	};

	constexpr Vector3 &
	operator=(const Vector3 &lhr)
	{
		this->x = lhr.x;
		this->y = lhr.y;
		this->z = lhr.z;
		return *this;
	}

	inline static Vector3
	Up()
	{
		return Vector3(0.f, 1.f, 0.f);
	};
	inline static Vector3
	Down()
	{
		return Vector3(0.f, -1.f, 0.f);
	};
	inline static Vector3
	Left()
	{
		return Vector3(-1.f, 0.f, 0.f);
	};
	inline static Vector3
	Right()
	{
		return Vector3(1.f, 0.f, 0.f);
	};
	inline static Vector3
	Forward()
	{
		return Vector3(0.f, 0.f, 1.f);
	};
	inline static Vector3
	Backward()
	{
		return Vector3(0.f, 0.f, -1.f);
	};

	inline Vector3
	operator-(Vector3 &rhs) const
	{
		Vector3 ret;
		ret.x = (x - rhs.x);
		ret.y = (y - rhs.y);
		ret.z = (z - rhs.z);
		return ret;
	}

	inline Vector3
	operator-() const
	{
		return Vector3(-x, -y, -z);
	}

	inline Vector3
	operator+(const Vector3 &rhs) const
	{
		Vector3 ret;
		ret.x = (x + rhs.x);
		ret.y = (y + rhs.y);
		ret.z = (z + rhs.z);
		return ret;
	}

	inline Vector3 &
	operator+=(const Vector3 &v)
	{
		this->x += v.x;
		this->y += v.y;
		this->z += v.z;
		return *this;
	}

	inline Vector3
	operator/(const float &d) const
	{
		Vector3 ret;
		ret.x = (x / d);
		ret.y = (y / d);
		ret.z = (z / d);
		return ret;
	}

	inline Vector3 // Until clang-format-11 is on the CI.
	operator*(const float &d) const
	{
		Vector3 ret;
		ret.x = (x * d);
		ret.y = (y * d);
		ret.z = (z * d);
		return ret;
	}

	inline Vector3
	Inverse() const
	{
		return {-x, -y, -z};
	}

	inline float static Dot(Vector3 const &lhs, Vector3 const &rhs)
	{
		float result = (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
		return result;
	}

	inline float
	Dot(Vector3 const &rhs) const
	{
		float result = (x * rhs.x) + (y * rhs.y) + (z * rhs.z);
		return result;
	}

	inline static float
	Angle(Vector3 v0, Vector3 v1)
	{
		Vector3 dir0 = v0.Normalized();
		Vector3 dir1 = v1.Normalized();

		float dot = dir0.Dot(dir1);
		dot = (dot < -1.f ? -1.f : (dot > 1.f ? 1.f : dot));

		float angle = acos(dot);

		return angle;
	}

	inline float
	sqrMagnitude() const
	{
		return x * x + y * y + z * z;
	}

	inline float
	Magnitude() const
	{
		return sqrt(x * x + y * y + z * z);
	}

	inline static Vector3
	Zero()
	{
		return {0.f, 0.f, 0.f};
	}

	inline static Vector3
	One()
	{
		return {1.f, 1.f, 1.f};
	}

	inline static Vector3
	Reflect(Vector3 inDirection, Vector3 inNormal)
	{
		return inNormal * -2.F * Dot(inNormal, inDirection) + inDirection;
	}

	inline void
	Normalize()
	{
		float mag = Magnitude();
		if (mag > kEpsilon) {
			x = x / mag;
			y = y / mag;
			z = z / mag;
		} else {
			x = 0.f;
			y = 0.f;
			z = 0.f;
		}
	}

	inline Vector3
	Normalized() const
	{
		Vector3 ret;
		float mag = Magnitude();
		if (mag > kEpsilon) {
			ret.x = x / mag;
			ret.y = y / mag;
			ret.z = z / mag;
		} else {
			ret.x = 0.f;
			ret.y = 0.f;
			ret.z = 0.f;
		}
		return ret;
	}


	inline void
	rotate(Vector3 axis, float radians)
	{
		float cos_theta = cosf(radians);
		float sin_theta = sinf(radians);

		x = (x * cos_theta) + (Vector3::Cross(axis) * sin_theta).x +
		    (axis.x * Vector3::Dot(axis, *this)) * (1 - cos_theta);

		y = (y * cos_theta) + (Vector3::Cross(axis) * sin_theta).y +
		    (axis.y * Vector3::Dot(axis, *this)) * (1 - cos_theta);

		z = (z * cos_theta) + (Vector3::Cross(axis) * sin_theta).z +
		    (axis.z * Vector3::Dot(axis, *this)) * (1 - cos_theta);
	}

	inline Vector3
	Cross(const Vector3 in) const
	{
		Vector3 ret;
		ret.x = y * in.z - z * in.y;
		ret.y = z * in.x - x * in.z;
		ret.z = x * in.y - y * in.x;
		return ret;
	}

	float x;
	float y;
	float z;
};

// Min Port of a Vector2 class for the deformation rendering system.
class Vector2
{
public:
	inline Vector2()
	{
		x = 0.f;
		y = 0.f;
	};

	inline Vector2(const Vector2 &in)
	{
		x = in.x;
		y = in.y;
	}

	inline Vector2(float _x, float _y)
	{
		x = _x;
		y = _y;
	};

	constexpr Vector2 &
	operator=(const Vector2 &lhr)
	{
		this->x = lhr.x;
		this->y = lhr.y;
		return *this;
	}

	inline static Vector2
	zero()
	{
		Vector2 ret;
		ret.x = 0.f;
		ret.y = 0.f;
		return ret;
	}

	inline static Vector2
	One()
	{
		Vector2 ret;
		ret.x = 1.f;
		ret.y = 1.f;
		return ret;
	}

	inline Vector2
	operator/(float d) const
	{
		Vector2 ret;
		ret.x = (x / d);
		ret.y = (y / d);
		return ret;
	}

	inline Vector2 // Until clang-format-11 is on the CI.
	operator*(float d) const
	{
		Vector2 ret;
		ret.x = (x * d);
		ret.y = (y * d);
		return ret;
	}
	inline Vector2
	operator-(const Vector2 &rhs) const
	{
		Vector2 ret;
		ret.x = (x - rhs.x);
		ret.y = (y - rhs.y);
		return ret;
	}

	inline Vector2
	operator+(const Vector2 &rhs) const
	{
		Vector2 ret;
		ret.x = (x + rhs.x);
		ret.y = (y + rhs.y);
		return ret;
	}

	float x;
	float y;
};

class Matrix4x4
{
public:
	inline Matrix4x4()
	{
		m00 = 0.0f;
		m01 = 0.0f;
		m02 = 0.0f;
		m03 = 0.0f;
		m10 = 0.0f;
		m11 = 0.0f;
		m12 = 0.0f;
		m13 = 0.0f;
		m20 = 0.0f;
		m21 = 0.0f;
		m22 = 0.0f;
		m23 = 0.0f;
		m30 = 0.0f;
		m31 = 0.0f;
		m32 = 0.0f;
		m33 = 0.0f;
	};

	inline Matrix4x4(const Matrix4x4 &_in)
	{
		m00 = _in.m00;
		m01 = _in.m01;
		m02 = _in.m02;
		m03 = _in.m03;
		m10 = _in.m10;
		m11 = _in.m11;
		m12 = _in.m12;
		m13 = _in.m13;
		m20 = _in.m20;
		m21 = _in.m21;
		m22 = _in.m22;
		m23 = _in.m23;
		m30 = _in.m30;
		m31 = _in.m31;
		m32 = _in.m32;
		m33 = _in.m33;
	};

	inline Matrix4x4(float in00,
	                 float in01,
	                 float in02,
	                 float in03,
	                 float in10,
	                 float in11,
	                 float in12,
	                 float in13,
	                 float in20,
	                 float in21,
	                 float in22,
	                 float in23,
	                 float in30,
	                 float in31,
	                 float in32,
	                 float in33)
	{
		m00 = in00;
		m01 = in01;
		m02 = in02;
		m03 = in03;
		m10 = in10;
		m11 = in11;
		m12 = in12;
		m13 = in13;
		m20 = in20;
		m21 = in21;
		m22 = in22;
		m23 = in23;
		m30 = in30;
		m31 = in31;
		m32 = in32;
		m33 = in33;
	};

	constexpr Matrix4x4 &
	operator=(const Matrix4x4 &lhr)
	{
		this->m00 = lhr.m00;
		this->m01 = lhr.m01;
		this->m02 = lhr.m02;
		this->m03 = lhr.m03;
		this->m10 = lhr.m10;
		this->m11 = lhr.m11;
		this->m12 = lhr.m12;
		this->m13 = lhr.m13;
		this->m20 = lhr.m20;
		this->m21 = lhr.m21;
		this->m22 = lhr.m22;
		this->m23 = lhr.m23;
		this->m30 = lhr.m30;
		this->m31 = lhr.m31;
		this->m32 = lhr.m32;
		this->m33 = lhr.m33;
		return *this;
	};

	inline static Matrix4x4
	Identity()
	{
		Matrix4x4 m;
		m.m00 = 1;
		m.m01 = 0;
		m.m02 = 0;
		m.m03 = 0;
		m.m10 = 0;
		m.m11 = 1;
		m.m12 = 0;
		m.m13 = 0;
		m.m20 = 0;
		m.m21 = 0;
		m.m22 = 1;
		m.m23 = 0;
		m.m30 = 0;
		m.m31 = 0;
		m.m32 = 0;
		m.m33 = 1;
		return m;
	}

	// Returns a 3x3 rotation matrix (padded to a Matrix4x4).
	inline static Matrix4x4
	RotationAlign(Vector3 const &fromDir, Vector3 const &toDir)
	{
		const Vector3 v = fromDir.Cross(toDir);
		const float c = fromDir.Dot(toDir);
		const float k = 1.0f / (1.0f + c);

		return Matrix4x4(v.x * v.x * k + c, v.y * v.x * k - v.z, v.z * v.x * k + v.y, 0.f, v.x * v.y * k + v.z,
		                 v.y * v.y * k + c, v.z * v.y * k - v.x, 0.f, v.x * v.z * k - v.y, v.y * v.z * k + v.x,
		                 v.z * v.z * k + c, 0.f, 0.f, 0.f, 0.f, 1.f);
	}

	inline Matrix4x4 // Until clang-format-11 is on the CI.
	operator*(const Matrix4x4 &_in)
	{
		Matrix4x4 ret;
		ret.m00 = (m00 * _in.m00) + (m01 * _in.m10) + (m02 * _in.m20) + (m03 * _in.m30);
		ret.m01 = (m00 * _in.m01) + (m01 * _in.m11) + (m02 * _in.m21) + (m03 * _in.m31);
		ret.m02 = (m00 * _in.m02) + (m01 * _in.m12) + (m02 * _in.m22) + (m03 * _in.m32);
		ret.m03 = (m00 * _in.m03) + (m01 * _in.m13) + (m02 * _in.m23) + (m03 * _in.m33);


		ret.m10 = (m10 * _in.m00) + (m11 * _in.m10) + (m12 * _in.m20) + (m13 * _in.m30);
		ret.m11 = (m10 * _in.m01) + (m11 * _in.m11) + (m12 * _in.m21) + (m13 * _in.m31);
		ret.m12 = (m10 * _in.m02) + (m11 * _in.m12) + (m12 * _in.m22) + (m13 * _in.m32);
		ret.m13 = (m10 * _in.m03) + (m11 * _in.m13) + (m12 * _in.m23) + (m13 * _in.m33);


		ret.m20 = (m20 * _in.m00) + (m21 * _in.m10) + (m22 * _in.m20) + (m23 * _in.m30);
		ret.m21 = (m20 * _in.m01) + (m21 * _in.m11) + (m22 * _in.m21) + (m23 * _in.m31);
		ret.m22 = (m20 * _in.m02) + (m21 * _in.m12) + (m22 * _in.m22) + (m23 * _in.m32);
		ret.m23 = (m20 * _in.m03) + (m21 * _in.m13) + (m22 * _in.m23) + (m23 * _in.m33);

		ret.m30 = (m30 * _in.m00) + (m31 * _in.m10) + (m32 * _in.m20) + (m33 * _in.m30);
		ret.m31 = (m30 * _in.m01) + (m31 * _in.m11) + (m32 * _in.m21) + (m33 * _in.m31);
		ret.m32 = (m30 * _in.m02) + (m31 * _in.m12) + (m32 * _in.m22) + (m33 * _in.m32);
		ret.m33 = (m30 * _in.m03) + (m31 * _in.m13) + (m32 * _in.m23) + (m33 * _in.m33);

		return ret;
	}

	inline Vector3
	MultiplyPoint(Vector3 const &point) const
	{
		Vector3 res;
		float w;
		res.x = m00 * point.x + m01 * point.y + m02 * point.z + m03;
		res.y = m10 * point.x + m11 * point.y + m12 * point.z + m13;
		res.z = m20 * point.x + m21 * point.y + m22 * point.z + m23;

		w = m30 * point.x + m31 * point.y + m32 * point.z + m33;

		w = 1.f / w;
		res.x *= w;
		res.y *= w;
		res.z *= w;
		return res;
	}

	inline static Matrix4x4
	Translate(Vector3 vector)
	{
		Matrix4x4 m;
		m.m00 = 1.f;
		m.m01 = 0.f;
		m.m02 = 0.f;
		m.m03 = vector.x;
		m.m10 = 0.f;
		m.m11 = 1.f;
		m.m12 = 0.f;
		m.m13 = vector.y;
		m.m20 = 0.f;
		m.m21 = 0.f;
		m.m22 = 1.f;
		m.m23 = vector.z;
		m.m30 = 0.f;
		m.m31 = 0.f;
		m.m32 = 0.f;
		m.m33 = 1.f;
		return m;
	}

	inline Vector3
	MultiplyVector(Vector3 vector) const
	{
		Vector3 res;
		res.x = m00 * vector.x + m01 * vector.y + m02 * vector.z;
		res.y = m10 * vector.x + m11 * vector.y + m12 * vector.z;
		res.z = m20 * vector.x + m21 * vector.y + m22 * vector.z;
		return res;
	}

	inline Vector3
	MultiplyPoint3x4(Vector3 point) const
	{
		Vector3 res;
		res.x = m00 * point.x + m01 * point.y + m02 * point.z + m03;
		res.y = m10 * point.x + m11 * point.y + m12 * point.z + m13;
		res.z = m20 * point.x + m21 * point.y + m22 * point.z + m23;
		return res;
	}

	inline Matrix4x4
	Transpose() const
	{
		Matrix4x4 r;
		r.m00 = m00;
		r.m01 = m10;
		r.m02 = m20;
		r.m03 = m30;
		r.m10 = m01;
		r.m11 = m11;
		r.m12 = m21;
		r.m13 = m31;
		r.m20 = m02;
		r.m21 = m12;
		r.m22 = m22;
		r.m23 = m32;
		r.m30 = m03;
		r.m31 = m13;
		r.m32 = m23;
		r.m33 = m33;
		return r;
	}

	inline Matrix4x4
	Inverse() const
	{
		float A2323 = m22 * m33 - m23 * m32;
		float A1323 = m21 * m33 - m23 * m31;
		float A1223 = m21 * m32 - m22 * m31;
		float A0323 = m20 * m33 - m23 * m30;
		float A0223 = m20 * m32 - m22 * m30;
		float A0123 = m20 * m31 - m21 * m30;
		float A2313 = m12 * m33 - m13 * m32;
		float A1313 = m11 * m33 - m13 * m31;
		float A1213 = m11 * m32 - m12 * m31;
		float A2312 = m12 * m23 - m13 * m22;
		float A1312 = m11 * m23 - m13 * m21;
		float A1212 = m11 * m22 - m12 * m21;
		float A0313 = m10 * m33 - m13 * m30;
		float A0213 = m10 * m32 - m12 * m30;
		float A0312 = m10 * m23 - m13 * m20;
		float A0212 = m10 * m22 - m12 * m20;
		float A0113 = m10 * m31 - m11 * m30;
		float A0112 = m10 * m21 - m11 * m20;

		float det =
		    m00 * (m11 * A2323 - m12 * A1323 + m13 * A1223) - m01 * (m10 * A2323 - m12 * A0323 + m13 * A0223) +
		    m02 * (m10 * A1323 - m11 * A0323 + m13 * A0123) - m03 * (m10 * A1223 - m11 * A0223 + m12 * A0123);
		det = 1 / det;

		return Matrix4x4(
		    det * (m11 * A2323 - m12 * A1323 + m13 * A1223), det * -(m01 * A2323 - m02 * A1323 + m03 * A1223),
		    det * (m01 * A2313 - m02 * A1313 + m03 * A1213), det * -(m01 * A2312 - m02 * A1312 + m03 * A1212),
		    det * -(m10 * A2323 - m12 * A0323 + m13 * A0223), det * (m00 * A2323 - m02 * A0323 + m03 * A0223),
		    det * -(m00 * A2313 - m02 * A0313 + m03 * A0213), det * (m00 * A2312 - m02 * A0312 + m03 * A0212),
		    det * (m10 * A1323 - m11 * A0323 + m13 * A0123), det * -(m00 * A1323 - m01 * A0323 + m03 * A0123),
		    det * (m00 * A1313 - m01 * A0313 + m03 * A0113), det * -(m00 * A1312 - m01 * A0312 + m03 * A0112),
		    det * -(m10 * A1223 - m11 * A0223 + m12 * A0123), det * (m00 * A1223 - m01 * A0223 + m02 * A0123),
		    det * -(m00 * A1213 - m01 * A0213 + m02 * A0113), det * (m00 * A1212 - m01 * A0212 + m02 * A0112));
	}

	float m00;
	float m01;
	float m02;
	float m03;

	float m10;
	float m11;
	float m12;
	float m13;

	float m20;
	float m21;
	float m22;
	float m23;

	float m30;
	float m31;
	float m32;
	float m33;
};

// Vector4 min port to get code working...
class Vector4
{
public:
	inline Vector4()
	{
		x = 0.f;
		y = 0.f;
		z = 0.f;
		w = 0.f;
	}

	inline Vector4(const Vector4 &in)
	{
		x = in.x;
		y = in.y;
		z = in.z;
		w = in.w;
	}

	constexpr Vector4 &
	operator=(const Vector4 &lhr)
	{
		if (this == &lhr) {
			// avoid self-assign
			return *this;
		}
		this->x = lhr.x;
		this->y = lhr.y;
		this->z = lhr.z;
		this->w = lhr.w;
		return *this;
	}

	inline Matrix4x4
	ComposeProjection() const
	{
		const float zNear = 0.07f;
		const float zFar = 1000.f;

		float fLeft = x;
		float fRight = y;
		float fTop = z;
		float fBottom = w;

		float idx = 1.0f / (fRight - fLeft);
		float idy = 1.0f / (fBottom - fTop);
		// float idz = 1.0f / (zFar - zNear);
		float sx = fRight + fLeft;
		float sy = fBottom + fTop;

		float c = -(zFar + zNear) / (zFar - zNear);
		float d = -(2.0F * zFar * zNear) / (zFar - zNear);

		Matrix4x4 m;
		m.m00 = 2.f * idx;
		m.m01 = 0.f;
		m.m02 = sx * idx;
		m.m03 = 0.f;
		m.m10 = 0.f;
		m.m11 = 2.f * idy;
		m.m12 = sy * idy;
		m.m13 = 0.f;
		m.m20 = 0.f;
		m.m21 = 0.f;
		m.m22 = c;
		m.m23 = d;
		m.m30 = 0.f;
		m.m31 = 0.f;
		m.m32 = -1.0f;
		m.m33 = 0.f;

		return m;
	}

	float x;
	float y;
	float z;
	float w;
};

class Ray
{
public:
	inline Ray(Vector3 origin, Vector3 direction)
	{
		m_Origin = origin;
		direction.Normalize();
		m_Direction.x = direction.x;
		m_Direction.y = direction.y;
		m_Direction.z = direction.z;
	}

	inline Vector3
	GetPoint(float distance)
	{
		return m_Origin + m_Direction * distance;
	}

	Vector3 m_Origin;
	Vector3 m_Direction;
};

static inline float
SIGN(float x)
{
	return (x >= 0.0f) ? +1.0f : -1.0f;
}

static inline float
NORM(float a, float b, float c, float d)
{
	return sqrt(a * a + b * b + c * c + d * d);
}

class Quaternion
{
public:
	inline Quaternion()
	{
		x = 0.f;
		y = 0.f;
		z = 0.f;
		w = 1.f;
	};

	inline Quaternion(float _x, float _y, float _z, float _w)
	{
		x = _x;
		y = _y;
		z = _z;
		w = _w;
	};

	inline Quaternion(const Quaternion &_in) = default;

	Quaternion &
	operator=(const Quaternion &lhr) = default;

	inline static Quaternion
	Identity()
	{
		return Quaternion(0, 0, 0, 1);
	}

	inline Quaternion
	conjugate() const
	{
		return Quaternion(-x, -y, -z, w);
	}

	inline float
	norm() const
	{
		return sqrt((x * x) + (y * y) + (z * z) + (w * w));
	}

	inline Quaternion
	scale(float s) const
	{
		return Quaternion(w * s, x * s, y * s, z * s);
	}

	inline Quaternion
	Inverse() const
	{
		return conjugate() / norm();
	}

	inline Vector3
	Right() const
	{
		return *this * Vector3::Right();
	}

	inline Vector3
	Up() const
	{
		return *this * Vector3::Up();
	}

	inline Vector3
	Forward() const
	{
		return *this * Vector3::Forward();
	}

	inline static void
	ToEulerAngle(const Quaternion &q, float &roll, float &pitch, float &yaw)
	{
		float sinr = +2.0f * (q.w * q.x + q.y * q.z);
		float cosr = +1.0f - 2.0f * (q.x * q.x + q.y * q.y);
		roll = atan2f(sinr, cosr);

		float sinp = +2.0f * (q.w * q.y - q.z * q.x);
		if (fabs(sinp) >= 1.f) {
			pitch = copysignf(LP_PI / 2.f, sinp);
		} else {
			pitch = asinf(sinp);
		}

		float siny = +2.0f * (q.w * q.z + q.x * q.y);
		float cosy = +1.0f - 2.0f * (q.y * q.y + q.z * q.z);
		yaw = atan2f(siny, cosy);
	}

	inline static Vector3
	ToEulerAngles(const Quaternion &in)
	{
		Vector3 euler;
		const static float PI_OVER_2 = LP_PI * 0.5f;
		const static float EPSILON = 1e-10f;
		float sqw, sqx, sqy, sqz;

		// quick conversion to Euler angles to give tilt to user
		sqw = in.w * in.w;
		sqx = in.x * in.x;
		sqy = in.y * in.y;
		sqz = in.z * in.z;

		euler.y = asinf(2.0f * (in.w * in.y - in.x * in.z));
		if (PI_OVER_2 - fabs(euler.y) > EPSILON) {
			euler.z = atan2f(2.0f * (in.x * in.y + in.w * in.z), sqx - sqy - sqz + sqw);
			euler.x = atan2f(2.0f * (in.w * in.x + in.y * in.z), sqw - sqx - sqy + sqz);
		} else {
			// compute heading from local 'down' vector
			euler.z = atan2f(2.f * in.y * in.z - 2.f * in.x * in.w, 2.f * in.x * in.z + 2.f * in.y * in.w);
			euler.x = 0.0f;

			// If facing down, reverse yaw
			if (euler.y < 0.f) {
				euler.z = LP_PI - euler.z;
			}
		}
		return euler;
	}

	inline Vector3 // Until clang-format-11 is on the CI.
	operator*(Vector3 const &vec) const
	{
		float num = x * 2.f;
		float num2 = y * 2.f;
		float num3 = z * 2.f;
		float num4 = x * num;
		float num5 = y * num2;
		float num6 = z * num3;
		float num7 = x * num2;
		float num8 = x * num3;
		float num9 = y * num3;
		float num10 = w * num;
		float num11 = w * num2;
		float num12 = w * num3;
		Vector3 result;
		result.x = (1.f - (num5 + num6)) * vec.x + (num7 - num12) * vec.y + (num8 + num11) * vec.z;
		result.y = (num7 + num12) * vec.x + (1.f - (num4 + num6)) * vec.y + (num9 - num10) * vec.z;
		result.z = (num8 - num11) * vec.x + (num9 + num10) * vec.y + (1.f - (num4 + num5)) * vec.z;
		return result;
	}

	inline static Quaternion
	Euler(const Vector3 &euler)
	{
		float c1 = cos(euler.z * 0.5f);
		float c2 = cos(euler.y * 0.5f);
		float c3 = cos(euler.x * 0.5f);
		float s1 = sin(euler.z * 0.5f);
		float s2 = sin(euler.y * 0.5f);
		float s3 = sin(euler.x * 0.5f);

		Quaternion ret;
		ret.x = c1 * c2 * s3 - s1 * s2 * c3;
		ret.y = c1 * s2 * c3 + s1 * c2 * s3;
		ret.z = s1 * c2 * c3 - c1 * s2 * s3;
		ret.w = c1 * c2 * c3 + s1 * s2 * s3;

		return ret;
	}


	Matrix4x4
	ToMatrix4x4() const
	{
		float qw = w;
		float qx = x;
		float qy = y;
		float qz = z;

		const float n = 1.0f / sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
		qx *= n;
		qy *= n;
		qz *= n;
		qw *= n;

		return Matrix4x4(1.0f - 2.0f * qy * qy - 2.0f * qz * qz, 2.0f * qx * qy - 2.0f * qz * qw,
		                 2.0f * qx * qz + 2.0f * qy * qw, 0.0f, 2.0f * qx * qy + 2.0f * qz * qw,
		                 1.0f - 2.0f * qx * qx - 2.0f * qz * qz, 2.0f * qy * qz - 2.0f * qx * qw, 0.0f,
		                 2.0f * qx * qz - 2.0f * qy * qw, 2.0f * qy * qz + 2.0f * qx * qw,
		                 1.0f - 2.0f * qx * qx - 2.0f * qy * qy, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	}

	inline Quaternion // Until clang-format-11 is on the CI.
	operator*(const Quaternion &q) const
	{
		return Quaternion(y * q.z - z * q.y + x * q.w + w * q.x, z * q.x - x * q.z + y * q.w + w * q.y,
		                  x * q.y - y * q.x + z * q.w + w * q.z, w * q.w - x * q.x - y * q.y - z * q.z);
	}

	Quaternion
	operator/(const float div) const
	{
		return Quaternion(x / div, y / div, z / div, w / div);
	}

	inline static Quaternion
	AxisAngle(Vector3 axis, float angle)
	{
		float halfAngle = angle * .5f;
		float s = (float)sin(halfAngle);
		Quaternion q;
		q.x = axis.x * s;
		q.y = axis.y * s;
		q.z = axis.z * s;
		q.w = (float)cos(halfAngle);
		return q;
	}

	inline static Quaternion
	LookAt(Vector3 sourcePoint, Vector3 destPoint)
	{
		Vector3 forwardVector = (destPoint - sourcePoint).Normalized();

		float dot = Vector3::Dot(Vector3::Forward(), forwardVector);

		if (fabs(dot - (-1.0f)) < 0.000001f) {
			return Quaternion(Vector3::Up().x, Vector3::Up().y, Vector3::Up().z, 3.1415926535897932f);
		}
		if (fabs(dot - (1.0f)) < 0.000001f) {
			return Quaternion();
		}

		float rotAngle = cos(dot);
		Vector3 rotAxis = Vector3::Forward().Cross(forwardVector);
		rotAxis = rotAxis.Normalized();
		return AxisAngle(rotAxis, rotAngle);
	}

	inline static Quaternion
	QuaternionLookRotation(Vector3 forward, Vector3 Up)
	{

		Vector3 vector1 = forward.Normalized();
		Vector3 vector2 = (Up.Cross(vector1)).Normalized();
		Vector3 vector3 = vector1.Cross(vector2);

		float m00 = vector2.x;
		float m01 = vector2.y;
		float m02 = vector2.z;
		float m10 = vector3.x;
		float m11 = vector3.y;
		float m12 = vector3.z;
		float m20 = vector1.x;
		float m21 = vector1.y;
		float m22 = vector1.z;

		float num8 = (m00 + m11) + m22;
		Quaternion quaternion;
		if (num8 > 0.f) {
			float num = (float)sqrtf(num8 + 1.f);
			quaternion.w = num * 0.5f;
			num = 0.5f / num;
			quaternion.x = (m12 - m21) * num;
			quaternion.y = (m20 - m02) * num;
			quaternion.z = (m01 - m10) * num;
			return quaternion;
		}

		if ((m00 >= m11) && (m00 >= m22)) {
			float num7 = (float)sqrtf(((1.f + m00) - m11) - m22);
			float num4 = 0.5f / num7;
			quaternion.x = 0.5f * num7;
			quaternion.y = (m01 + m10) * num4;
			quaternion.z = (m02 + m20) * num4;
			quaternion.w = (m12 - m21) * num4;
			return quaternion;
		}

		if (m11 > m22) {
			float num6 = (float)sqrtf(((1.f + m11) - m00) - m22);
			float num3 = 0.5f / num6;
			quaternion.x = (m10 + m01) * num3;
			quaternion.y = 0.5f * num6;
			quaternion.z = (m21 + m12) * num3;
			quaternion.w = (m20 - m02) * num3;
			return quaternion;
		}

		float num5 = (float)sqrtf(((1.f + m22) - m00) - m11);
		float num2 = 0.5f / num5;
		quaternion.x = (m20 + m02) * num2;
		quaternion.y = (m21 + m12) * num2;
		quaternion.z = 0.5f * num5;
		quaternion.w = (m01 - m10) * num2;
		return quaternion;
	}

	inline static Quaternion
	FromMatrix(const Matrix4x4 m)
	{
		float tr = m.m00 + m.m11 + m.m22;
		float qx, qy, qz, qw;

		if (tr > 0) {
			float S = sqrtf(tr + 1.f) * 2.f;
			qw = 0.25f * S;
			qx = (m.m21 - m.m12) / S;
			qy = (m.m02 - m.m20) / S;
			qz = (m.m10 - m.m01) / S;
		} else if ((m.m00 > m.m11) && (m.m00 > m.m22)) {
			float S = sqrtf(1.f + m.m00 - m.m11 - m.m22) * 2.f;
			qw = (m.m21 - m.m12) / S;
			qx = 0.25f * S;
			qy = (m.m01 + m.m10) / S;
			qz = (m.m02 + m.m20) / S;
		} else if (m.m11 > m.m22) {
			float S = sqrtf(1.f + m.m11 - m.m00 - m.m22) * 2.f;
			qw = (m.m02 - m.m20) / S;
			qx = (m.m01 + m.m10) / S;
			qy = 0.25f * S;
			qz = (m.m12 + m.m21) / S;
		} else {
			float S = sqrtf(1.f + m.m22 - m.m00 - m.m11) * 2.f;
			qw = (m.m10 - m.m01) / S;
			qx = (m.m02 + m.m20) / S;
			qy = (m.m12 + m.m21) / S;
			qz = 0.25f * S;
		}

		return Quaternion(qx, qy, qz, qw);
	}

	//// quaternion = [w, x, y, z]'
	// void mRot2Quat(const Matrix4x4 m) {
	//	float r11 = m.m00;
	//	float r12 = m.m01;
	//	float r13 = m.m02;
	//	float r21 = m.m10;
	//	float r22 = m.m11;
	//	float r23 = m.m12;
	//	float r31 = m.m20;
	//	float r32 = m.m21;
	//	float r33 = m.m22;
	//	float w = (r11 + r22 + r33 + 1.0f) / 4.0f;
	//	float x = (r11 - r22 - r33 + 1.0f) / 4.0f;
	//	float y = (-r11 + r22 - r33 + 1.0f) / 4.0f;
	//	float z = (-r11 - r22 + r33 + 1.0f) / 4.0f;
	//	if (w < 0.0f) {
	//		w = 0.0f;
	//	}
	//	if (x < 0.0f) {
	//		x = 0.0f;
	//	}
	//	if (y < 0.0f) {
	//		y = 0.0f;
	//	}
	//	if (z < 0.0f) {
	//		z = 0.0f;
	//	}
	//	w = sqrt(w);
	//	x = sqrt(x);
	//	y = sqrt(y);
	//	z = sqrt(z);
	//	if (w >= x && w >= y && w >= z) {
	//		w *= +1.0f;
	//		x *= SIGN(r32 - r23);
	//		y *= SIGN(r13 - r31);
	//		z *= SIGN(r21 - r12);
	//	}
	//	else if (x >= w && x >= y && x >= z) {
	//		w *= SIGN(r32 - r23);
	//		x *= +1.0f;
	//		y *= SIGN(r21 + r12);
	//		z *= SIGN(r13 + r31);
	//	}
	//	else if (y >= w && y >= x && y >= z) {
	//		w *= SIGN(r13 - r31);
	//		x *= SIGN(r21 + r12);
	//		y *= +1.0f;
	//		z *= SIGN(r32 + r23);
	//	}
	//	else if (z >= w && z >= x && z >= y) {
	//		w *= SIGN(r21 - r12);
	//		x *= SIGN(r31 + r13);
	//		y *= SIGN(r32 + r23);
	//		z *= +1.0f;
	//	}
	//	else {
	//		//U_LOG_E("coding error");
	//	}
	//	float r = NORM(w, x, w, z);
	//	w /= r;
	//	x /= r;
	//	y /= r;
	//	z /= r;
	//}

	inline static Quaternion
	FromToRotation(Vector3 dir0, Vector3 dir1)
	{
		Vector3 axis = dir0.Cross(dir1).Normalized();
		float angle = Vector3::Angle(dir0, dir1);
		return Quaternion::AxisAngle(axis, angle);
	}

	float x;
	float y;
	float z;
	float w;
};

// A position and rotation. You can multiply two poses; this acts like
// Matrix4x4 multiplication, but Poses always have unit scale.
class Pose
{
public:
	Vector3 position;
	Quaternion rotation;

	explicit Pose(Vector3 const &pos)
	{
		position = pos;
		rotation = Quaternion::Identity();
	}
	explicit Pose(Quaternion const &rot)
	{
		position = Vector3::Zero();
		rotation = rot;
	}
	Pose(Vector3 const &pos, Quaternion const &rot)
	{
		position = pos;
		rotation = rot;
	}

	inline static Pose
	Identity()
	{
		return Pose(Vector3::Zero(), Quaternion::Identity());
	}

	inline Pose
	Inverse() const
	{
		Quaternion invQ = rotation.Inverse();
		return Pose(invQ * -position, invQ);
	}

	inline Matrix4x4
	Matrix() const
	{
		return Matrix4x4::Translate(position) * rotation.ToMatrix4x4();
	}

	inline Pose // Until clang-format-11 is on the CI.
	operator*(Pose const &rhs) const
	{
		return Pose(position + (rotation * rhs.position), rotation * rhs.rotation);
	}

	inline Pose // Until clang-format-11 is on the CI.
	operator*(Vector3 const &rhs) const
	{
		return Pose(position + rotation * rhs, rotation);
	}

	inline static Pose
	FromMatrix(Matrix4x4 m)
	{
		return Pose(Vector3(m.m03, m.m13, m.m23), Quaternion::FromMatrix(m));
	}
};
