# Member A — Rendering Core & Math · Dev Journal

**Subsystem:** vector math, rays, colors, deterministic RNG, camera, object base,
ray–sphere / ray–plane intersection. The geometric and numeric foundation every
other member builds on.

**Files I own:** `src/core/vec3.hpp`, `ray.hpp`, `color.hpp`, `random.hpp`;
`src/scene/object.hpp`, `camera.hpp`, `sphere.hpp`, `plane.hpp`.

## Interface contract (what B, C, D rely on)

```cpp
struct Vec3 { double x, y, z; /* +,-,*scalar,/scalar, dot, cross, length, normalized, reflect, component-mul */ };
using Color = Vec3;
struct Ray  { Vec3 origin, dir; Vec3 at(double t) const; };

struct RNG { explicit RNG(uint64_t seed); double next(); Vec3 in_unit_disk(); Vec3 in_unit_sphere(); };
uint64_t seed_for(int frame, int x, int y, int sample);   // determinism: pixel-seeded, not rank-seeded

struct HitRecord { double t; Vec3 p, normal; int material_id; bool front_face; };
struct Hittable  { virtual bool hit(const Ray&, double tmin, double tmax, HitRecord&) const = 0; };

struct Camera { Camera(Vec3 eye, Vec3 lookat, Vec3 up, double vfov_deg, double aspect);
                Ray get_ray(double u, double v) const; };
struct Sphere : Hittable { Sphere(Vec3 center, double radius, int material_id); };
struct Plane  : Hittable { Plane(Vec3 point, Vec3 normal, int material_id); };
```

## Log
<!-- Entries appended as code lands. Format: Idea / What I did / Why this, not the alternative. -->
