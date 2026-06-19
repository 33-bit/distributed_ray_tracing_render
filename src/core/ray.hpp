#pragma once
// Member A — a ray: an origin and a (conventionally normalized) direction.
#include "core/vec3.hpp"

struct Ray {
    Vec3 origin;
    Vec3 dir;

    Ray() {}
    Ray(const Vec3& o, const Vec3& d) : origin(o), dir(d) {}

    // Point at parameter t along the ray.
    Vec3 at(double t) const { return origin + t * dir; }
};
