# MPI Distributed Ray Tracing Renderer

Course project for **IT4130E — Parallel and Distributed Programming**.

An MPI-based distributed ray tracer (C++17) that renders an animated 3D scene
with hard & soft shadows, reflection, refractive glass, and anti-aliasing. Each
frame is split into 2D tiles distributed across MPI ranks by a dynamic
master-worker scheduler; frames are assembled into a video. The project measures
correctness (vs a sequential baseline), runtime, communication cost, granularity,
and speedup.

**Headline results:** MPI output is **byte-identical** to the sequential renderer
(MSE = 0); **5.3× speedup at 8 processes**; dynamic scheduling holds worker
imbalance to **1.3 %** where static scheduling drifts to **31.6 %**.

> Full write-up: [`docs/PROJECT.md`](docs/PROJECT.md) · Design spec:
> [`docs/superpowers/specs/2026-06-19-mpi-ray-tracer-design.md`](docs/superpowers/specs/2026-06-19-mpi-ray-tracer-design.md)

## Quick start

```bash
# 1. unit tests for the renderer core (Members A & B) — 32 checks
make test

# 2. sequential baseline — renders frames to frames/
make seq
./raytracer_seq --width 480 --height 360 --spp 16 --depth 8 --shadow-samples 6 --frames 48

# 3. distributed render across N ranks (1 master + N-1 workers)
make mpi
mpirun -np 8 ./raytracer_mpi --width 480 --height 360 --spp 16 --depth 8 \
       --shadow-samples 6 --frames 48 --tile 32 --schedule dynamic

# 4. video, experiments + charts, and the seq-vs-mpi correctness check
tools/assemble_video.sh frames output/render.mp4 24
tools/run_experiments.sh && python3 tools/make_charts.py output
python3 tools/compare_frames.py frames_seq frames_mpi      # expect MSE = 0
```

## Requirements

- C++17 compiler (Apple clang / g++)
- Open MPI (`mpic++`, `mpirun`)
- ffmpeg (video), Python 3 + matplotlib (charts)

## Team & ownership

| Member | Subsystem | Journal |
|---|---|---|
| A | Rendering core & math | [member-a](docs/members/member-a-rendering-core.md) |
| B | Lighting, materials, shading | [member-b](docs/members/member-b-lighting-materials.md) |
| C | MPI scheduling & communication | [member-c](docs/members/member-c-mpi-scheduling.md) |
| D | Scene, renderer, benchmark, animation, video | [member-d](docs/members/member-d-integration-benchmark.md) |

_Build/run details and experiment results are expanded in `docs/PROJECT.md`._
