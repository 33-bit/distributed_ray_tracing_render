#pragma once
// Member A — the abstract surface interface and the hit record.
//
// HitRecord is the single struct every intersectable returns and every shader
// consumes, so it is the contract between geometry (A) and shading (B).
// set_face_normal() forces the stored normal to face *against* the incoming
// ray; B can then light without re-checking orientation, and refraction (if
// ever added) knows which side it entered from via `front_face`.
#include "core/ray.hpp"

struct HitRecord {
    double t;            // ray parameter at the hit
    Vec3   p;            // hit point in world space
    Vec3   normal;       // unit normal, oriented against the ray
    int    material_id;  // index into Scene's material table
    bool   front_face;   // true if the ray hit the outward-facing side

    void set_face_normal(const Ray& r, const Vec3& outward_normal) {
        front_face = dot(r.dir, outward_normal) < 0.0;
        normal = front_face ? outward_normal : -outward_normal;
    }
};

struct Hittable {
    virtual ~Hittable() = default;
    // Report the nearest hit in (tmin, tmax); return false if none.
    virtual bool hit(const Ray& r, double tmin, double tmax, HitRecord& rec) const = 0;
};
