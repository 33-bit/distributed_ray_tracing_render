#pragma once
// Axis-aligned box (AABB) — ray intersection via the Kay-Kajiya slab method.
#include "scene/object.hpp"

struct Box : Hittable {
    Vec3 bmin, bmax;
    int  material_id;

    Box(const Vec3& center, const Vec3& size, int mat)
        : bmin(center - size * 0.5), bmax(center + size * 0.5), material_id(mat) {}

    bool hit(const Ray& r, double tmin, double tmax, HitRecord& rec) const override {
        double t_enter = -INF;
        double t_exit  =  INF;
        int  enter_axis = 0, exit_axis = 0;
        bool enter_neg  = false, exit_neg = false;

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
            bool neg = (inv < 0.0);
            if (neg) std::swap(t0, t1);

            if (t0 > t_enter) { t_enter = t0; enter_axis = a; enter_neg = !neg; }
            if (t1 < t_exit)  { t_exit  = t1; exit_axis  = a; exit_neg  =  neg; }
            if (t_enter > t_exit) return false;
        }

        auto make_normal = [](int axis, double sign) {
            Vec3 n(0, 0, 0);
            if (axis == 0) n.x = sign;
            else if (axis == 1) n.y = sign;
            else n.z = sign;
            return n;
        };

        if (t_enter >= tmin && t_enter <= tmax) {
            rec.t = t_enter;
            rec.p = r.at(t_enter);
            rec.set_face_normal(r, make_normal(enter_axis, enter_neg ? -1.0 : 1.0));
            rec.material_id = material_id;
            return true;
        }
        if (t_exit >= tmin && t_exit <= tmax) {
            rec.t = t_exit;
            rec.p = r.at(t_exit);
            rec.set_face_normal(r, make_normal(exit_axis, exit_neg ? -1.0 : 1.0));
            rec.material_id = material_id;
            return true;
        }
        return false;
    }
};
