#pragma once
// Member B — a light source. One struct covers point, area, and spot lights via
// flags, so Member C can still ship it as a flat block of numbers:
//   radius == 0    -> point light  -> a single shadow ray -> hard shadows
//   radius  > 0    -> area light   -> many jittered shadow rays -> soft shadows
//   is_spot        -> emission limited to a cone around spot_dir (smooth edge)
//   attenuate      -> intensity falls off with distance (inverse-square-ish)
#include <cmath>
#include "core/color.hpp"

struct Light {
    Vec3   position;
    Color  intensity;
    double radius = 0.0;        // 0 = point, >0 = spherical area light

    // Spotlight cone: full intensity where cos(angle) >= cos_inner, zero past
    // cos_outer, smooth falloff between (cos_outer < cos_inner).
    bool   is_spot   = false;
    Vec3   spot_dir  = Vec3(0, -1, 0);
    double cos_inner = 0.95;
    double cos_outer = 0.90;

    // Distance attenuation: intensity *= 1 / (1 + atten_k * d^2).
    bool   attenuate = false;
    double atten_k   = 0.0;

    // --- named constructors (keep scene building readable) ---
    static Light point(const Vec3& p, const Color& c) { return Light{p, c, 0.0}; }
    static Light area(const Vec3& p, const Color& c, double r) { return Light{p, c, r}; }
    static Light spot(const Vec3& p, const Color& c, const Vec3& dir,
                      double inner_deg, double outer_deg, double r = 0.0) {
        Light L{p, c, r};
        L.is_spot   = true;
        L.spot_dir  = normalized(dir);
        L.cos_inner = std::cos(inner_deg * PI / 180.0);
        L.cos_outer = std::cos(outer_deg * PI / 180.0);
        return L;
    }
    Light& with_attenuation(double k) { attenuate = true; atten_k = k; return *this; }
};
