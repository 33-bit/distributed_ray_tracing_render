#pragma once
// Member A — ray/infinite-plane intersection. The ground plane that catches
// shadows is a Plane. Solves dot(n, o + t·d - p0) = 0 for t.
#include "scene/object.hpp"

struct Plane : Hittable {
    Vec3   point;        // a point on the plane
    Vec3   normal;       // unit normal
    int    material_id;

    Plane(const Vec3& p, const Vec3& n, int mat)
        : point(p), normal(normalized(n)), material_id(mat) {}

    bool hit(const Ray& r, double tmin, double tmax, HitRecord& rec) const override {
        double denom = dot(normal, r.dir);
        if (std::fabs(denom) < 1e-9) return false;   // ray parallel to plane
        double t = dot(point - r.origin, normal) / denom;
        if (t < tmin || t > tmax) return false;

        rec.t = t;
        rec.p = r.at(t);
        rec.set_face_normal(r, normal);
        rec.material_id = material_id;
        return true;
    }
};
