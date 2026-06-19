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
