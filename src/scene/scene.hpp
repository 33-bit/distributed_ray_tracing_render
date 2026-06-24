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
#include "scene/triangle.hpp"
#include "scene/box.hpp"
#include "scene/material.hpp"
#include "scene/light.hpp"
#include "scene/bvh.hpp"
#include "render/shading.hpp"   // ISceneQuery

struct Scene : ISceneQuery {
    std::vector<std::unique_ptr<Hittable>> objects;
    std::vector<Material> materials;
    std::vector<Light>    lights_;
    Camera camera;
    Color  bg_top    = Color(0.45, 0.62, 0.95);   // sky
    Color  bg_bottom = Color(0.95, 0.95, 0.98);    // horizon haze
    // How many units of dir.y it takes to reach pure bg_top, starting from the
    // horizon (dir.y=0). Default 1.0 reproduces the original plain linear
    // gradient over the full [-1,1] range (every existing scene keeps its
    // exact look). A camera that looks roughly level (common for ground-level,
    // human-eye-height shots) only ever sees a narrow dir.y band near 0, so a
    // real sunset's saturated horizon color would otherwise always render as
    // a 50/50 blend with the zenith color — gray, not gold. Set this below 1.0
    // (e.g. 0.45) to concentrate the gradient near the horizon instead, like a
    // real sky's color actually behaves.
    double bg_horizon = 1.0;

    // Optional BVH acceleration (off by default — see RenderParams::use_bvh).
    // Built once after the scene's objects are all added; unbounded objects
    // (infinite planes) are kept in their own list and always tested linearly.
    bool use_bvh = false;
    std::unique_ptr<BVHNode> bvh_root;
    std::vector<Hittable*> unbounded_objects;

    int add_material(const Material& m) { materials.push_back(m); return static_cast<int>(materials.size()) - 1; }
    void add_sphere(const Vec3& c, double r, int mat) { objects.push_back(std::make_unique<Sphere>(c, r, mat)); }
    void add_plane(const Vec3& p, const Vec3& n, int mat) { objects.push_back(std::make_unique<Plane>(p, n, mat)); }
    void add_triangle(const Vec3& a, const Vec3& b, const Vec3& c, int mat) { objects.push_back(std::make_unique<Triangle>(a, b, c, mat)); }
    void add_box(const Vec3& center, const Vec3& size, int mat) { objects.push_back(std::make_unique<Box>(center, size, mat)); }

    // Split objects into a BVH (bounded primitives) + a small linear list
    // (unbounded primitives, e.g. planes) and build the tree. Call after every
    // object has been added; safe to call again if the object list changes.
    void enable_bvh(bool on) {
        use_bvh = on;
        if (!on) return;
        std::vector<Hittable*> bounded;
        unbounded_objects.clear();
        AABB box;
        for (const auto& o : objects) {
            if (o->bounding_box(box)) bounded.push_back(o.get());
            else unbounded_objects.push_back(o.get());
        }
        bvh_root = bounded.empty() ? nullptr : BVHNode::build(bounded, 0, bounded.size());
    }

    // --- ISceneQuery ---
    bool hit(const Ray& r, double tmin, double tmax, HitRecord& rec) const override {
        bool found = false;
        double closest = tmax;
        HitRecord tmp;
        if (use_bvh && bvh_root) {
            if (bvh_root->hit(r, tmin, closest, tmp)) { found = true; closest = tmp.t; rec = tmp; }
            for (Hittable* o : unbounded_objects)
                if (o->hit(r, tmin, closest, tmp)) { found = true; closest = tmp.t; rec = tmp; }
            return found;
        }
        for (const auto& o : objects)
            if (o->hit(r, tmin, closest, tmp)) { found = true; closest = tmp.t; rec = tmp; }
        return found;
    }
    const Material& material(int id) const override { return materials[id]; }
    const std::vector<Light>& lights() const override { return lights_; }
    Color background(const Ray& r) const override {
        double t = 0.5 * (normalized(r.dir).y / bg_horizon + 1.0);   // vertical gradient
        t = clampd(t, 0.0, 1.0);
        return lerp(bg_bottom, bg_top, t);
    }
};

// The canonical demo scene: a checker floor + four spheres (diffuse / mirror /
// glossy / gold). The camera orbits the scene and the light sweeps the opposite
// way over the animation, so shadows rotate across the floor. The geometry is
// fixed; only the camera and light depend on the frame, keeping this a pure
// function of (aspect, frame, total_frames).
inline Scene build_demo_scene(double aspect, int frame, int total_frames) {
    Scene s;

    int m_floor  = s.add_material(Material::checkerboard(Color(0.9, 0.9, 0.9), Color(0.15, 0.2, 0.3), 1.0));
    int m_red    = s.add_material(Material::diffuse(Color(0.85, 0.15, 0.15)));
    int m_mirror = s.add_material(Material::mirror(Color(1, 1, 1), 0.9));
    int m_glass  = s.add_material(Material::dielectric(1.5));            // clear glass sphere
    int m_gold   = s.add_material(Material::glossy(Color(0.9, 0.7, 0.2), 0.6, 48.0));

    s.add_plane(Vec3(0, 0, 0), Vec3(0, 1, 0), m_floor);
    s.add_sphere(Vec3(-1.6, 1.0, 0.0), 1.0, m_red);     // diffuse
    s.add_sphere(Vec3( 0.0, 1.0, 0.0), 1.0, m_mirror);  // mirror
    s.add_sphere(Vec3( 1.6, 1.0, 0.0), 1.0, m_glass);   // refractive glass
    s.add_sphere(Vec3( 0.0, 0.5, 2.0), 0.5, m_gold);    // glossy gold

    // A small triangle pyramid (4 triangles) to the left, showing the Triangle
    // primitive alongside the spheres.
    int m_pyr = s.add_material(Material::diffuse(Color(0.65, 0.25, 0.8)));   // purple
    Vec3 pa(-3.7, 0.0, 0.7), pb(-2.5, 0.0, 1.0), pc(-3.1, 0.0, -0.6), apex(-3.1, 1.6, 0.35);
    s.add_triangle(pa, pb, apex, m_pyr);
    s.add_triangle(pb, pc, apex, m_pyr);
    s.add_triangle(pc, pa, apex, m_pyr);
    s.add_triangle(pa, pc, pb, m_pyr);   // base

    // Normalized time in [0,1) over the whole sequence (0 for a single frame).
    double t = (total_frames > 1) ? static_cast<double>(frame) / total_frames : 0.0;

    // Camera orbits the scene center once over the full animation.
    const Vec3 center(0.0, 1.0, 0.0);
    double cam_angle = 2.0 * PI * t;
    double cam_radius = 7.0, cam_height = 2.3;
    Vec3 eye(center.x + cam_radius * std::sin(cam_angle),
             cam_height,
             center.z + cam_radius * std::cos(cam_angle));
    s.camera = Camera(eye, center, Vec3(0, 1, 0), 40.0, aspect);

    // Light sweeps the opposite direction (with a phase offset) so the shadows
    // rotate across the floor and the demo clearly shows moving shadows.
    double light_angle = -2.0 * PI * t + 0.7;
    double light_radius = 5.0;
    s.lights_.push_back(Light{
        Vec3(light_radius * std::sin(light_angle), 6.0, light_radius * std::cos(light_angle)),
        Color(1.0, 0.95, 0.9),
        0.6 });   // area light: radius 0.6 -> soft shadows when --shadow-samples > 1
                  // (with shadow-samples=1 it degenerates to a hard point light)

    // A cool cyan spotlight from above-front, aimed at the scene, to show the
    // cone falloff as a tinted pool of light on the floor.
    s.lights_.push_back(Light::spot(Vec3(0.0, 7.0, 2.0), Color(0.20, 0.45, 0.65),
                                    Vec3(0.0, -1.0, -0.25), 16.0, 30.0));

    return s;
}
