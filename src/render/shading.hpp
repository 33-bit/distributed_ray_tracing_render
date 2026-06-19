#pragma once
// Member B — the shading core: turn a ray into a color.
//
// shade() is intentionally written against ISceneQuery, an abstract *view* of
// the scene (intersect + look up material/lights/background), NOT against Member
// D's concrete Scene. That keeps lighting decoupled from how the scene is stored
// or distributed: the same shade() runs unchanged in the sequential renderer and
// inside every MPI worker.
#include <vector>
#include <cmath>
#include <algorithm>
#include "core/ray.hpp"
#include "core/color.hpp"
#include "core/random.hpp"
#include "scene/object.hpp"
#include "scene/material.hpp"
#include "scene/light.hpp"

// Abstract scene view consumed by the shader. Implemented by Member D's Scene.
struct ISceneQuery {
    virtual bool hit(const Ray& r, double tmin, double tmax, HitRecord& rec) const = 0;
    virtual const Material& material(int id) const = 0;
    virtual const std::vector<Light>& lights() const = 0;
    virtual Color background(const Ray& r) const = 0;
    virtual ~ISceneQuery() = default;
};

// sRGB-ish gamma for display. Applied once by the renderer when writing pixels,
// not inside shade() — so reflection recursion stays in linear light.
inline Color gamma_correct(Color c, double gamma = 2.2) {
    double inv = 1.0 / gamma;
    return Color(std::pow(clampd(c.x, 0.0, 1.0), inv),
                 std::pow(clampd(c.y, 0.0, 1.0), inv),
                 std::pow(clampd(c.z, 0.0, 1.0), inv));
}

// Is the segment from p to light_pos blocked by any object?
inline bool occluded(const ISceneQuery& s, const Vec3& p, const Vec3& light_pos) {
    Vec3   to   = light_pos - p;
    double dist = to.length();
    Ray    shadow(p, to / dist);
    HitRecord tmp;
    // (1e-3, dist-1e-3): skip self-intersection at p and don't count the light.
    return s.hit(shadow, 1e-3, dist - 1e-3, tmp);
}

// Recursive ray tracer. depth counts reflection bounces; max_depth caps them.
inline Color shade(const ISceneQuery& scene, const Ray& r,
                   int depth, int max_depth, int shadow_samples, RNG& rng) {
    HitRecord rec;
    if (!scene.hit(r, 1e-4, INF, rec))
        return scene.background(r);

    const Material& m = scene.material(rec.material_id);
    if (m.type == MatType::Emissive)
        return m.emission;

    const Vec3 n    = rec.normal;
    const Vec3 view = normalized(-r.dir);

    // small ambient so shadowed regions are dark but not pure black
    Color result = m.albedo * 0.08;

    for (const Light& L : scene.lights()) {
        // point light -> 1 shadow ray (hard); area light -> N jittered (soft)
        const int samples = (L.radius > 0.0 && shadow_samples > 1) ? shadow_samples : 1;
        Color contribution(0, 0, 0);

        for (int i = 0; i < samples; ++i) {
            Vec3 lp = L.position;
            if (L.radius > 0.0 && samples > 1)
                lp = L.position + L.radius * rng.in_unit_sphere();  // sample the light volume

            if (occluded(scene, rec.p, lp))
                continue;  // this sample is in shadow -> contributes nothing

            Vec3   ldir = normalized(lp - rec.p);
            double ndl  = std::max(0.0, dot(n, ldir));

            // Lambertian diffuse
            contribution += m.albedo * L.intensity * ndl;

            // Phong specular
            if (m.specular > 0.0 && ndl > 0.0) {
                Vec3   refl = reflect(-ldir, n);
                double rv   = std::max(0.0, dot(refl, view));
                contribution += L.intensity * (m.specular * std::pow(rv, m.shininess));
            }
        }
        result += contribution * (1.0 / samples);  // average -> penumbra from partial visibility
    }

    // Recursive mirror reflection, blended by reflectivity.
    if (m.reflectivity > 0.0 && depth < max_depth) {
        Vec3 rdir = normalized(reflect(r.dir, n));
        Ray  rray(rec.p + n * 1e-4, rdir);   // offset along normal to avoid self-hit
        Color reflected = shade(scene, rray, depth + 1, max_depth, shadow_samples, rng);
        result = lerp(result, reflected, m.reflectivity);
    }

    return result;
}
