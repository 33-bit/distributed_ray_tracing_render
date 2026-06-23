# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
make seq          # Sequential baseline -> ./raytracer_seq
make mpi          # MPI+OpenMP distributed build -> ./raytracer_mpi (requires mpic++)
make test         # Build and run 54 core unit tests (no frameworks, hand-rolled CHECK macro)
make clean

# Sequential render
./raytracer_seq --width 480 --height 360 --spp 16 --depth 8 --shadow-samples 6 --frames 48

# Distributed render (rank 0 = master, ranks 1..N-1 = workers)
mpirun -np 8 ./raytracer_mpi --width 480 --height 360 --spp 16 --depth 8 \
       --shadow-samples 6 --frames 48 --tile 32 --schedule dynamic

# MPI+OpenMP hybrid (--bind-to none lets OpenMP threads spread across cores)
mpirun --bind-to none -np 4 ./raytracer_mpi --threads 2 --frames 48

# Non-blocking prefetch mode (double-buffered worker, master lookahead=2)
mpirun -np 8 ./raytracer_mpi --prefetch --frames 48

# JSON scene config
./raytracer_seq --scene scenes/cornell_box.json --frames 48

# Post-processing
tools/assemble_video.sh frames output/render.mp4 24
python3 tools/compare_frames.py frames_seq frames_mpi   # correctness: expect MSE=0
tools/run_experiments.sh && python3 tools/make_charts.py output
```

## Architecture

Header-only C++17 codebase. Two binaries from one `src/main.cpp` via conditional compilation (`-DUSE_MPI`). No external dependencies beyond STL, MPI, and OpenMP.

### Module ownership

Each `src/` subdirectory maps to a team member's responsibility:

- **`core/`** (Member A) — Math primitives: `Vec3`, `Ray`, `Color`, `RNG` with deterministic per-pixel seeding (`seed_for(frame, px, py, sample)`) and cosine-weighted hemisphere sampling for path tracing, `Timer`.
- **`scene/`** (Members A+D) — Geometry (`Sphere`, `Plane`, `Triangle`, `Box` via `Hittable` interface), `Camera`, `Material` (diffuse/mirror/glossy/dielectric/emissive/checkerboard/colored_glass), `Light` (point/area/spot), `Scene` struct, JSON scene parser.
- **`render/`** (Members B+D) — `shading.hpp` implements **path-traced** ray tracing (direct lighting + indirect GI bounce + Russian Roulette + rough reflections) against `ISceneQuery` (abstract interface). ACES filmic tone mapping. `Renderer::render_tile()` is the stateless entry point called by both sequential and MPI paths. `Image` handles PPM output. `Tile` defines 2D tile regions.
- **`mpi/`** (Member C) — Master-worker scheduling (`mpi_master.hpp`, `mpi_worker.hpp`), serialization protocol (`mpi_serializer.hpp`), MPI tag constants. Worker has two modes: blocking (baseline) and prefetch (non-blocking double-buffered with `MPI_Irecv`/`MPI_Isend`).
- **`benchmark/`** (Member D) — `BenchLog` timing struct with per-rank compute/comm/idle breakdown, `CsvLogger` for experiment data, `gather_logs()` to collect all ranks' timing via `MPI_Gather`.

### Key design invariants

- **Deterministic rendering**: RNG is seeded by `seed_for(frame, px, py, sample)` — independent of tile assignment, process count, or thread count. MPI output must be byte-identical to sequential (MSE=0).
- **Scene as pure function**: `build_demo_scene(aspect, frame, total_frames)` and `build_scene_from_config(cfg, ...)` are pure functions. Workers reconstruct scenes locally from broadcast `RenderParams` (8 ints) + optional JSON config string. No scene geometry crosses the wire.
- **Shader decoupling**: `shade()` operates on `ISceneQuery` (abstract interface), not the concrete `Scene`. This lets the same shading code run identically in sequential and MPI contexts.
- **Wire protocol**: Only three things move over MPI: config (broadcast once, 8 ints), task descriptors (5 ints: frame + tile bounds), and rendered tile pixels (RGB bytes). Defined in `mpi_serializer.hpp`.
- **Scheduling**: Dynamic scheduling uses a single global cursor in the master; static scheduling pre-assigns tasks round-robin. The difference is a single lambda in `mpi_master.hpp`.

### Compilation modes

The `#ifdef USE_MPI` guard in `main.cpp` selects between:
- Sequential path: simple frame loop calling `render_frame_sequential()`.
- MPI path: `MPI_Init`, broadcast config, `run_master()`/`run_worker()` split by rank, `MPI_Finalize`.

The `#ifdef _OPENMP` guard in `renderer.hpp` adds `#pragma omp parallel for` over tile rows within `render_tile()`.

## Tests

Tests live in `src/test_core.cpp` — a single file with a hand-rolled `CHECK(cond)` macro (no test framework). 54 tests cover vec3 algebra, RNG determinism, ray-sphere/plane/triangle/box intersection, camera rays, shading (shadow, GI indirect bounce, reflection, refraction, Fresnel, Beer-Lambert, spotlights, ACES/Reinhard tone mapping, checker patterns). Add new assertions to the same file.

## Scene configs

JSON scene files in `scenes/` define materials (by name), objects (referencing materials), lights, camera, and background. Camera and lights support `orbit` animation configs. Camera supports `aperture` + `focus_dist` for depth of field. Materials support optional `roughness` for glossy reflections. Object types: `sphere`, `plane`, `triangle`, `box` (center + size). 30 scenes total including 22 Minecraft-themed (box blocks), cornell_box, glass_spheres, spotlight_show, demo, cathedral, hall_of_mirrors, frozen_throne, minecraft_village_cinematic.
