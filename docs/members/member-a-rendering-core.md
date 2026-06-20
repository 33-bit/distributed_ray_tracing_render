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

## Theory I should know (my part)

> Full version with diagrams: `docs/PROJECT.md` Appendix A.1–A.2. Here is the
> minimum to explain my subsystem at the viva.

**Vectors are the alphabet.** Everything is 3-D vectors. Two operations carry the
whole project:
- **Dot product** `a·b = ax·bx + ay·by + az·bz = |a||b|cosθ`. It measures
  *alignment* — I use it for "how much does this surface face the light?" and to
  project one vector onto another.
- **Cross product** `a×b` gives a vector **perpendicular** to both — I use it to
  build the camera's axes and a triangle's normal.
- `normalize(v) = v/|v|` makes a unit-length direction.

**A ray** is `P(t) = O + t·D` (origin + t·direction, `t ≥ 0`). Rendering a pixel =
find the smallest `t > 0` where the ray meets a surface.

**Ray–sphere** is a quadratic. Substituting the ray into `|X−C|² = r²` gives
`a t² + b t + c = 0`; the **discriminant** `b²−4ac` decides miss (`<0`) vs hit,
and the smaller positive root is the nearest surface. (I use the "half-b" form to
drop the 4s.)

**Ray–plane** is one division: `t = ((Q−O)·N)/(D·N)`; if `D·N≈0` the ray is
parallel and misses.

**Ray–triangle** (Möller–Trumbore) writes the hit point in **barycentric**
coordinates `(1−u−v)V0 + uV1 + vV2`; `u,v ≥ 0` and `u+v ≤ 1` means "inside". It
returns `t,u,v` from one 3×3 solve — no separate plane test.

**The normal `N`** is the unit perpendicular at the hit; it's what the lighting
(Member B) reads. Sphere: `(P−C)/r`. Triangle: `normalize(e1×e2)`.

**The camera** is a pinhole: from eye/look-at/up I build an orthonormal basis and
a virtual "viewport" rectangle; `get_ray(u,v)` shoots a ray from the eye through
screen position `(u,v)∈[0,1]²`.

**The RNG and determinism.** A pseudo-random generator turns a **seed** into a
repeatable number stream. The crucial design choice (`seed_for(frame,x,y,sample)`)
is to seed by *where and when* a sample is — never by which process computes it —
so the renderer is a **pure function** and the MPI result equals the sequential
one bit-for-bit. This is the foundation Member C's correctness proof stands on.

## Log
<!-- Entries appended as code lands. Format: Idea / What I did / Why this, not the alternative. -->

### 2026-06-19 — Vector math + ray (`vec3.hpp`, `ray.hpp`, `color.hpp`)

**Idea.** Everything downstream is built on a 3-component vector, so nail it
first with a clean, fully-inlined value type.

**What I did.** `Vec3{x,y,z}` with the full operator set (`+ - * /`, `dot`,
`cross`, `length`, `normalized`, `reflect`, `near_zero`). `Ray{origin,dir}` with
`at(t)`. `using Color = Vec3` and a component-wise `operator*` so the shader can
write `albedo * light_color` as one multiply.

**Why this, not the alternative.**
- *`double` not `float`.* On an 8-core demo we are not FP-throughput bound, and
  `double` removes the self-intersection / shadow-acne precision problems that
  `float` causes at grazing angles. The proposal already expects tiny FP
  differences in the seq-vs-mpi check — I would rather those come from sum order,
  not from `float` rounding.
- *Color = Vec3, not a separate struct.* DRY. A dedicated `Color` would duplicate
  arithmetic for zero benefit; reusing `Vec3` makes color modulation free.
- *Header-only.* Matches the proposal's module layout (§12) and lets the
  compiler inline the hot vector ops, which dominate ray tracing.

### 2026-06-19 — Deterministic RNG (`random.hpp`) — the correctness keystone

**Idea.** The whole seq-vs-mpi correctness story (Task 6) hinges on one rule:
a pixel's random samples must not depend on *which* process draws them.

**What I did.** `seed_for(frame,x,y,sample)` hashes the sample's coordinates (a
boost-style `hash_combine`) into a 64-bit seed; `RNG` is `splitmix64` seeded
from it. So sample (frame=3, x=100, y=50, s=2) produces the same jitter whether
the sequential renderer or MPI rank 7 computes it.

**Why this, not the alternative.**
- *Pixel-seeded, not `std::mt19937` seeded per process.* A per-process or
  time-based seed would make every parallel run differ from the baseline and
  destroy the MSE≈0 check. Pixel seeding makes the renderer a pure function of
  (scene, params) — reproducible and trivially parallel.
- *splitmix64, not `mt19937`.* mt19937 has a large state (2.5 KB) that would be
  expensive to re-seed per sample; splitmix64 is one `uint64` of state, seeds
  instantly, and has more than enough quality for AA/soft-shadow sampling.

### 2026-06-19 — Intersections + camera (`object.hpp`, `sphere.hpp`, `plane.hpp`, `camera.hpp`)

**Idea.** Provide the two primitives the demo scene needs (sphere, ground plane)
behind one `Hittable::hit()` interface, plus a pinhole camera.

**What I did.** `HitRecord` carries `t,p,normal,material_id,front_face`;
`set_face_normal()` flips the normal to always face the incoming ray. Sphere uses
the `half_b` quadratic form and tries the near root then the far root. Plane is a
single dot-product solve with a parallel-ray guard. Camera builds an orthonormal
`(u,v,w)` basis from eye/lookat/up and maps screen `(u,v)∈[0,1]²` to rays.

**Why this, not the alternative.**
- *`Hittable` virtual interface.* Lets `Scene` hold a heterogeneous list and lets
  Member C serialize objects by type tag without knowing their math. Clean
  A↔C↔D boundary.
- *Oriented normals via `set_face_normal`.* B can shade without re-deriving which
  side it hit, and it leaves the door open for refraction (deferred) without an
  interface change.
- *Outward normal stored against the ray*, with `tmin` passed by the caller
  (we use `1e-4`) — standard epsilon to avoid shadow-ray self-hits.

**Tests (`make test`): 18 checks, 0 failures** — dot/cross/normalize/reflect,
RNG determinism, sphere hit `t=4` with camera-facing normal, plane hit `t=1`,
camera center ray ≈ `(0,0,-1)`.

### 2026-06-20 — Triangle primitive (`triangle.hpp`)

**Idea.** Add a third intersectable so the renderer isn't limited to spheres and
planes, and to give the geometry layer the building block for triangle meshes.

**What I did.** `Triangle : Hittable` via the Möller–Trumbore algorithm — solve
the ray/triangle test directly in barycentric coordinates `(u,v)` with no
precomputed plane. Double-sided. Wired a 4-triangle pyramid into the demo scene.

**Why this, not the alternative.**
- *Möller–Trumbore, not plane-hit-then-inside-test.* It is the standard,
  branch-light formulation and yields the barycentric coords for free (useful
  later for interpolated normals / texture coords).
- *Double-sided (no back-face cull)* so a lone triangle is visible from either
  side — fewer surprises in a hand-built scene.

**Tests: +4** (hit at `t=2` with a camera-facing normal; miss outside the
triangle). Member A main code now ~273 LOC.
