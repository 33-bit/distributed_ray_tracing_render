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
        CHECK(shadow.x < 0.1);            // shadowed point -> ~ambient only

        // reflectivity == 0 => recursion depth must not change the result
        RNG r1(1), r2(1);
        Color d0 = shade(sc, Ray(Vec3(0, 3, 0), Vec3(0, -1, 0)), 0, 0, 1, r1);
        Color d4 = shade(sc, Ray(Vec3(0, 3, 0), Vec3(0, -1, 0)), 0, 4, 1, r2);
        CHECK(approx(d0.x, d4.x) && approx(d0.y, d4.y));

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
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
