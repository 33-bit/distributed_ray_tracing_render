# Member B — Lighting, Materials & Shading · Dev Journal

**Subsystem:** surface materials, light sources, and the `shade()` function that
turns a ray–surface hit into a color — Phong diffuse+specular, hard and soft
shadow rays, recursive reflection, gamma. The "how it looks" layer.

**Files I own:** `src/scene/material.hpp`, `src/scene/light.hpp`,
`src/render/shading.hpp`.

## Interface contract (what C, D rely on)

```cpp
enum class MatType { Diffuse, Specular, Reflective, Emissive };
struct Material { MatType type; Color albedo; double specular; double shininess;
                  double reflectivity; Color emission; };
struct Light    { Vec3 position; Color intensity; double radius; }; // radius 0 = point, >0 = area (soft)

// I depend on A's geometry but NOT on D's concrete Scene — only this query view:
struct ISceneQuery {
    virtual bool hit(const Ray&, double tmin, double tmax, HitRecord&) const = 0;
    virtual const Material& material(int id) const = 0;
    virtual const std::vector<Light>& lights() const = 0;
    virtual Color background(const Ray&) const = 0;
    virtual ~ISceneQuery() = default;
};

Color shade(const ISceneQuery& scene, const Ray& r, int depth, int max_depth,
            int shadow_samples, RNG& rng);
Color gamma_correct(Color c, double gamma = 2.2);
```

## Log
<!-- Entries appended as code lands. Format: Idea / What I did / Why this, not the alternative. -->

### 2026-06-19 — Materials & lights (`material.hpp`, `light.hpp`)

**Idea.** Keep the surface/light data tiny and POD-like so Member C can ship it
over MPI as plain numbers, while still covering diffuse, glossy, mirror, and
emissive looks.

**What I did.** `Material{type, albedo, specular, shininess, reflectivity,
emission}` with named constructors (`diffuse/glossy/mirror/emissive`). `Light`
is one struct where `radius==0` means point light and `radius>0` means a
spherical area light.

**Why this, not the alternative.**
- *One `Material` struct with a `type` tag, not a class hierarchy of
  `DiffuseMaterial`/`MirrorMaterial`.* A polymorphic material would need virtual
  dispatch and custom serialization per subclass — painful to `MPI_Send`. A flat
  POD bag serializes as a fixed run of doubles (Member C just memcpy-style packs
  it) and the `type` tag picks the branch.
- *One `Light` struct, radius switches hard/soft*, instead of separate
  `PointLight`/`AreaLight` types — same serialization argument, and the shader
  branch is a one-liner.

### 2026-06-19 — The shader (`shading.hpp`) — decoupled from the concrete Scene

**Idea.** `shade()` should not know how the scene is stored or that MPI exists.
It should only need: "can you intersect this ray, and what material/lights/
background apply?"

**What I did.** Defined `ISceneQuery` (pure virtual: `hit`, `material`,
`lights`, `background`) and wrote `shade(scene, ray, depth, max_depth,
shadow_samples, rng)` against it. The shader does ambient + Lambertian diffuse +
Phong specular, casts shadow rays per light, recurses for reflection, and is
gamma-agnostic.

**Why this, not the alternative.**
- *`ISceneQuery` interface instead of `#include "scene/scene.hpp"`.* This is the
  cleanest cut in the whole project: the identical `shade()` runs in the
  sequential renderer and inside every MPI worker, and B can be unit-tested with
  a 15-line `MockScene` (see `test_core.cpp`) without pulling in D's code. It
  also breaks what would otherwise be a circular include (Scene needs shading,
  shading would need Scene).
- *Phong, not Blinn-Phong or a PBR/microfacet BRDF.* The proposal asks for
  "diffuse + specular," not photorealism. Phong is the textbook model, cheap, and
  easy to defend orally. PBR would add cost and complexity for no grading benefit
  (advanced graphics is explicitly deprioritized vs the MPI work).
- *Soft shadows by sampling a sphere around the light* (`L.position + radius *
  rng.in_unit_sphere()`), not a properly oriented disk. A disk needs a local
  frame facing the shaded point; a sphere sample is orientation-free, one line,
  and visually indistinguishable for a small light. The per-sample visibility is
  averaged, so partial occlusion yields a real penumbra — and it deliberately
  makes some tiles cost more than others, which feeds Member C's load-balancing
  experiment.
- *Reflection blended with `lerp(local, reflected, reflectivity)`, not added.*
  Adding can blow past 1.0 and needs clamping/energy bookkeeping; a lerp keeps a
  mirror in [0,1] and reads naturally ("reflectivity = how much of the reflected
  image shows through").
- *Gamma in `gamma_correct()` called by the renderer at write time, not inside
  `shade()`.* Reflection recursion must accumulate in linear light; gamma-ing at
  every bounce would double-correct. The renderer applies it once per pixel.
- *Small flat ambient (`albedo*0.08`).* Pure-black shadows look broken and make
  the shadow region invisible in the demo; a touch of ambient keeps detail.

**Tests (`make test`): 24 total, 0 failures** (+6 for B) — miss→background,
lit ≫ shadowed, shadow≈ambient, depth-irrelevant when non-reflective, emissive
passthrough, mirror reflects background exactly.
