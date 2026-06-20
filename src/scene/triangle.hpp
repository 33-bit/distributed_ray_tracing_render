#pragma once
// Member A — ray/triangle intersection via the Möller–Trumbore algorithm.
//
// A triangle is the building block for arbitrary flat surfaces and (later)
// triangle meshes. Möller–Trumbore solves the ray/triangle test directly in
// barycentric coordinates (u,v) without precomputing the plane equation, which
// is the standard, branch-light formulation. Triangles here are double-sided
// (we don't cull back faces) so a single triangle is visible from either side.
#include "scene/object.hpp"

struct Triangle : Hittable {
    Vec3 v0, v1, v2;
    int  material_id;

    Triangle(const Vec3& a, const Vec3& b, const Vec3& c, int mat)
        : v0(a), v1(b), v2(c), material_id(mat) {}

    bool hit(const Ray& r, double tmin, double tmax, HitRecord& rec) const override {
        const double EPS = 1e-9;
        Vec3   e1  = v1 - v0;
        Vec3   e2  = v2 - v0;
        Vec3   pv  = cross(r.dir, e2);
        double det = dot(e1, pv);
        if (std::fabs(det) < EPS) return false;        // ray parallel to triangle

        double inv = 1.0 / det;
        Vec3   tv  = r.origin - v0;
        double u   = dot(tv, pv) * inv;                 // barycentric u
        if (u < 0.0 || u > 1.0) return false;

        Vec3   qv  = cross(tv, e1);
        double v   = dot(r.dir, qv) * inv;              // barycentric v
        if (v < 0.0 || u + v > 1.0) return false;

        double dist = dot(e2, qv) * inv;
        if (dist < tmin || dist > tmax) return false;

        rec.t = dist;
        rec.p = r.at(dist);
        rec.set_face_normal(r, normalized(cross(e1, e2)));
        rec.material_id = material_id;
        return true;
    }
};
