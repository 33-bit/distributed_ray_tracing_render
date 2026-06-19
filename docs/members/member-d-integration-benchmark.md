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
