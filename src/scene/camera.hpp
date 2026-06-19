#pragma once
// Member A — pinhole camera. Builds an orthonormal basis (u,v,w) from eye /
// lookat / up and maps screen coords (u,v) in [0,1] to world-space rays.
// vfov is vertical field of view in degrees; aspect = width/height.
#include "core/ray.hpp"

struct Camera {
    Vec3 origin;
    Vec3 lower_left;   // world position of the (0,0) screen corner
    Vec3 horizontal;   // spans the viewport width
    Vec3 vertical;     // spans the viewport height

    Camera() {}

    Camera(const Vec3& eye, const Vec3& lookat, const Vec3& up,
           double vfov_deg, double aspect) {
        double theta = vfov_deg * PI / 180.0;
        double h = std::tan(theta / 2.0);
        double viewport_h = 2.0 * h;
        double viewport_w = aspect * viewport_h;

        Vec3 w = normalized(eye - lookat);   // camera looks toward -w
        Vec3 u = normalized(cross(up, w));
        Vec3 v = cross(w, u);

        origin     = eye;
        horizontal = viewport_w * u;
        vertical   = viewport_h * v;
        lower_left = origin - horizontal * 0.5 - vertical * 0.5 - w;
    }

    // u,v in [0,1] with (0,0) at the lower-left of the image.
    Ray get_ray(double u, double v) const {
        Vec3 dir = lower_left + u * horizontal + v * vertical - origin;
        return Ray(origin, normalized(dir));
    }
};
