#pragma once
// Pinhole / thin-lens camera. Builds an orthonormal basis (u,v,w) from eye /
// lookat / up and maps screen coords (s,t) in [0,1] to world-space rays.
// When aperture > 0, rays are jittered on a lens disk for depth-of-field bokeh.
#include "core/ray.hpp"
#include "core/random.hpp"

struct Camera {
    Vec3   origin;
    Vec3   lower_left;
    Vec3   horizontal;
    Vec3   vertical;
    Vec3   u_axis, v_axis;
    double lens_radius = 0.0;

    Camera() {}

    Camera(const Vec3& eye, const Vec3& lookat, const Vec3& up,
           double vfov_deg, double aspect,
           double aperture = 0.0, double focus_dist_in = 0.0) {
        double theta = vfov_deg * PI / 180.0;
        double h = std::tan(theta / 2.0);
        double focus_dist = (focus_dist_in > 0.0) ? focus_dist_in : (eye - lookat).length();
        double viewport_h = 2.0 * h * focus_dist;
        double viewport_w = aspect * viewport_h;

        Vec3 w = normalized(eye - lookat);
        u_axis = normalized(cross(up, w));
        v_axis = cross(w, u_axis);

        origin      = eye;
        horizontal  = viewport_w * u_axis;
        vertical    = viewport_h * v_axis;
        lower_left  = origin - horizontal * 0.5 - vertical * 0.5 - focus_dist * w;
        lens_radius = aperture / 2.0;
    }

    Ray get_ray(double s, double t, RNG* rng = nullptr) const {
        if (lens_radius > 0.0 && rng) {
            Vec3 rd     = lens_radius * rng->in_unit_disk();
            Vec3 offset = u_axis * rd.x + v_axis * rd.y;
            Vec3 target = lower_left + s * horizontal + t * vertical;
            return Ray(origin + offset, normalized(target - origin - offset));
        }
        Vec3 dir = lower_left + s * horizontal + t * vertical - origin;
        return Ray(origin, normalized(dir));
    }
};
