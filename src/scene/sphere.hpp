#pragma once
// Member A — ray/sphere intersection.
//
// Solves |o + t·d - c|² = r². Using half_b = b/2 removes the 4's from the
// quadratic formula (disc = half_b² - a·c), which is the standard, slightly
// cheaper and more numerically tidy form.
#include "scene/object.hpp"

struct Sphere : Hittable {
    Vec3   center;
    double radius;
    int    material_id;

    Sphere(const Vec3& c, double r, int mat) : center(c), radius(r), material_id(mat) {}

    bool hit(const Ray& r, double tmin, double tmax, HitRecord& rec) const override {
        Vec3   oc     = r.origin - center;
        double a      = dot(r.dir, r.dir);
        double half_b = dot(oc, r.dir);
        double c      = dot(oc, oc) - radius * radius;
        double disc   = half_b * half_b - a * c;
        if (disc < 0.0) return false;

        double sq = std::sqrt(disc);
        // try the nearer root first, then the farther one
        double root = (-half_b - sq) / a;
        if (root < tmin || root > tmax) {
            root = (-half_b + sq) / a;
            if (root < tmin || root > tmax) return false;
        }

        rec.t = root;
        rec.p = r.at(root);
        Vec3 outward = (rec.p - center) / radius;
        rec.set_face_normal(r, outward);
        rec.material_id = material_id;
        return true;
    }

    bool bounding_box(AABB& out) const override {
        out = AABB(center - Vec3(radius, radius, radius), center + Vec3(radius, radius, radius));
        return true;
    }
};
