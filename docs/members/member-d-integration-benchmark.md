# Member D — Scene, Renderer, Benchmark, Animation & Video · Dev Journal

**Subsystem:** the integrator. Owns the concrete `Scene`, the `Renderer` glue
that drives A's camera+intersection and B's shading per pixel, PPM image I/O,
the animation (orbiting camera + moving light), the CLI/`main`, per-rank timing
and CSV benchmarking, and the ffmpeg/chart/correctness tooling. The "tie it all
together and measure it" layer.

**Files I own:** `src/scene/scene.hpp`, `src/render/renderer.hpp`, `tile.hpp`,
`image.hpp`, `src/core/timer.hpp`, `src/benchmark/benchmark.hpp`,
`csv_logger.hpp`, `src/main.cpp`, `tools/*`.

## Interface contract (what A, B, C rely on)

```cpp
struct Tile { int x0, y0, x1, y1; };                // half-open pixel rectangle
struct RenderParams { int width, height, spp, max_depth, shadow_samples,
                      frame, total_frames, tile_size; };

struct Image { int w, h; /* RGB */ Image(int,int); void set(int x,int y,Color);
               void write_ppm(const std::string& path) const; };

struct Scene : ISceneQuery { Camera camera; /* objects, materials, lights, bg */ };
Scene build_demo_scene(double aspect, int frame, int total_frames); // animation here

struct Renderer { static void render_tile(const Scene&, const RenderParams&,
                                          const Tile&, Image& out); };

struct Timer { void start(); double elapsed_s() const; };
struct BenchLog { double comp_s, comm_s, idle_s; long tiles; }; // per rank, gathered to CSV
```

## Log
<!-- Entries appended as code lands. Format: Idea / What I did / Why this, not the alternative. -->

### 2026-06-19 — Scene + renderer + image + main → first frame

**Idea.** Stand up the smallest thing that proves the whole A+B pipeline works
end to end: one frame, rendered on one core, written to disk and eyeballed.
Everything parallel comes after this is correct (the proposal's rule: "first
make one frame render correctly").

**What I did.**
- `Scene : ISceneQuery` — owns the object list (`unique_ptr<Hittable>`), the
  material and light tables, the camera, and a sky-gradient `background()`.
  Because it implements B's interface, the same object is passed directly to
  `shade()` and (later) to MPI workers.
- `build_demo_scene(aspect, frame, total_frames)` — a *pure function* that
  returns the fully-posed scene for a given frame: checker floor + four spheres
  (diffuse red, mirror, glossy green, glossy gold) under one warm light.
- `Image` (RGB buffer + binary PPM/P6 writer), `Tile` (+ `make_tiles`),
  `Renderer::render_tile` (the AA loop), `Timer`, and `main` (CLI + sequential
  frame loop).

**Why this, not the alternative.**
- *`Scene` implements `ISceneQuery` rather than the shader taking a `Scene&`
  directly.* Keeps B↔D decoupled (B compiles and tests without D) and avoids a
  circular include. This is the seam that lets the identical renderer run
  sequentially and under MPI.
- *`build_demo_scene` is a pure function of the frame index*, not a mutable
  global scene I step forward in time. Reproducibility: rank 7 rendering frame
  12 builds the exact same scene the sequential renderer would, with no shared
  animation state to synchronize. This is the scene-level twin of A's pixel-based
  RNG seeding.
- *PPM/P6, not PNG.* Zero dependencies (PNG would need libpng/zlib), trivial to
  write correctly, and ffmpeg ingests it directly for the video. The proposal
  explicitly uses PPM.
- *Per-sample RNG seeded by `seed_for(frame,px,py,s)` inside `render_tile`.* This
  is the determinism keystone on the renderer side: a pixel's color depends only
  on (scene, params), never on which tile or process computed it — so Task 6 can
  assert MSE(seq, mpi) ≈ 0. I seed *per sample* (not per pixel) so AA and
  soft-shadow samples are decorrelated yet reproducible.
- *Tone map then gamma at write time*, in the renderer, not in `shade()` — keeps
  reflection recursion in linear light (see Member B's note) and is applied
  exactly once per pixel.
- *Image origin top-left, camera v bottom-up* — the renderer flips
  `v = (H-1-py+jy)/H`, so the PPM comes out right-side up without a post-pass.
- *Tiles exist now even though rendering is sequential.* `render_tile` is already
  the unit of work Member C will distribute; introducing it here means the MPI
  layer changes *who calls* `render_tile`, not the renderer itself.

**Verification.** `make seq`, then
`./raytracer_seq --width 600 --height 450 --spp 16 --depth 5 --frames 1`.
PPM header `P6 600 450 255`, byte count = 600·450·3 + header. Converted to PNG
and visually confirmed: four materials, hard shadows on the checker floor,
the mirror sphere reflecting floor + sky, anti-aliased edges. 0.32 s/frame.

### 2026-06-19 — Animation: orbiting camera + sweeping light

**Idea.** Turn the still into a sequence by making the camera and light
functions of the frame index — without introducing any per-frame mutable state.

**What I did.** In `build_demo_scene`, compute `t = frame/total_frames ∈ [0,1)`.
The camera orbits the scene center once over the sequence (`cam_angle = 2π·t`);
the light revolves the opposite way with a phase offset
(`light_angle = -2π·t + 0.7`). The geometry stays put. `main`'s frame loop
already calls `build_demo_scene(aspect, f, F)` per frame, so nothing else
changed.

**Why this, not the alternative.**
- *Camera and light as pure functions of `frame`*, not a scene I mutate frame to
  frame. Same reasoning as the static scene: any rank can build any frame
  independently with no animation state to broadcast or keep in sync. Animation
  becomes embarrassingly parallel across frames *and* tiles.
- *Light orbits opposite the camera.* If the light tracked the camera, shadows
  would barely move on screen. Counter-rotating makes the moving shadows
  obvious, which is exactly the visual the proposal asks the demo to show.
- *Full 360° camera orbit* so the video showcases every sphere and the mirror
  from all sides.

**Verification.** `--frames 12`: 12 PPMs written. Frame 0 vs frame 6 (PNG,
side by side) shows the viewpoint rotated 180° (sphere left/right order flips,
the front gold sphere becomes occluded by the mirror) and the shadows pointing a
different way — i.e. both camera and light animate. ~0.1 s/frame at 480×360 spp8.

### 2026-06-19 — CSV logging + correctness tool (with Member C)

**Idea.** Persist Member C's per-rank timings for charting, and add a tool that
*quantitatively* proves the MPI output equals the sequential baseline.

**What I did.**
- `csv_logger.hpp` — appends one row per rank (config + comp/comm/idle/tiles/
  total), writing the header only when the file is new.
- `tools/compare_frames.py` — reads two PPM directories and reports per-frame +
  overall MSE, max abs diff, mean abs diff (numpy-accelerated, pure-Python
  fallback). Exit 0 if within threshold; flags byte-identical specially.

**Why this, not the alternative.**
- *Append, not overwrite.* An experiment sweep (`-np 1,2,4,8`; tile 16…128) dumps
  all runs into one CSV that `make_charts.py` reads — no per-run file juggling.
- *Separate Python tool, not a C++ check.* Correctness validation should be
  independent of the renderer it validates; a tiny external script that only
  knows the PPM format is more convincing and is reusable on any two frame dirs.
- *MSE + max + mean, not just "files equal."* The report wants the metrics named
  in the proposal, and a max-abs of 0 is a stronger statement than a single bit.

**Result.** seq vs `-np 4` dynamic and seq vs `-np 4` static both report
**MSE = 0, max diff = 0** across all frames — byte-identical, confirming the
determinism design end to end.

> Debug note: my first `compare_frames.py` read the second buffer as `int32`
> instead of `uint8`, so numpy saw mismatched array lengths and threw. The
> renderer was never wrong (`cmp` had already shown the files identical) — the
> bug was in the checker. Fixed the dtype.

### 2026-06-20 — Video, experiment runner, charts (+ scene tuning)

**Idea.** Produce the final artifacts: the rendered video, the experiment CSVs,
and the report charts (speedup, granularity, load balance).

**What I did.**
- `assemble_video.sh` — ffmpeg PPM→H.264 mp4 (even-dimension scale filter).
- `run_experiments.sh` — sweeps tile size {16,32,64,128} and P {1,2,4,8},
  best-of-N trials, dumps CSVs.
- `make_charts.py` — speedup+efficiency, granularity stacked bar, static-vs-
  dynamic load balance.
- Tuning that came out of the first noisy run (see below): made the demo light
  an **area light** so `--shadow-samples` produces real soft shadows, and made
  the worker/master **cache the scene per frame** instead of rebuilding it per
  tile.

**Why this, not the alternative.**
- *H.264/mp4 via ffmpeg, even-dim scale filter.* yuv420p (needed for broad
  playback) requires even width/height; the `scale=trunc(iw/2)*2:...` filter
  guarantees it without me constraining render resolutions.
- *Best-of-N trials, keep the fastest.* My first single-trial sweep had a
  wandering outlier (one config would randomly spike 2–3×) because the laptop
  was doing other work. Min-of-N is the standard way to estimate compute time
  under contention; `make_charts.py` chunks each config's rows into trials and
  keeps the fastest. This turned a noisy granularity plot into the clean
  comm-grows-with-finer-tiles result.
- *Area light + soft shadows for the benchmark workload.* The first speedup runs
  were overhead-bound (per-tile compute too small next to MPI traffic). Soft
  shadows add real, content-dependent compute per tile, which (a) makes the
  speedup curve meaningful, (b) makes the load-balance experiment matter, and
  (c) demonstrates a feature we promised. Determinism still holds — the
  soft-shadow samples are pixel-seeded — so seq vs MPI stays MSE=0 (re-verified
  with `--shadow-samples 4`).
- *Scene cached per frame.* Rebuilding the scene for every tile was redundant
  work that unfairly inflated worker time vs the sequential baseline (which
  builds once per frame). Caching makes the comparison apples-to-apples.

**Results (480×360, spp 16, depth 6, soft shadows ×4, 8 frames, best-of-3):**
- **Speedup**: T1 = 4.58 s; P=2 → 4.71 s (1 worker ≈ 1 core, dedicated master);
  P=4 → 1.60 s (2.9×); P=8 → 0.91 s (**5.0×**, ~72 % efficiency vs the 7 workers).
- **Granularity**: communication share is largest at 16×16 and shrinks as tiles
  grow; makespan drifts down 0.95 s → 0.87 s (16→128). Coarser = less master
  traffic.
- **Load balance**: dynamic **1.6 %** worker-compute spread vs static **5.8 %**.
- **Correctness**: MSE = 0 (byte-identical) with hard and soft shadows.
- **Video**: 480×360 H.264, 48 frames, orbiting camera + soft moving shadows.

### 2026-06-20 — Re-run after the glass sphere landed (final numbers)

When Member B added the refractive glass sphere I re-ran the whole suite, because
glass adds heavy, content-dependent per-tile cost. The headline numbers in
`docs/PROJECT.md` §6 are this final run:
- **Speedup**: T1 = 7.15 s → P=4 2.9×, **P=8 5.3×**.
- **Granularity**: communication share 4.0 % (16×16) → 0.4 % (128×128).
- **Load balance**: dynamic **1.3 %** vs static **31.6 %** — far more dramatic
  than before, because the glass/mirror tiles are now genuinely expensive, so a
  static cyclic split strands the unlucky worker. This is the clearest possible
  argument for dynamic scheduling, and it came for free from a graphics feature.
- **Correctness** still MSE = 0 with all features on.

Retrospective: keeping the renderer a pure, deterministic function of
(scene, params) was the decision that paid off most — it made the parallel build
*provably* correct (`cmp`-clean), turned debugging into a non-event, and let
every experiment be re-run without fear. The one thing I'd change: let rank 0
render when idle, to reclaim the dedicated-master core and lift the speedup
ceiling from P−1 toward P.

