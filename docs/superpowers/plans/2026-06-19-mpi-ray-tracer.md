# MPI Distributed Ray Tracer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Build a C++17 + MPI distributed ray tracer that renders an animated, shadowed, reflective 3D scene as tiles distributed by a dynamic master-worker model, assembles a video, and produces correctness + speedup benchmarks.

**Architecture:** MPI-agnostic renderer core (vec/ray/camera/intersection/shading) + a thin MPI layer that distributes 2D image tiles. Single codebase, two targets: `make seq` (clang++, baseline) and `make mpi` (mpic++). Per-pixel-sample deterministic seeding makes seq and mpi outputs identical.

**Tech Stack:** C++17, Open MPI 5.0.9, Apple clang 17, ffmpeg 8.1.2, Python (matplotlib) for charts, Make.

## Global Constraints

- C++17, header-only modules under `src/`, one `main.cpp`. Compiles `-Wall -O2`.
- MPI code isolated in `src/mpi/`, guarded by `#ifdef USE_MPI`; the `seq` target never includes MPI headers.
- Each of 4 members owns ≥ 250 LOC. Ownership per `docs/superpowers/specs/2026-06-19-mpi-ray-tracer-design.md` §3 — no file has two owners.
- Determinism: `seed = hash(frame, x, y, sample)`. Sequential and MPI frames must match (MSE < 1e-6 on identical params).
- Image format PPM (P6). Video via ffmpeg. No external C++ libs beyond MPI + STL.
- Every member's code lands together with a journal entry in `docs/members/` (Idea → What I did → Why-not-alternative).

---

### Task 1: Project skeleton, build system, test harness

**Files:**
- Create: `Makefile`, `README.md`, `src/test_core.cpp` (assert-based test driver), `tools/.keep`
- Create: `docs/members/member-a-rendering-core.md`, `member-b-lighting-materials.md`, `member-c-mpi-scheduling.md`, `member-d-integration-benchmark.md` (journal headers)

**Interfaces:**
- Produces: `make seq`, `make mpi`, `make test`, `make clean` targets.

- [ ] Makefile with `seq` (clang++), `mpi` (mpic++ -DUSE_MPI), `test`, `clean`. `-std=c++17 -Wall -O2 -Isrc`.
- [ ] `test_core.cpp` minimal `CHECK(cond)` macro that counts failures and exits nonzero.
- [ ] Journal files seeded with member name + subsystem + interface contract.
- [ ] Verify: `make test` builds and runs (0 tests pass initially is fine).
- [ ] Commit: "chore: project skeleton, makefile, test harness".

---

### Task 2 (Member A): Rendering core & math

**Files:** Create `src/core/vec3.hpp`, `ray.hpp`, `color.hpp`, `random.hpp`; `src/scene/object.hpp`, `camera.hpp`, `sphere.hpp`, `plane.hpp`. Add asserts to `src/test_core.cpp`.

**Interfaces — Produces:**
- `struct Vec3 { double x,y,z; }` with `+ - *(scalar) /(scalar)`, `dot`, `cross`, `length`, `normalized`, `reflect(v,n)`, component mul.
- `struct Ray { Vec3 origin, dir; Vec3 at(double t); }`
- `using Color = Vec3;`
- `struct RNG { RNG(uint64_t seed); double next(); Vec3 in_unit_sphere(); }` — splitmix64/xorshift, pure function of seed.
- `uint64_t seed_for(int frame,int x,int y,int sample);`
- `struct HitRecord { double t; Vec3 p, normal; int material_id; bool front_face; };`
- `struct Hittable { virtual bool hit(const Ray&, double tmin, double tmax, HitRecord&) const = 0; };`
- `struct Camera { Camera(eye,lookat,up,vfov,aspect); Ray get_ray(double u,double v) const; }`
- `struct Sphere : Hittable` (center, radius, material_id) and `struct Plane : Hittable` (point, normal, material_id).

- [ ] Implement vec3 with all ops.
- [ ] Tests: `dot((1,0,0),(1,0,0))==1`; `cross(x,y)==z`; `normalized` length≈1; `reflect` off flat normal flips component; sphere hit at known t; plane hit; RNG determinism (`seed_for` same args → same stream); camera center ray direction.
- [ ] Verify: `make test` all green.
- [ ] Journal entry A. Commit: "feat(core): vec3, ray, rng, camera, sphere/plane intersection (Member A)".

---

### Task 3 (Member B): Lighting, materials, shading

**Files:** Create `src/scene/material.hpp`, `light.hpp`, `src/render/shading.hpp`. Add shading asserts to test driver. Needs a minimal `Scene` view — define a small `SceneView` interface that `hit()`s and exposes materials+lights so B does not depend on D's full container.

**Interfaces — Consumes:** A's `Vec3`, `Ray`, `HitRecord`, `RNG`.
**Produces:**
- `enum class MatType { Diffuse, Specular, Reflective, Emissive };`
- `struct Material { MatType type; Color albedo; double specular; double shininess; double reflectivity; Color emission; };`
- `struct Light { Vec3 position; Color intensity; double radius; /*0=point,>0=area*/ };`
- `struct ISceneQuery { virtual bool hit(const Ray&,double,double,HitRecord&) const=0; virtual const Material& material(int id) const=0; virtual const std::vector<Light>& lights() const=0; virtual Color background(const Ray&) const=0; };`
- `Color shade(const ISceneQuery& s, const Ray& r, int depth, int max_depth, int shadow_samples, RNG& rng);`

- [ ] Phong: ambient + diffuse(`max(0,n·l)`) + specular(`pow(max(0,r·v),shininess)`); shadow ray to each light (occluded → no contribution); soft shadow = average over `shadow_samples` jittered points on light disk; reflective → recurse `shade` with reflected ray until `max_depth`; emissive returns emission; miss returns background.
- [ ] Gamma helper `Color gamma_correct(Color, double g=2.2)`.
- [ ] Tests: point fully lit vs fully shadowed (black); reflectivity=0 ignores recursion; depth cap terminates; emissive returns emission.
- [ ] Verify `make test`. Journal entry B. Commit: "feat(shading): materials, lights, phong + shadows + reflection (Member B)".

---

### Task 4 (Member D): Scene container, renderer, image, main → ONE frame

**Files:** Create `src/scene/scene.hpp` (implements `ISceneQuery`), `src/render/image.hpp` (PPM P6), `src/render/tile.hpp`, `src/render/renderer.hpp`, `src/core/timer.hpp`, `src/main.cpp`.

**Interfaces — Consumes:** A (camera/objects), B (`shade`, `ISceneQuery`).
**Produces:**
- `struct Scene : ISceneQuery { std::vector<unique_ptr<Hittable>> objects; std::vector<Material> materials; std::vector<Light> lights_; Camera camera; Color bg_top,bg_bottom; ... }`
- `Scene build_demo_scene(double aspect, int frame, int total_frames);` (static for now)
- `struct Tile { int x0,y0,x1,y1; };`
- `struct Image { int w,h; vector<Color>; void set(x,y,c); void write_ppm(path); }`
- `struct RenderParams { int width,height,spp,max_depth,shadow_samples,frame,total_frames; };`
- `void render_tile(const Scene&, const RenderParams&, const Tile&, Image& out);` — AA by `spp` jittered samples per pixel, gamma on write.
- `class Timer { void start(); double elapsed_s(); }`

- [ ] CLI: `--width --height --spp --depth --shadow-samples --frames --tile --out`. Default a small frame.
- [ ] Sequential path: build scene, render all tiles, write `frames/frame_0000.ppm`.
- [ ] Verify: `make seq && ./raytracer_seq --width 200 --height 150 --frames 1` → PPM exists, valid P6 header, contains non-background pixels (sphere visible), shadow under sphere darker than lit ground. Inspect by converting to PNG.
- [ ] Journal entry D. Commit: "feat(render): scene, renderer, PPM image, CLI — first frame (Member D)".

---

### Task 5 (Member D): Animation → multi-frame sequence

**Files:** Modify `src/scene/scene.hpp` (`build_demo_scene` uses `frame/total_frames` to orbit camera + move light), `src/main.cpp` (frame loop).

**Interfaces — Produces:** camera orbit `angle = 2π·frame/total`; light position on a moving path.

- [ ] Loop frames 0..F-1, render each, write `frame_%04d.ppm`.
- [ ] Verify: `./raytracer_seq --frames 12 --width 200 --height 150` → 12 PPMs; shadow direction differs between frame 0 and frame 6.
- [ ] Journal entry D (animation). Commit: "feat(anim): orbiting camera + moving light, multi-frame (Member D)".

---

### Task 6 (Member C): MPI tiles, serializer, master/worker → distributed run

**Files:** Create `src/mpi/mpi_tags.hpp`, `mpi_serializer.hpp`, `mpi_master.hpp`, `mpi_worker.hpp`. Modify `src/main.cpp` (`#ifdef USE_MPI` → MPI_Init, rank 0 master else worker).

**Interfaces — Consumes:** D's `Scene`, `RenderParams`, `render_tile`, `Image`.
**Produces:**
- tags `TAG_TASK, TAG_RESULT, TAG_REQUEST, TAG_STOP`.
- `vector<double> serialize_scene(...)` / `Scene deserialize_scene(...)`, tile pack/unpack of pixel buffers.
- `void run_master(params, total_frames, schedule_mode)` — generate tiles per frame, dynamic dispatch via `ANY_SOURCE` request/result, assemble, write frames, gather timing.
- `void run_worker()` — recv scene, loop: request→recv tile→render→send pixels.
- static mode: `owner = task_id % (P-1)`.

- [ ] Bcast params + scene to all ranks.
- [ ] Dynamic loop: master seeds one task per worker, then on each result sends next task; sends `TAG_STOP` when drained.
- [ ] Verify determinism: `./raytracer_seq ... --out frames_seq` vs `mpirun -np 4 ./raytracer_mpi ... --out frames_mpi`; run `tools/compare_frames.py` → MSE < 1e-6.
- [ ] Journal entry C. Commit: "feat(mpi): master-worker dynamic tile scheduling + serializer (Member C)".

---

### Task 7 (Member C+D): Benchmark, timing, static-vs-dynamic, correctness tool

**Files:** Create `src/benchmark/benchmark.hpp`, `csv_logger.hpp`, `tools/compare_frames.py`. Modify master/worker to record comp/comm/idle time; `main.cpp` `--schedule static|dynamic --bench out.csv`.

**Interfaces — Produces:** per-rank `{comp_s, comm_s, idle_s, tiles_done}` via `MPI_Gather`; CSV columns `proc,comp_s,comm_s,idle_s,tiles,total_s`.

- [ ] Workers time render (comp) vs send/recv (comm); master times idle waiting.
- [ ] `MPI_Reduce`/`Gather` stats to rank 0 → CSV.
- [ ] `compare_frames.py`: MSE, max abs diff, mean diff over a frame dir pair.
- [ ] Verify: run dynamic vs static, CSV has all ranks; dynamic idle-spread < static.
- [ ] Journal entries C + D. Commit: "feat(bench): per-rank timing, CSV, static/dynamic, correctness tool".

---

### Task 8 (Member D): Video + experiment runner + charts

**Files:** Create `tools/assemble_video.sh` (ffmpeg PPM→mp4), `tools/run_experiments.sh` (sweep P and tile size → CSVs), `tools/make_charts.py` (speedup, efficiency, granularity stacked bar, runtime w/ vs w/o comm).

- [ ] `assemble_video.sh frames/ output/render.mp4` via ffmpeg.
- [ ] `run_experiments.sh`: speedup sweep `-np 1,2,4,8`; granularity sweep tile `16,32,64,128`; write CSVs to `output/`.
- [ ] `make_charts.py`: read CSVs → PNG charts.
- [ ] Verify: produce `output/render.mp4` and at least the speedup chart from a real run.
- [ ] Journal entry D. Commit: "feat(tools): video assembly, experiment runner, charts (Member D)".

---

### Task 9: Finalize PROJECT.md, journals, README

**Files:** Create `docs/PROJECT.md` (full explainer + diagrams + per-member summary + results). Finalize all 4 journals with a closing reflection. Finalize `README.md` (build/run quickstart).

- [ ] PROJECT.md: overview, parallel model, architecture diagram (ASCII), data flow, build/run, experiment results with real numbers, per-member contribution table + LOC counts.
- [ ] Verify: LOC count per member ≥ 250 (`cloc`/`wc`); links resolve.
- [ ] Commit: "docs: full PROJECT.md, member journals, README".

---

## Self-Review

- **Spec coverage:** §2 build→T1; §3 ownership→T2-T8; §4 features→T2/T3/T5; §5 determinism→T6 MSE gate; §6 parallel model→T6/T7; §7 milestones→T1-T8 1:1; §8 docs→T1 seed + T9 finalize. ✓ No gaps.
- **Placeholder scan:** verification steps are concrete commands with thresholds; interfaces give exact signatures. Full per-line code is produced at execution (same-session inline), not duplicated here. ✓
- **Type consistency:** `shade(ISceneQuery&,Ray,depth,max_depth,shadow_samples,RNG&)` used identically in T3/T4; `render_tile(Scene,RenderParams,Tile,Image&)` consistent T4/T6; `seed_for(frame,x,y,sample)` consistent T2/determinism. ✓
