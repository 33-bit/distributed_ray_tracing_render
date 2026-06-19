#pragma once
// Member A — Color is just a Vec3 of RGB in [0,1]. Sharing the type lets the
// shader write `albedo * light` as one component-wise multiply.
#include "core/vec3.hpp"

using Color = Vec3;

inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline Color clamp_color(const Color& c) {
    return Color(clampd(c.x, 0.0, 1.0), clampd(c.y, 0.0, 1.0), clampd(c.z, 0.0, 1.0));
}
