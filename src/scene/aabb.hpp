#pragma once
// Member A — axis-aligned bounding box, used by the BVH (scene/bvh.hpp) to
// prune ray/object tests. Same slab method as Box::hit() (scene/box.hpp), but
// boolean-only: the BVH only needs "does this ray enter the box within
// (tmin,tmax)", not a hit record.
#include <algorithm>
#include <cmath>
#include "core/ray.hpp"

struct AABB {
    Vec3 bmin = Vec3( INF,  INF,  INF);
    Vec3 bmax = Vec3(-INF, -INF, -INF);

    AABB() = default;
    AABB(const Vec3& a, const Vec3& b) : bmin(a), bmax(b) {}

    static AABB surrounding(const AABB& a, const AABB& b) {
        return AABB(
            Vec3(std::min(a.bmin.x, b.bmin.x), std::min(a.bmin.y, b.bmin.y), std::min(a.bmin.z, b.bmin.z)),
            Vec3(std::max(a.bmax.x, b.bmax.x), std::max(a.bmax.y, b.bmax.y), std::max(a.bmax.z, b.bmax.z)));
    }

    Vec3 centroid() const { return (bmin + bmax) * 0.5; }

    int longest_axis() const {
        Vec3 d = bmax - bmin;
        if (d.x > d.y && d.x > d.z) return 0;
        return (d.y > d.z) ? 1 : 2;
    }

    bool hit(const Ray& r, double tmin, double tmax) const {
        const double origin[3] = { r.origin.x, r.origin.y, r.origin.z };
        const double dir[3]    = { r.dir.x,    r.dir.y,    r.dir.z    };
        const double lo[3]     = { bmin.x,     bmin.y,     bmin.z     };
        const double hi[3]     = { bmax.x,     bmax.y,     bmax.z     };
        for (int a = 0; a < 3; ++a) {
            if (std::fabs(dir[a]) < 1e-30) {
                if (origin[a] < lo[a] || origin[a] > hi[a]) return false;
                continue;
            }
            double inv = 1.0 / dir[a];
            double t0 = (lo[a] - origin[a]) * inv;
            double t1 = (hi[a] - origin[a]) * inv;
            if (inv < 0.0) std::swap(t0, t1);
            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);
            if (tmin > tmax) return false;
        }
        return true;
    }
};
