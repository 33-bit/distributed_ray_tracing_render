#pragma once
// Member B — a light source. One struct covers both cases via `radius`:
//   radius == 0  -> point light  -> a single shadow ray -> hard shadows
//   radius  > 0  -> area light   -> many jittered shadow rays -> soft shadows
// Modeling the area light as a small sphere (sampled with RNG::in_unit_sphere)
// keeps it orientation-free — no need to track a disk's facing direction.
#include "core/color.hpp"

struct Light {
    Vec3   position;     // center of the light
    Color  intensity;    // emitted color / brightness
    double radius = 0.0; // 0 = point, >0 = spherical area light (penumbra width)
};
