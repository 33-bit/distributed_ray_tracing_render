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
