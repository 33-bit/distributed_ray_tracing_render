# MPI-Based Distributed Ray Tracing Renderer

**IT4130E — Parallel and Distributed Programming · 4-member project**

This project implements a distributed ray tracing renderer in **C++17 + MPI**.
It renders an animated 3D scene — shadows, soft shadows, reflections, refraction
(glass), anti-aliasing, an orbiting camera and a moving light — as a sequence of
image frames, distributes the per-frame tile workload across an MPI cluster with
a **dynamic master-worker scheduler**, assembles the frames into a video, and
measures correctness, runtime, communication overhead, granularity, speedup and
parallel efficiency.

It is positioned as a **distributed rendering *system***, not a graphics demo:
the priority is a working MPI pipeline with measurable, explainable parallel
performance and a provable correctness guarantee against a sequential baseline.

![Sample frame](results/sample_frame.png)

*Demo scene: checker floor, a diffuse sphere, a mirror sphere, a refractive
glass sphere, and a glossy gold sphere, under one soft area light.*

---

## 1. Why ray tracing, and why parallelize it?

Each output pixel is computed by shooting a ray from the camera into the scene
and recursively tracing what it hits (shadows, reflections, refraction). Pixels
are **independent** — no pixel needs the result of another — so the image is
*embarrassingly parallel*. But it is also **expensive**: cost grows as

```
N = Frames × Width × Height × SamplesPerPixel × RayDepth × ShadowSamples
```

This combination — expensive and independent — is exactly what distributed
computing is for. We split each frame into rectangular **tiles** and let many
machines render tiles at once.

## 2. Results at a glance

Measured on one 8-core machine (`mpirun -np P`, 1 master + P−1 workers),
480×360, spp 16, reflection depth 8, soft shadows ×4, best-of-3 trials:

| Metric | Result |
|---|---|
| **Correctness** | Sequential vs MPI frames are **byte-identical (MSE = 0)** — dynamic, static, and any P |
| **Speedup** | 2.9× at P=4, **5.3× at P=8** (≈66 % efficiency vs P, ≈88 % vs the P−1 workers) |
| **Load balance** | dynamic **1.3 %** worker imbalance vs static **31.6 %** |
| **Granularity** | communication share falls 4.0 % → 0.4 % as tiles grow 16→128 |
| **Output** | 480×360 H.264 video, orbiting camera + moving soft shadows |

## 3. Architecture

The renderer core is **MPI-agnostic**; MPI lives in its own module and is only
compiled into the distributed build. One codebase, two binaries:

```
make seq  -> raytracer_seq   (clang++,           correctness baseline)
make mpi  -> raytracer_mpi   (mpic++ -DUSE_MPI,  adds master-worker layer)
```

```
                         ┌──────────────────────────────────────────┐
                         │                  main.cpp                 │
                         │   CLI · seq frame-loop  /  MPI rank split │
                         └───────────────┬───────────────┬──────────┘
                                         │               │
                        ┌────────────────▼───┐      ┌────▼─────────────────┐
                        │  Renderer (D)      │      │  MPI layer (C)        │
                        │  render_tile():    │      │  master / worker /    │
                        │  AA + tonemap+gamma│◄─────│  serializer / tags    │
                        └───────┬─────┬──────┘ calls└───────────────────────┘
                                │     │
                 ┌──────────────▼─┐ ┌─▼───────────────────┐
                 │ Camera+geometry│ │ shade()  (B)        │
                 │ (A)            │ │ Phong · shadows ·   │
                 │ Vec3/Ray/RNG/  │ │ reflection ·        │
                 │ Sphere/Plane   │ │ refraction · gamma  │
                 └────────────────┘ └─────────┬───────────┘
                                              │ queries
                                    ┌─────────▼──────────┐
                                    │ Scene : ISceneQuery │ (D)
                                    │ objects/materials/  │
                                    │ lights/camera/bg    │
                                    └─────────────────────┘
```

**Key seam:** the shader (B) is written against an abstract `ISceneQuery`, not
the concrete `Scene` (D). The *same* `shade()` runs in the sequential renderer
and inside every MPI worker, and each subsystem can be compiled and unit-tested
on its own.

### Data flow for one tile
```
Renderer.render_tile(tile)
  for each pixel, for each sample s:
      rng = RNG( seed_for(frame, x, y, s) )          # A — deterministic
      ray = Camera.get_ray(u + jitter, v + jitter)   # A
      color += shade(scene, ray, depth=0, ...)        # B (recurses for mirror/glass)
  pixel = gamma( tonemap( color / spp ) )            # B helpers, D applies
```

## 4. Parallel model (the report's core questions)

**What is parallelized?** The pixels of every frame, grouped into 2-D tiles.

**Why is it parallelizable?** Pixels are independent; a tile needs only the
(read-only) scene, never a neighbour tile's result.

**Decomposition — hybrid (data + task).** Data: each frame is split into
`tile_size × tile_size` pixel blocks. Task: each tile is an independent unit of
work `(frame, x0,y0,x1,y1)`.

**Mapping — dynamic master-worker.**
```
Rank 0  = master : generates tasks, hands them out, assembles & writes frames
Rank 1..P-1 = workers : receive a tile, render it, return the pixels, repeat
```

**Communication — star topology, blocking MPI.**
```
            worker 1
               │
 worker 4 ── master(0) ── worker 2          MPI_Bcast : config to all
               │                            MPI_Send/Recv + ANY_SOURCE : tiles
            worker 3                         MPI_Gather : timing stats
```
A worker's result message *is* its request for more work (self-scheduling), so
there is no separate request message. The master remembers which tile it gave
each worker (`pending[rank]`), so a result is just raw RGB bytes — no header.

**Load balancing.** Tiles vary in cost (a tile over the glass sphere triggers
deep reflection/refraction recursion; a background tile is cheap). With
**dynamic** scheduling a worker that finishes early immediately pulls the next
tile, so fast workers naturally do more. The **static** baseline binds tile *i*
to worker `i mod (P−1)` up front and cannot adapt — which is why it imbalances
(see §6).

## 5. Correctness — identical to the sequential renderer

The renderer is a **pure function of (scene, parameters)**:

- The scene is rebuilt from the frame index by `build_demo_scene(aspect, frame,
  total)` — no shared mutable animation state.
- Every random sample is seeded by `seed_for(frame, x, y, sample)` — by *where
  and when* the sample is, never by *which process* draws it.

So a pixel computes the same value regardless of which rank, which tile, or which
schedule renders it. Validated with `tools/compare_frames.py`: sequential vs
`-np 4/6/8`, dynamic and static, hard and soft shadows — **MSE = 0, max pixel
difference = 0 (byte-identical)**, not merely "close".

## 6. Experiments

Run them yourself: `make mpi && tools/run_experiments.sh && python3 tools/make_charts.py output`

### 6.1 Speedup & efficiency
![Speedup](results/chart_speedup.png)

T1 = 7.15 s. P=4 → 2.45 s (**2.9×**), P=8 → 1.35 s (**5.3×**). The measured curve
hugs the *P−1* line because rank 0 is a dedicated coordinator — with one worker
(P=2) there is no speedup, and efficiency is best measured against the P−1 actual
compute processes (≈88 %). The "excl. comm" curve sits just above "incl. comm",
confirming communication is a small fraction of runtime.

### 6.2 Granularity
![Granularity](results/chart_granularity.png)

Fixed N and P=8, tile size 16→128. Compute per worker is roughly constant; the
**communication share shrinks from 4.0 % (16×16) to 0.4 % (128×128)** because
coarser tiles mean fewer, larger messages through the single master. The classic
trade-off: fine tiles balance better but cost more scheduling traffic.

### 6.3 Load balance — dynamic vs static
![Load balance](results/chart_schedule.png)

P=8, tile 64. **Dynamic keeps every worker within 1.3 %** of the others; **static
spreads them by 31.6 %** (one worker draws several heavy glass/mirror tiles while
others idle), making the static run ~15 % slower overall. This is the headline
argument for dynamic scheduling.

## 7. Team & contributions

Each member owns a subsystem with a small, well-defined interface and a dev
journal (`docs/members/`) recording *idea → what I did → why-not-the-alternative*
for every step. LOC = non-blank lines in owned files plus that member's unit
tests / wiring (each ≥ 250).

| Member | Subsystem | Owns | ~LOC | Journal |
|---|---|---|---|---|
| **A** | Rendering core & math | `core/{vec3,ray,color,random}`, `scene/{object,camera,sphere,plane}` | ~275 | [A](members/member-a-rendering-core.md) |
| **B** | Lighting, materials, shading | `scene/{material,light}`, `render/shading` | ~275 | [B](members/member-b-lighting-materials.md) |
| **C** | MPI scheduling & communication | `mpi/{tags,serializer,master,worker}` | ~310 | [C](members/member-c-mpi-scheduling.md) |
| **D** | Scene, renderer, benchmark, animation, video | `scene/scene`, `render/{renderer,image,tile}`, `core/timer`, `benchmark/*`, `main.cpp`, `tools/*` | ~765 | [D](members/member-d-integration-benchmark.md) |

- **A** provides the geometry/numeric foundation, including the pixel-seeded
  deterministic RNG that the whole correctness story rests on.
- **B** turns a ray-hit into a color: Phong diffuse+specular, hard & soft
  shadows, recursive reflection with Fresnel, refractive glass, gamma — all
  behind the `ISceneQuery` interface so it never depends on the concrete scene.
- **C** distributes the work: dynamic master-worker over `MPI_ANY_SOURCE`, a
  static baseline, the wire format, and the per-rank comp/comm/idle timing.
- **D** ties it together (scene, renderer glue, PPM I/O, animation, CLI) and owns
  measurement & delivery (timing, CSV, correctness tool, charts, video).

## 8. Build & run

```bash
# unit tests (Members A & B)
make test                 # 32 checks

# sequential baseline
make seq
./raytracer_seq --width 480 --height 360 --spp 16 --depth 8 --shadow-samples 6 --frames 48

# distributed render
make mpi
mpirun -np 8 ./raytracer_mpi --width 480 --height 360 --spp 16 --depth 8 \
       --shadow-samples 6 --frames 48 --tile 32 --schedule dynamic

# video, experiments, charts, correctness
tools/assemble_video.sh frames output/render.mp4 24
tools/run_experiments.sh
python3 tools/make_charts.py output
python3 tools/compare_frames.py frames_seq frames_mpi
```

CLI flags: `--width --height --spp --depth --shadow-samples --frames --tile
--schedule {dynamic|static} --out <dir> --bench <csv>`.

**Multi-machine note:** on a real cluster, add a hostfile —
`mpirun --hostfile hosts -np 12 ./raytracer_mpi ...`. The code is unchanged; only
the launch differs. (For grading, `-np` on one node simulates the cluster.)

## 9. Repository layout

```
src/
  core/      vec3 ray color random timer            (A, +D timer)
  scene/     object camera sphere plane             (A)
             material light                          (B)
             scene                                   (D)
  render/    shading                                 (B)
             renderer image tile                     (D)
  mpi/       tags serializer master worker           (C)
  benchmark/ benchmark csv_logger                    (D)
  main.cpp   test_core.cpp
tools/       assemble_video.sh run_experiments.sh make_charts.py compare_frames.py
docs/        PROJECT.md  members/*.md  results/*  superpowers/{specs,plans}/*
```

## 10. Scope, limitations, future work

**Done:** distributed tile rendering, dynamic + static scheduling, hard & soft
shadows, reflection, refraction, anti-aliasing, animation, video, byte-exact
correctness, full benchmark suite.

**Known limitations (honest):**
- Rank 0 is a dedicated coordinator, so the practical speedup ceiling is P−1
  cores. A master that also renders when idle would recover that core.
- A single master serializes all scheduling traffic; at very fine tiles this is
  the bottleneck (visible in §6.2).
- No spatial acceleration structure (BVH): every ray tests every object. Fine for
  a handful of spheres; would matter for large scenes.

**Deferred (per proposal §13):** BVH, textures, triangle meshes, non-blocking
MPI. None are needed for the distributed-rendering goal.
