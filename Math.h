#pragma once
#include <cmath>

struct Vec3
{
	float x, y, z;

	Vec3() : x(0), y(0), z(0) {}
	Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

	Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
	Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
	Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }

	float Length() const { return sqrtf(x * x + y * y + z * z); }
	Vec3 Normalize() const { float len = Length(); return len > 0 ? Vec3(x / len, y / len, z / len) : Vec3(); }

	static Vec3 Cross(const Vec3& a, const Vec3& b)
	{
		return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
	}

	static float Dot(const Vec3& a, const Vec3& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}
};

struct Matrix4x4
{
	float m[16];

	Matrix4x4()
	{
		for (int i = 0; i < 16; i++) m[i] = 0;
	}

	static Matrix4x4 Identity()
	{
		Matrix4x4 mat;
		mat.m[0] = mat.m[5] = mat.m[10] = mat.m[15] = 1.0f;
		return mat;
	}

	static Matrix4x4 LookAtLH(const Vec3& eye, const Vec3& target, const Vec3& up)
	{
		Vec3 zaxis = (target - eye).Normalize();
		Vec3 xaxis = Vec3::Cross(up, zaxis).Normalize();
		Vec3 yaxis = Vec3::Cross(zaxis, xaxis);

		Matrix4x4 mat;
		mat.m[0] = xaxis.x; mat.m[1] = yaxis.x; mat.m[2] = zaxis.x; mat.m[3] = 0;
		mat.m[4] = xaxis.y; mat.m[5] = yaxis.y; mat.m[6] = zaxis.y; mat.m[7] = 0;
		mat.m[8] = xaxis.z; mat.m[9] = yaxis.z; mat.m[10] = zaxis.z; mat.m[11] = 0;
		mat.m[12] = -Vec3::Dot(xaxis, eye);
		mat.m[13] = -Vec3::Dot(yaxis, eye);
		mat.m[14] = -Vec3::Dot(zaxis, eye);
		mat.m[15] = 1.0f;
		return mat;
	}

	static Matrix4x4 PerspectiveFovLH(float fovY, float aspect, float nearZ, float farZ)
	{
		float yScale = 1.0f / tanf(fovY * 0.5f);
		float xScale = yScale / aspect;

		Matrix4x4 mat;
		mat.m[0] = xScale;
		mat.m[5] = yScale;
		mat.m[10] = farZ / (farZ - nearZ);
		mat.m[11] = 1.0f;
		mat.m[14] = -nearZ * farZ / (farZ - nearZ);
		return mat;
	}

	static Matrix4x4 Multiply(const Matrix4x4& a, const Matrix4x4& b)
	{
		Matrix4x4 result;
		for (int i = 0; i < 4; i++)
		{
			for (int j = 0; j < 4; j++)
			{
				result.m[i * 4 + j] = 0;
				for (int k = 0; k < 4; k++)
				{
					result.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
				}
			}
		}
		return result;
	}

	static Matrix4x4 RotationY(float angle)
	{
		float c = cosf(angle);
		float s = sinf(angle);

		Matrix4x4 mat = Identity();
		mat.m[0] = c;
		mat.m[2] = s;
		mat.m[8] = -s;
		mat.m[10] = c;
		return mat;
	}

	static Matrix4x4 Transpose(const Matrix4x4& mat)
	{
		Matrix4x4 result;
		for (int i = 0; i < 4; i++)
		{
			for (int j = 0; j < 4; j++)
			{
				result.m[i * 4 + j] = mat.m[j * 4 + i];
			}
		}
		return result;
	}

	static Matrix4x4 Scale(float sx, float sy, float sz)
	{
		Matrix4x4 mat = Identity();
		mat.m[0] = sx;
		mat.m[5] = sy;
		mat.m[10] = sz;
		return mat;
	}
};

class Camera
{
public:
	Vec3 position;
	Vec3 target;
	Vec3 up;

	float fov;
	float aspect;
	float nearZ;
	float farZ;

	Camera()
	{
		position = Vec3(0, 0, -3);
		target = Vec3(0, 0, 0);
		up = Vec3(0, 1, 0);
		fov = 3.14159f / 4.0f;
		aspect = 16.0f / 9.0f;
		nearZ = 0.1f;
		farZ = 10000.0f;
	}

	Matrix4x4 GetViewMatrix() const
	{
		return Matrix4x4::LookAtLH(position, target, up);
	}

	Matrix4x4 GetProjectionMatrix() const
	{
		return Matrix4x4::PerspectiveFovLH(fov, aspect, nearZ, farZ);
	}

	Matrix4x4 GetViewProjectionMatrix() const
	{
		return Matrix4x4::Multiply(GetViewMatrix(), GetProjectionMatrix());
	}
};
