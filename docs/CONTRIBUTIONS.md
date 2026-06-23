# Member contributions & difficulty

Who built what, how much, and how hard it was. Each member owns a subsystem with a
small, well-defined interface and a dev journal in `docs/members/` recording every
decision (*idea → what I did → why-not-the-alternative*). LOC = non-blank lines of
**main renderer/MPI code** in owned files — each member clears the ≥ 250 bar on
this measure alone.

## Work summary

| Member | Subsystem | Key work built | Main LOC | Unit tests |
|---|---|---|---|---|
| **A** | Rendering core & math | `Vec3` algebra · ray · pixel-seeded deterministic **RNG** + cosine-weighted hemisphere sampling · pinhole/**thin-lens DOF** camera · ray–**sphere**/plane/**triangle**/**box (AABB)** intersection · `Hittable`/`HitRecord` interface · optional **BVH** accel (median-split, off by default) | **~390** | 43 |
| **B** | Lighting, materials, shading | **Path-traced GI** (indirect diffuse bounce + Russian Roulette) · Phong diffuse+specular · hard + **soft shadows** (Monte Carlo) · recursive **reflection** + Schlick **Fresnel** · **rough reflections** (glossy) · **refraction** (Snell, TIR, Beer–Lambert glass) · **spotlight** + attenuation · checker texture · gamma + **ACES filmic** tone-map · all behind the `ISceneQuery` interface | **~300** | 26 |
| **C** | MPI scheduling & communication | dynamic **master–worker** (`MPI_ANY_SOURCE`) · static baseline · scene/tile **serializer** · per-rank comp/comm/idle timing (`MPI_Gather`) · **MPI+OpenMP hybrid** · **non-blocking prefetch** (depth-2 dispatch + `Isend`/`Irecv`) | **322** | end-to-end |
| **D** | Integration, benchmark, animation, video | `Scene` + `Renderer` glue · **anti-aliasing** · PPM image I/O · **animation** (orbiting camera + sweeping light) · **JSON scene parser** (32 scenes) · CLI/`main` · timing + **CSV** · **MSE correctness tool** · experiment runner · **charts** · ffmpeg **video** | **~450** | end-to-end |

Member D additionally owns the ~435-line **report/tooling layer** (not counted
above so it doesn't inflate the comparison): `benchmark/{benchmark,csv_logger}` +
`tools/{run_experiments.sh, make_charts.py, compare_frames.py, assemble_video.sh}`.

## Difficulty

| Member | Difficulty | Hardest single part | Why it's that hard |
|---|---|---|---|
| **A** | ★★★☆☆ **Medium** | Möller–Trumbore triangle + the determinism RNG design | The math must be *exactly* right — it's the foundation everything stands on — but the techniques are standard and the code is small. |
| **B** | ★★★★☆ **Medium-High** | Refraction (Snell + Fresnel + total internal reflection) and recursive reflection | Several light-transport physics models stacked together; easy to get subtly wrong (wrong normal side, energy blow-up, double gamma). Dense, careful code. |
| **C** | ★★★★★ **High** | Non-blocking prefetch (double-buffered worker + depth-2 master) without deadlock | The real parallel-computing depth: protocol design, `ANY_SOURCE`, FIFO result tracking, **plus** two advanced modes (hybrid threading, async overlap). Most error-prone — concurrency bugs are the worst kind. |
| **D** | ★★★★☆ **Medium-High** | Benchmarking methodology + the pure-function/determinism integration | Huge **breadth** (10+ files, 4 tools) and the part that makes the project *measurable and provably correct* — best-of-N trials, comp/comm/idle split, MSE proof, animation-as-pure-function. Volume + judgment, not one hard algorithm. |

**In one line:** **C is the hardest** (the distributed-systems core — protocol,
hybrid, non-blocking). **B is the trickiest physics.** **D is the most work by
volume** (integration + measurement + tooling). **A is the most foundational**
(everything else depends on it being exact).

## How the work was divided

The split follows the proposal's recommended subsystem ownership, chosen so each
member has a clear interface and nobody blocks anyone else:

- **A → B → D** is the render pipeline (geometry → shading → assembly); **C**
  wraps the finished renderer in the distributed engine without touching it.
- Two files are shared by design: `src/test_core.cpp` (A and B each append their
  own unit tests) and `src/main.cpp` (D owns it; C added the `#ifdef USE_MPI`
  branch). These are noted in the journals.
- The clean seam that made parallel work independent of graphics: Member B's
  shader talks to an abstract `ISceneQuery`, and the scene is a deterministic pure
  function — so Member C could parallelize without understanding the shading, and
  the result is provably identical to the sequential renderer (MSE = 0).

## Verified outcomes (shared credit)

- **69 unit tests pass** (original 42 + box AABB + ACES tone map + GI tests +
  15 BVH/AABB tests); clean build of both `raytracer_seq` and `raytracer_mpi`
  (with OpenMP).
- **All 5 parallel modes** — dynamic, static, hybrid, prefetch, hybrid+prefetch —
  produce **byte-identical** output to the sequential baseline (MSE = 0). The
  optional `--bvh` accelerator is also MSE = 0 against the linear scan, on
  every scene tested, sequential and MPI.
- Full experiment suite: speedup/efficiency, granularity, dynamic-vs-static load
  balance, and the MPI×OpenMP hybrid split (`docs/PROJECT.md` §6,
  `docs/results/`).
- **32 JSON scene configs** (24 Minecraft-themed with box blocks, Cornell box,
  glass spheres, spotlight show, demo, cathedral, hall of mirrors, frozen throne,
  mirror glass gallery, cinematic village).

_See each member's journal in `docs/members/` for the step-by-step reasoning, and
`docs/VIVA_QA.md` for the "explain your part" pitches._
