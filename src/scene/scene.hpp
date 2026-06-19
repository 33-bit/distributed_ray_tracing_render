#pragma once
// Member D — the concrete scene: the object list, material/light tables, camera,
// and sky background. Scene IS-A ISceneQuery, so the same object is handed
// straight to Member B's shade() and to Member C's MPI workers.
//
// build_demo_scene() is the single source of truth for "what we render". It is
// a pure function of (aspect, frame, total_frames) — given a frame index it
// returns the fully-posed scene for that frame, which is what makes the
// animation reproducible across the sequential and MPI renderers (Task 5 fills
// in the per-frame camera/light motion; for now it is a static pose).
#include <vector>
#include <memory>
#include "core/color.hpp"
#include "scene/camera.hpp"
#include "scene/sphere.hpp"
#include "scene/plane.hpp"
#include "scene/material.hpp"
#include "scene/light.hpp"
#include "render/shading.hpp"   // ISceneQuery

struct Scene : ISceneQuery {
    std::vector<std::unique_ptr<Hittable>> objects;
    std::vector<Material> materials;
    std::vector<Light>    lights_;
    Camera camera;
    Color  bg_top    = Color(0.45, 0.62, 0.95);   // sky
    Color  bg_bottom = Color(0.95, 0.95, 0.98);    // horizon haze

    int add_material(const Material& m) { materials.push_back(m); return static_cast<int>(materials.size()) - 1; }
    void add_sphere(const Vec3& c, double r, int mat) { objects.push_back(std::make_unique<Sphere>(c, r, mat)); }
    void add_plane(const Vec3& p, const Vec3& n, int mat) { objects.push_back(std::make_unique<Plane>(p, n, mat)); }

    // --- ISceneQuery ---
    bool hit(const Ray& r, double tmin, double tmax, HitRecord& rec) const override {
        bool found = false;
        double closest = tmax;
        HitRecord tmp;
        for (const auto& o : objects)
            if (o->hit(r, tmin, closest, tmp)) { found = true; closest = tmp.t; rec = tmp; }
        return found;
    }
    const Material& material(int id) const override { return materials[id]; }
    const std::vector<Light>& lights() const override { return lights_; }
    Color background(const Ray& r) const override {
        double t = 0.5 * (normalized(r.dir).y + 1.0);   // vertical gradient
        return lerp(bg_bottom, bg_top, t);
    }
};

// The canonical demo scene: a checker floor + four spheres (diffuse / mirror /
// glossy / gold) under one warm light. Camera and light motion are added in
// Task 5; here the pose is fixed.
inline Scene build_demo_scene(double aspect, int frame, int total_frames) {
    (void)frame; (void)total_frames;   // static for Task 4; animated in Task 5
    Scene s;

    int m_floor  = s.add_material(Material::checkerboard(Color(0.9, 0.9, 0.9), Color(0.15, 0.2, 0.3), 1.0));
    int m_red    = s.add_material(Material::diffuse(Color(0.85, 0.15, 0.15)));
    int m_mirror = s.add_material(Material::mirror(Color(1, 1, 1), 0.9));
    int m_green  = s.add_material(Material::glossy(Color(0.2, 0.6, 0.3), 0.8, 64.0));
    int m_gold   = s.add_material(Material::glossy(Color(0.9, 0.7, 0.2), 0.6, 48.0));

    s.add_plane(Vec3(0, 0, 0), Vec3(0, 1, 0), m_floor);
    s.add_sphere(Vec3(-1.6, 1.0, 0.0), 1.0, m_red);
    s.add_sphere(Vec3( 0.0, 1.0, 0.0), 1.0, m_mirror);
    s.add_sphere(Vec3( 1.6, 1.0, 0.0), 1.0, m_green);
    s.add_sphere(Vec3( 0.0, 0.5, 2.0), 0.5, m_gold);

    s.lights_.push_back(Light{ Vec3(4, 6, 3), Color(1.0, 0.95, 0.9), 0.0 });

    s.camera = Camera(Vec3(0, 2.2, 7), Vec3(0, 1.0, 0), Vec3(0, 1, 0), 40.0, aspect);
    return s;
}
