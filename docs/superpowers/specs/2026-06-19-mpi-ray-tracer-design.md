# Design Spec — MPI-Based Distributed Ray Tracing Renderer

**Date:** 2026-06-19
**Course:** IT4130E — Parallel and Distributed Programming
**Team size:** 4 members
**Status:** Approved

---

## 1. Goal

Implement a distributed ray tracing renderer in **C++17 + MPI** that renders an
animated 3D scene (shadows, reflections, anti-aliasing, moving light, rotating
camera) as a sequence of image frames, distributes the per-frame tile workload
across an MPI cluster using a dynamic master-worker model, assembles the frames
into a video, and produces benchmark data (runtime, communication cost,
granularity, speedup, efficiency) plus a correctness comparison against a
sequential baseline.

The deliverable is positioned as a **distributed rendering system**, not a
graphics showcase. Correctness and measurable parallel performance come first;
visual richness second.

## 2. Build Strategy

One codebase, two build targets from a single `Makefile`:

- `make seq` → plain `clang++`, produces `raytracer_seq`. Fully runnable with no
  MPI. This is the **correctness baseline**.
- `make mpi` → `mpic++`, produces `raytracer_mpi`. Adds the master-worker layer.

The renderer core is MPI-agnostic. MPI code is isolated under `src/mpi/` and only
compiled into the `mpi` target (guarded by `#ifdef USE_MPI`). This lets the
sequential renderer be developed and validated before any distribution exists —
the proposal's core planning rule ("first make one frame render correctly, then
parallelize").

Toolchain (verified present): Open MPI 5.0.9, ffmpeg 8.1.2, Apple clang 17,
8 physical cores.

## 3. Ownership Split (4 members, each ≥ 250 LOC)

Boundaries chosen so each member owns a subsystem with a small, well-defined
interface, so members do not block each other, and so each part is independently
explainable at the oral defense.

### Member A — Rendering Core & Math
Files: `core/vec3.hpp`, `core/ray.hpp`, `core/color.hpp`, `core/random.hpp`,
`scene/camera.hpp`, `scene/object.hpp` (`Hittable` base + `HitRecord`),
`scene/sphere.hpp`, `scene/plane.hpp` (geometry **and** `hit()` math — owned whole).
Exposes: `Vec3` algebra, `Ray`, `Color`, deterministic `RNG` (pixel-seeded),
`Camera::get_ray(u,v)`, `HitRecord`, ray-sphere and ray-plane intersection.

### Member B — Lighting, Materials & Shading
Files: `scene/material.hpp`, `scene/light.hpp`, `render/shading.hpp`.
Exposes: `Material` (diffuse / specular / reflective / emissive), `Light`
(point + area), and `shade(scene, hit, ray, depth, rng) -> Color` implementing
Phong diffuse+specular, hard + soft shadow rays, recursive reflection, gamma.

### Member C — MPI Scheduling & Communication
Files: `mpi/mpi_tags.hpp`, `mpi/mpi_master.hpp`, `mpi/mpi_worker.hpp`,
`mpi/mpi_serializer.hpp`.
Exposes: `run_master()`, `run_worker()`, scene/tile (de)serialization, both
**dynamic** master-worker scheduling and a **static** `task_id % P` baseline for
comparison. Uses `MPI_Bcast`, `MPI_Send/Recv`, `MPI_ANY_SOURCE`, `MPI_Reduce`.

### Member D — Scene, Renderer Integration, Benchmark, Animation, Video
Files: `scene/scene.hpp` (object/light container + animation), `render/renderer.hpp`,
`render/tile.hpp`, `render/image.hpp`, `core/timer.hpp`, `benchmark/benchmark.hpp`,
`benchmark/csv_logger.hpp`, `main.cpp`, `tools/*`.
Exposes: `Scene` container, `Renderer::render_tile()` / `render_pixel()` (the glue
that calls A's camera+intersection and B's shade), PPM `Image` I/O, `Timer`,
CSV logging, animated camera path + moving light, CLI arg parsing, and the
ffmpeg/charts/correctness scripts.

### Interaction
`D.Renderer` drives a tile → calls `A.Camera::get_ray` + `A.{Sphere,Plane}::hit`
→ calls `B.shade` → `C` distributes tiles across ranks and gathers results →
`D` assembles frames, times, and charts.

## 4. Features

Must-have: ray-sphere/plane intersection, diffuse + specular lighting, hard
shadows, recursive reflection, anti-aliasing, animated camera, dynamic MPI
scheduling, video output, sequential-vs-parallel correctness check, benchmark
charts.

Included nice-to-have: soft shadows (area-light sampling), gamma correction,
multiple materials, moving light, static-vs-dynamic scheduling comparison.

Explicitly deferred (proposal §13): BVH, refraction, textures, mesh loading,
non-blocking MPI.

## 5. Determinism / Correctness

Random sampling is seeded **per pixel-sample**, not per process:
`seed = hash(frame_id, pixel_x, pixel_y, sample_id)`. A pixel therefore produces
identical samples regardless of which rank renders it, so the sequential and MPI
outputs match within floating-point tolerance. Validation: MSE, max abs pixel
diff, mean diff between `seq` and `mpi` frames.

## 6. Parallel Model

- **Decomposition:** hybrid. Data decomposition (frame → 2D tiles) with task-based
  runtime assignment (each tile = one task).
- **Mapping:** dynamic master-worker. Rank 0 = master (generates tasks, assembles
  frames, writes images, collects timing). Ranks 1..P-1 = workers (request tile,
  render, return pixels).
- **Communication:** star topology. `MPI_Bcast` scene config; `Send/Recv` +
  `ANY_SOURCE` for tile request/result; `Reduce` for timing stats.
- **Load balancing:** workers pull new tiles when idle → faster workers do more.
  Tile size is the key granularity knob (16/32/64/128).

## 7. Milestones (code + per-member journals grow together)

1. Skeleton + Makefile + docs scaffolding.
2. **A** — vec3/ray/color/random/camera + intersection math.
3. **B** — material/light/shading.
4. **D** — scene + renderer + image + main → render ONE frame, verify PPM.
5. **D** — animation (camera path + moving light) → multi-frame sequence.
6. **C** — tile + MPI master/worker/serializer → distributed run; verify MSE≈0 vs seq.
7. **C+D** — benchmark timing, CSV, static-vs-dynamic, correctness report.
8. **D** — ffmpeg video + experiment runner + speedup/granularity charts.

## 8. Documentation Deliverables

- `docs/PROJECT.md` — full explainer: idea, parallel model, architecture,
  diagrams, build/run instructions, per-member summary, experiment results.
- `docs/members/member-{a,b,c,d}-*.md` — four dev journals. Each entry follows
  **Idea → What I did → Why this and not the alternative**, appended as that
  member's code lands, mirroring a real group workflow.
- This spec.

## 9. Out of Scope / Risks

- Google Drive folder + git: local git only; `frames/`, `output/`, binaries are
  gitignored.
- MPI on a single 8-core machine simulates the cluster via `mpirun -np N`
  (oversubscribe allowed); real multi-machine hostfile noted in docs but not
  required for grading.
- Soft shadows increase per-tile cost variance — good for the load-balance
  experiment, intentionally kept.
