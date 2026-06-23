// Unit-test driver for the renderer core (Members A & B).
//
// This is the project's TDD safety net: every member appends asserts here when
// they land a module, so `make test` stays green as the codebase grows. We use
// a tiny hand-rolled CHECK macro instead of a test framework to keep the build
// dependency-free (only STL + MPI are allowed).
//
// Run:  make test
#include <cstdio>
#include <cmath>

// modules under test (Member A — rendering core)
#include "core/vec3.hpp"
#include "core/ray.hpp"
#include "core/color.hpp"
#include "core/random.hpp"
#include "scene/object.hpp"
#include "scene/sphere.hpp"
#include "scene/plane.hpp"
#include "scene/triangle.hpp"
#include "scene/box.hpp"
#include "scene/camera.hpp"
// modules under test (Member B — shading)
#include <vector>
#include "scene/material.hpp"
#include "scene/light.hpp"
#include "render/shading.hpp"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

// Floating-point approximate equality for geometry/shading checks.
[[maybe_unused]] static bool approx(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) < eps;
}

// Minimal ISceneQuery for shading tests: a flat object list + lookup tables.
struct MockScene : ISceneQuery {
    std::vector<const Hittable*> objs;
    std::vector<Material> mats;
    std::vector<Light> lts;
    Color bg = Color(0, 0, 0);

    bool hit(const Ray& r, double tmin, double tmax, HitRecord& rec) const override {
        bool found = false;
        double closest = tmax;
        HitRecord tmp;
        for (const Hittable* o : objs)
            if (o->hit(r, tmin, closest, tmp)) { found = true; closest = tmp.t; rec = tmp; }
        return found;
    }
    const Material& material(int id) const override { return mats[id]; }
    const std::vector<Light>& lights() const override { return lts; }
    Color background(const Ray&) const override { return bg; }
};

int main() {
    std::printf("Running core unit tests...\n");

    // === Member A (rendering core) tests ===
    {
        // -- vec3 algebra --
        CHECK(approx(dot(Vec3(1, 0, 0), Vec3(1, 0, 0)), 1.0));
        CHECK(approx(dot(Vec3(1, 0, 0), Vec3(0, 1, 0)), 0.0));
        Vec3 cx = cross(Vec3(1, 0, 0), Vec3(0, 1, 0));            // x × y = z
        CHECK(approx(cx.x, 0) && approx(cx.y, 0) && approx(cx.z, 1));
        CHECK(approx(normalized(Vec3(3, 0, 0)).length(), 1.0));
        Vec3 rfl = reflect(Vec3(1, -1, 0), Vec3(0, 1, 0));        // bounce off a floor
        CHECK(approx(rfl.x, 1) && approx(rfl.y, 1) && approx(rfl.z, 0));
        Vec3 mod = Vec3(0.5, 0.5, 0.5) * Vec3(0.4, 0.2, 1.0);     // color modulation
        CHECK(approx(mod.x, 0.2) && approx(mod.y, 0.1) && approx(mod.z, 0.5));

        // -- deterministic RNG (pixel-based seeding) --
        CHECK(seed_for(0, 10, 20, 3) == seed_for(0, 10, 20, 3)); // reproducible
        CHECK(seed_for(0, 10, 20, 3) != seed_for(1, 10, 20, 3)); // frame matters
        CHECK(seed_for(0, 10, 20, 3) != seed_for(0, 11, 20, 3)); // pixel matters
        RNG ra(seed_for(2, 5, 5, 0)), rb(seed_for(2, 5, 5, 0));
        CHECK(approx(ra.next(), rb.next()));                      // identical streams
        double rv = RNG(12345).next();
        CHECK(rv >= 0.0 && rv < 1.0);

        // -- ray/sphere: shoot -z at a unit sphere centered at (0,0,-5) --
        Sphere s(Vec3(0, 0, -5), 1.0, 0);
        HitRecord rec;
        CHECK(s.hit(Ray(Vec3(0, 0, 0), Vec3(0, 0, -1)), 1e-4, INF, rec));
        CHECK(approx(rec.t, 4.0));                                // near surface at z=-4
        CHECK(rec.front_face && approx(rec.normal.z, 1.0));       // normal faces camera
        CHECK(!s.hit(Ray(Vec3(0, 3, 0), Vec3(0, 0, -1)), 1e-4, INF, rec)); // miss above

        // -- ray/plane: floor at y=0, ray dropping from y=1 --
        Plane p(Vec3(0, 0, 0), Vec3(0, 1, 0), 1);
        HitRecord prec;
        CHECK(p.hit(Ray(Vec3(0, 1, 0), Vec3(0, -1, 0)), 1e-4, INF, prec));
        CHECK(approx(prec.t, 1.0));

        // -- camera: center ray of a -z-looking camera points down -z --
        Camera cam(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0), 60.0, 1.0);
        Ray center = cam.get_ray(0.5, 0.5);
        CHECK(approx(center.dir.x, 0, 1e-6) && approx(center.dir.z, -1, 1e-6));

        // -- ray/triangle (Möller–Trumbore): a triangle facing the camera at z=-2 --
        Triangle tri(Vec3(-1, -1, -2), Vec3(1, -1, -2), Vec3(0, 1, -2), 0);
        HitRecord trec;
        CHECK(tri.hit(Ray(Vec3(0, 0, 0), Vec3(0, 0, -1)), 1e-4, INF, trec));
        CHECK(approx(trec.t, 2.0));                                   // hits the face at z=-2
        CHECK(approx(trec.normal.z, 1.0, 1e-9));                      // normal faces the camera
        CHECK(!tri.hit(Ray(Vec3(5, 5, 0), Vec3(0, 0, -1)), 1e-4, INF, trec));  // misses outside

        // -- ray/box (AABB slab method): box centered at (0,0,-5), size 2x2x2 --
        Box bx(Vec3(0, 0, -5), Vec3(2, 2, 2), 0);
        HitRecord brec;
        CHECK(bx.hit(Ray(Vec3(0, 0, 0), Vec3(0, 0, -1)), 1e-4, INF, brec));
        CHECK(approx(brec.t, 4.0));                                       // front face at z=-4
        CHECK(brec.front_face && approx(brec.normal.z, 1.0));             // normal faces camera
        CHECK(!bx.hit(Ray(Vec3(5, 5, 0), Vec3(0, 0, -1)), 1e-4, INF, brec));  // miss
        // hit from inside
        CHECK(bx.hit(Ray(Vec3(0, 0, -5), Vec3(0, 0, -1)), 1e-4, INF, brec));
        CHECK(approx(brec.t, 1.0));                                       // exit face at z=-6
        CHECK(!brec.front_face);
        // side face hit
        CHECK(bx.hit(Ray(Vec3(5, 0, -5), Vec3(-1, 0, 0)), 1e-4, INF, brec));
        CHECK(approx(brec.t, 4.0));                                       // +x face at x=1
        CHECK(approx(brec.normal.x, 1.0));
    }
    // === Member B (shading) tests ===
    {
        RNG rng(7);
        Plane floor(Vec3(0, 0, 0), Vec3(0, 1, 0), 0);

        MockScene sc;
        sc.objs = { &floor };
        sc.mats = { Material::diffuse(Color(0.8, 0.8, 0.8)) };
        sc.lts  = { Light{ Vec3(5, 5, 0), Color(1, 1, 1), 0.0 } };
        sc.bg   = Color(0.1, 0.2, 0.3);

        // miss -> background (ray points up, the floor is below the origin)
        Color miss = shade(sc, Ray(Vec3(0, 1, 0), Vec3(0, 1, 0)), 0, 4, 1, rng);
        CHECK(approx(miss.x, 0.1) && approx(miss.y, 0.2) && approx(miss.z, 0.3));

        // floor point lit by an unobstructed side light at (5,5,0)
        Color lit = shade(sc, Ray(Vec3(0, 3, 0), Vec3(0, -1, 0)), 0, 4, 1, rng);

        // same point, but an occluder now sits on the floor->light segment
        Sphere occ(Vec3(2.5, 2.5, 0), 0.6, 0);
        MockScene scs;
        scs.objs = { &floor, &occ };
        scs.mats = sc.mats; scs.lts = sc.lts; scs.bg = sc.bg;
        Color shadow = shade(scs, Ray(Vec3(0, 3, 0), Vec3(0, -1, 0)), 0, 4, 1, rng);

        CHECK(lit.x > shadow.x + 0.05);   // clearly brighter when lit
        CHECK(shadow.x < 0.35);           // shadowed, but GI adds indirect sky light

        // GI: deeper recursion adds indirect light from background bounces
        RNG r1(1), r2(1);
        Color d0 = shade(sc, Ray(Vec3(0, 3, 0), Vec3(0, -1, 0)), 0, 0, 1, r1);
        Color d4 = shade(sc, Ray(Vec3(0, 3, 0), Vec3(0, -1, 0)), 0, 4, 1, r2);
        CHECK(d4.x >= d0.x - 0.01);      // deeper path tracing adds energy

        // emissive material returns its emission directly
        Sphere es(Vec3(0, 0, -3), 1.0, 0);
        MockScene se;
        se.objs = { &es };
        se.mats = { Material::emissive(Color(0.9, 0.1, 0.1)) };
        Color em = shade(se, Ray(Vec3(0, 0, 0), Vec3(0, 0, -1)), 0, 4, 1, rng);
        CHECK(approx(em.x, 0.9) && approx(em.y, 0.1) && approx(em.z, 0.1));

        // a perfect mirror facing empty space reflects the background color
        Sphere ms(Vec3(0, 0, -3), 1.0, 0);
        MockScene sm;
        sm.objs = { &ms };
        sm.mats = { Material::mirror(Color(1, 1, 1), 1.0) };
        sm.bg   = Color(0.2, 0.4, 0.6);
        Color refl = shade(sm, Ray(Vec3(0, 0, 0), Vec3(0, 0, -1)), 0, 4, 1, rng);
        CHECK(approx(refl.x, 0.2, 1e-6) && approx(refl.z, 0.6, 1e-6));

        // checker albedo alternates between adjacent unit cells
        Material chk = Material::checkerboard(Color(1, 1, 1), Color(0, 0, 0), 1.0);
        CHECK(effective_albedo(chk, Vec3(0.5, 0, 0.5)).x !=
              effective_albedo(chk, Vec3(1.5, 0, 0.5)).x);

        // Fresnel grows from head-on to grazing
        CHECK(fresnel_schlick(1.0, 0.04) < fresnel_schlick(0.02, 0.04));
        CHECK(approx(fresnel_schlick(1.0, 1.0), 1.0));   // a mirror is fully reflective head-on

        // Reinhard tone map compresses HDR into [0,1)
        Color tm = tone_map_reinhard(Color(10, 10, 10));
        CHECK(tm.x < 1.0 && tm.x > 0.9);

        // ACES filmic tone map: darker midtones but richer contrast
        Color ta = tone_map_aces(Color(1, 1, 1));
        CHECK(ta.x < 1.0 && ta.x > 0.7);
        Color ta2 = tone_map_aces(Color(0, 0, 0));
        CHECK(approx(ta2.x, 0.0, 0.01));

        // refraction (Snell): entering a denser medium bends the ray toward the
        // normal, so its horizontal component shrinks
        Vec3 in = normalized(Vec3(1, -1, 0));
        Vec3 refr;
        CHECK(refract(in, Vec3(0, 1, 0), 1.0 / 1.5, refr));      // air -> glass, not TIR
        CHECK(std::fabs(refr.x) < std::fabs(in.x));
        CHECK(approx(refr.length(), 1.0, 1e-9));                 // stays unit length
        // total internal reflection: glass -> air at a grazing angle has no exit ray
        Vec3 dummy;
        CHECK(!refract(normalized(Vec3(1, -0.1, 0)), Vec3(0, 1, 0), 1.5, dummy));

        // spotlight cone (smoothstep): full inside, zero outside, soft between
        CHECK(approx(smoothstep(0.90, 0.95, 0.99), 1.0));
        CHECK(approx(smoothstep(0.90, 0.95, 0.50), 0.0));
        double edge = smoothstep(0.90, 0.95, 0.925);
        CHECK(edge > 0.0 && edge < 1.0);

        // a downward spotlight lights the floor under it, not far to the side
        Plane fl(Vec3(0, 0, 0), Vec3(0, 1, 0), 0);
        MockScene sp;
        sp.objs = { &fl };
        sp.mats = { Material::diffuse(Color(0.8, 0.8, 0.8)) };
        sp.lts  = { Light::spot(Vec3(0, 5, 0), Color(1, 1, 1), Vec3(0, -1, 0), 12, 22) };
        Color under = shade(sp, Ray(Vec3(0, 3, 0),     Vec3(0, -1, 0)), 0, 4, 1, rng);
        Color aside = shade(sp, Ray(Vec3(9, 3, 0.001), Vec3(0, -1, 0)), 0, 4, 1, rng);
        CHECK(under.x > aside.x + 0.1);

        // Beer-Lambert: a longer path through tinted glass absorbs more
        Material cg = Material::colored_glass(1.5, Color(0.0, 0.4, 0.4));
        CHECK(cg.type == MatType::Dielectric && cg.absorption.y > 0.0);
        CHECK(std::exp(-cg.absorption.y * 4.0) < std::exp(-cg.absorption.y * 0.5));
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
