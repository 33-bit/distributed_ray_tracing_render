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

// Reinhard tone map: compress unbounded HDR (e.g. bright specular highlights)
// into [0,1) before gamma, instead of a hard clamp that flattens highlights.
inline Color tone_map_reinhard(const Color& c) {
    return Color(c.x / (1.0 + c.x), c.y / (1.0 + c.y), c.z / (1.0 + c.z));
}

// Schlick approximation of the Fresnel reflectance: reflection grows toward
// grazing angles. cos_theta is |view·normal|; r0 is the head-on reflectance.
inline double fresnel_schlick(double cos_theta, double r0) {
    double m = clampd(1.0 - cos_theta, 0.0, 1.0);
    return r0 + (1.0 - r0) * (m * m * m * m * m);
}

// Head-on reflectance of a dielectric with index `ior` (surrounding medium air).
inline double schlick_r0(double ior) {
    double r = (1.0 - ior) / (1.0 + ior);
    return r * r;
}

// Snell refraction of unit dir v through a surface whose normal n faces against
// v; eta_ratio = n_incident / n_transmitted. Returns false on total internal
// reflection (no transmitted ray exists).
inline bool refract(const Vec3& v, const Vec3& n, double eta_ratio, Vec3& out) {
    double cos_i  = std::min(dot(-v, n), 1.0);
    double sin_t2 = eta_ratio * eta_ratio * (1.0 - cos_i * cos_i);
    if (sin_t2 > 1.0) return false;
    out = eta_ratio * (v + cos_i * n) - std::sqrt(1.0 - sin_t2) * n;
    return true;
}

// Smooth 0->1 ramp between edge0 and edge1 (Hermite). Used for the soft edge of
// a spotlight cone.
inline double smoothstep(double edge0, double edge1, double x) {
    double t = clampd((x - edge0) / (edge1 - edge0 + 1e-12), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Resolve the diffuse albedo at a point, applying the procedural checker if set.
inline Color effective_albedo(const Material& m, const Vec3& p) {
    if (!m.checker) return m.albedo;
    long long cx = static_cast<long long>(std::floor(p.x * m.checker_scale));
    long long cy = static_cast<long long>(std::floor(p.y * m.checker_scale));
    long long cz = static_cast<long long>(std::floor(p.z * m.checker_scale));
    return ((cx + cy + cz) & 1LL) ? m.albedo2 : m.albedo;
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

    // Dielectric (glass): split into a Fresnel-weighted reflected + refracted ray.
    if (m.type == MatType::Dielectric) {
        if (depth >= max_depth) return scene.background(r);
        Vec3   unit  = normalized(r.dir);
        Vec3   nn    = rec.normal;                                   // faces against the ray
        double eta   = rec.front_face ? (1.0 / m.ior) : m.ior;       // air->glass or glass->air
        double cos_i = std::min(dot(-unit, nn), 1.0);
        double fres  = fresnel_schlick(cos_i, schlick_r0(m.ior));

        // Beer-Lambert: a ray that travelled `rec.t` *inside* the glass (i.e. it
        // is now exiting, back face) is attenuated by exp(-absorption · distance),
        // which tints thick glass. Clear glass (absorption 0) is unaffected.
        Color absorb(1, 1, 1);
        if (!rec.front_face)
            absorb = Color(std::exp(-m.absorption.x * rec.t),
                           std::exp(-m.absorption.y * rec.t),
                           std::exp(-m.absorption.z * rec.t));

        Color reflected = shade(scene, Ray(rec.p + nn * 1e-4, reflect(unit, nn)),
                                depth + 1, max_depth, shadow_samples, rng);
        Vec3 refr;
        if (refract(unit, nn, eta, refr)) {
            Color transmitted = shade(scene, Ray(rec.p - nn * 1e-4, normalized(refr)),
                                      depth + 1, max_depth, shadow_samples, rng);
            return absorb * m.albedo * lerp(transmitted, reflected, fres);
        }
        return absorb * m.albedo * reflected;   // total internal reflection -> all reflected
    }

    const Vec3 n    = rec.normal;
    const Vec3 view = normalized(-r.dir);
    const Color alb = effective_albedo(m, rec.p);

    // small ambient so shadowed regions are dark but not pure black
    Color result = alb * 0.08;

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

            Vec3   to   = lp - rec.p;
            double dist = to.length();
            Vec3   ldir = to / dist;
            double ndl  = std::max(0.0, dot(n, ldir));

            // Spotlight cone and distance attenuation scale this light's power.
            double factor = 1.0;
            if (L.is_spot)
                factor *= smoothstep(L.cos_outer, L.cos_inner, dot(-ldir, L.spot_dir));
            if (L.attenuate)
                factor *= 1.0 / (1.0 + L.atten_k * dist * dist);
            if (factor <= 0.0) continue;

            // Lambertian diffuse
            contribution += alb * L.intensity * (ndl * factor);

            // Phong specular
            if (m.specular > 0.0 && ndl > 0.0) {
                Vec3   refl = reflect(-ldir, n);
                double rv   = std::max(0.0, dot(refl, view));
                contribution += L.intensity * (m.specular * std::pow(rv, m.shininess) * factor);
            }
        }
        result += contribution * (1.0 / samples);  // average -> penumbra from partial visibility
    }

    // Recursive mirror reflection, blended by reflectivity.
    if (m.reflectivity > 0.0 && depth < max_depth) {
        Vec3 rdir = normalized(reflect(r.dir, n));
        Ray  rray(rec.p + n * 1e-4, rdir);   // offset along normal to avoid self-hit
        Color reflected = shade(scene, rray, depth + 1, max_depth, shadow_samples, rng);
        // Fresnel (Schlick): reflect more at grazing angles; R0 = reflectivity.
        double fres = fresnel_schlick(std::fabs(dot(view, n)), m.reflectivity);
        result = lerp(result, reflected, fres);
    }

    return result;
}
