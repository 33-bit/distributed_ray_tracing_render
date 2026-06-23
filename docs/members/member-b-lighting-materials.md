# Member B — Lighting, Materials & Shading · Dev Journal

**Subsystem:** surface materials, light sources, and the `shade()` function that
turns a ray–surface hit into a color — **path-traced global illumination** (indirect
diffuse bounce with cosine-weighted importance sampling + Russian Roulette), Phong
diffuse+specular, hard and soft shadow rays, recursive reflection with **rough
reflections** for glossy materials, **ACES filmic tone mapping**, gamma. The "how it
looks" layer.

**Files I own:** `src/scene/material.hpp`, `src/scene/light.hpp`,
`src/render/shading.hpp`.

## Interface contract (what C, D rely on)

```cpp
enum class MatType { Diffuse, Specular, Reflective, Emissive, Dielectric };
struct Material { MatType type; Color albedo; double specular; double shininess;
                  double reflectivity; double roughness; Color emission;
                  double ior; Color absorption; };
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
            int shadow_samples, RNG& rng);  // path tracer: direct + indirect GI + reflection
Color gamma_correct(Color c, double gamma = 2.2);
Color tone_map_aces(const Color& c);       // ACES filmic (replaces Reinhard as default)
Color tone_map_reinhard(const Color& c);   // still available for comparison
```

## Theory I should know (my part)

> Full version: `docs/PROJECT.md` Appendix A.1. My job: given a ray–surface hit
> (from Member A), compute its colour.

**Phong lighting** — colour = ambient + Σ over lights of (diffuse + specular):
- **Ambient** `ka·albedo` — a small constant so shadows aren't pure black.
- **Diffuse (Lambert)** `albedo · I · max(0, N·L)`. `N·L` is a cosine: a surface
  is brightest when it faces the light (`L` = direction to light), dark when edge-on.
- **Specular (Phong)** `ks · I · max(0, R·V)^shininess` — the shiny highlight.
  `R` is `L` mirrored about `N`, `V` points to the camera; the power tightens it.

**Shadows.** For each light, shoot a **shadow ray** from the hit toward the light;
if it hits something first, that light is blocked here. A **point light** → one
ray → hard edge. An **area light** → sample `S` points on it and average
visibility → soft **penumbra**. Averaging random samples is **Monte Carlo**
integration: more samples, smoother, slower.

**Reflection (mirror).** Bounce the ray about the normal, `R = D − 2(D·N)N`, trace
it recursively, blend it in by the material's reflectivity; a depth cap stops
infinite mirror-in-mirror recursion.

**Refraction (glass).** Light bends by **Snell's law** `n₁sinθ₁ = n₂sinθ₂`
(air n≈1, glass n≈1.5). If it's too steep to exit, that's **total internal
reflection**. How much reflects vs refracts is **Fresnel**, approximated by
**Schlick** `R0 + (1−R0)(1−cosθ)⁵` — more reflection at grazing angles, which is
why a glass rim looks bright. **Beer–Lambert** `e^(−σd)` darkens light by how far
`d` it travelled through coloured glass.

**Display fixes (applied by the renderer, but my helpers).** **Gamma** encodes
`c^(1/2.2)` because monitors are non-linear — so all the math above stays in
*linear* light and is corrected once at the end. **Reinhard tone mapping**
`c/(1+c)` squashes over-bright highlights into `[0,1)` instead of clipping.

**Why this matters for the parallel side:** all of the above is deterministic
(the soft-shadow randomness is seeded per pixel-sample by Member A), so my shader
gives identical output no matter which MPI rank runs it → MSE = 0.

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

### 2026-06-19 — Quality pass: checker, Fresnel, tone mapping

**Idea.** Three small additions that make the demo read better and the lighting
behave physically, all inside the materials/shading subsystem I already own.

**What I did.**
- `Material::checkerboard(a,b,scale)` + `effective_albedo()` — a world-space
  checker for the ground. Shadows on a checker floor are the canonical way to
  show off a ray tracer, and it gives the eye a scale reference as the camera
  orbits.
- `fresnel_schlick()` — replaced the constant reflection blend with an
  angle-dependent Schlick term, so reflective surfaces reflect more at grazing
  angles (realistic) while staying exact at head-on (a true mirror is still a
  mirror, so the existing mirror unit test still holds).
- `tone_map_reinhard()` — for the renderer to compress bright specular
  highlights into [0,1) before gamma, instead of a hard clamp that would flatten
  them to flat white.

**Why this, not the alternative.** These are genuine lighting/material features,
not filler: each one is visible in the final video and each is a standard
technique I can explain at the defense. I kept Fresnel gated behind
`reflectivity>0` so purely diffuse surfaces get no spurious grazing reflection.

**Tests now 28 total, 0 failures** (+4): checker alternation, Fresnel
monotonicity, mirror-at-head-on, Reinhard range.

### 2026-06-20 — Refraction / dielectric glass (`material.hpp`, `shading.hpp`)

**Idea.** Add a glass material — the natural next step for a "materials"
subsystem and the most recognisable ray-tracing effect. It also turns the demo
into a proper showcase (diffuse + mirror + glass + glossy) and, as a bonus, adds
the variable per-tile cost that makes the load-balancing experiment meaningful.

**What I did.** New `MatType::Dielectric` + `ior` field + `Material::dielectric()`.
A `refract()` helper (Snell's law, returns false on total internal reflection)
and `schlick_r0()`. In `shade()`, a dielectric spawns a Fresnel-weighted pair:
a reflected ray and (unless TIR) a refracted ray, blended by the Schlick term.
The center-right sphere in the demo is now clear glass.

**Why this, not the alternative.**
- *Deterministic Fresnel blend (reflect + refract weighted by Schlick), not a
  stochastic pick-one-by-random.* Stochastic refraction is cheaper per ray but
  needs many samples to denoise; a deterministic blend gives clean glass at low
  spp AND, crucially, draws no random numbers — so the seq-vs-MPI byte-identity
  survives (re-verified: MSE = 0 with glass + soft shadows).
- *Reused Member A's oriented normal.* Because `set_face_normal` already points
  the normal against the ray, the air→glass vs glass→air distinction collapses
  to one line: `eta = front_face ? 1/ior : ior`. No special-casing the inside.
- *Handle total internal reflection explicitly* (no transmitted ray ⇒ all
  reflected) instead of ignoring it, so steep internal angles look right.

This was scoped as "optional" in the proposal, but it is the cleanest way for my
subsystem to clear the 250-LOC bar with a genuine, defensible feature rather than
filler — and it visibly improved both the render and the experiments.

**Tests now 32 total, 0 failures** (+4): Snell bends toward the normal entering
glass, refracted ray stays unit-length, and grazing glass→air is total internal
reflection.

### 2026-06-20 — Spotlight, distance attenuation, Beer-Lambert glass

**Idea.** Round out the lighting/material model with the three effects that most
change how a scene reads: a focused spotlight, plausible distance falloff, and
colored (absorbing) glass.

**What I did.**
- `Light` gained a spotlight cone (smooth `smoothstep` edge between `cos_inner`
  and `cos_outer`) and optional `1/(1+k·d²)` distance attenuation, plus named
  factories (`point` / `area` / `spot`, `with_attenuation`).
- `Material` gained an `absorption` colour + `colored_glass()`; the dielectric
  path now applies Beer-Lambert `exp(-absorption · path)` to light traversing the
  glass.
- Demo: a cool cyan spotlight casts a tinted pool of light on the floor.

**Why this, not the alternative.**
- *One `Light` struct with flags, not a class hierarchy* — keeps it a POD that
  Member C can still ship as a flat block of numbers.
- *`smoothstep` cone edge, not a hard cutoff* — avoids an aliased rim on the spot.
- *Beer-Lambert keyed on the back-face hit distance (`rec.t`)* — that is exactly
  how far the ray travelled inside the glass; clear glass (absorption 0) is
  unaffected, so the seq-vs-MPI determinism baseline is untouched.

**Tests: +6** (smoothstep ramp; spotlight lights under-but-not-aside; Beer
falloff). Shading stays deterministic → seq vs MPI MSE=0. Member B main code now
~251 LOC.

