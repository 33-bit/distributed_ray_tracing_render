#pragma once
// Member A — 3D vector / point / RGB triple.
//
// One struct serves geometry (points, directions) AND color (see color.hpp's
// `using Color = Vec3`). Component-wise operator* doubles as color modulation.
// We use `double` throughout: an 8-core demo is not perf-bound on FP width, and
// double avoids the shadow-acne / self-intersection precision headaches that
// float invites at grazing angles.
#include <cmath>

constexpr double PI  = 3.14159265358979323846;
constexpr double INF = 1e30;

struct Vec3 {
    double x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator-() const { return Vec3(-x, -y, -z); }

    Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    Vec3& operator*=(double s)      { x *= s;   y *= s;   z *= s;   return *this; }

    double length_squared() const { return x * x + y * y + z * z; }
    double length()         const { return std::sqrt(length_squared()); }
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
// component-wise multiply — used both for scaling and for color * color modulation
inline Vec3 operator*(const Vec3& a, const Vec3& b) { return Vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline Vec3 operator*(const Vec3& a, double s)      { return Vec3(a.x * s, a.y * s, a.z * s); }
inline Vec3 operator*(double s, const Vec3& a)      { return a * s; }
inline Vec3 operator/(const Vec3& a, double s)      { return a * (1.0 / s); }

inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3   cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

inline Vec3 normalized(const Vec3& v) { double l = v.length(); return l > 0 ? v / l : v; }

// Mirror reflection of incoming direction v about surface normal n (n unit).
inline Vec3 reflect(const Vec3& v, const Vec3& n) { return v - 2.0 * dot(v, n) * n; }

inline bool near_zero(const Vec3& v) {
    const double s = 1e-8;
    return std::fabs(v.x) < s && std::fabs(v.y) < s && std::fabs(v.z) < s;
}
