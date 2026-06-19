# Member C — MPI Scheduling & Communication · Dev Journal

**Subsystem:** the distributed engine. Splits each frame into tiles, distributes
them across ranks with a dynamic master-worker scheduler, serializes the scene
and tile results over MPI, and provides a static scheduler as a comparison
baseline. The "how it parallelizes" layer.

**Files I own:** `src/mpi/mpi_tags.hpp`, `mpi_serializer.hpp`, `mpi_master.hpp`,
`mpi_worker.hpp` (all guarded by `#ifdef USE_MPI`).

## Interface contract

```cpp
enum Tag { TAG_TASK = 1, TAG_RESULT, TAG_REQUEST, TAG_STOP };
enum class Schedule { Dynamic, Static };

// Serialization (POD double buffers, easiest to MPI_Bcast/Send).
std::vector<double> serialize_scene_params(const RenderParams&, int total_frames, int schedule);
// tile result = header (tile coords + frame) followed by RGB triples.

// I consume D's renderer; I do not know how a tile is rendered, only how to ship it.
void run_master(const RenderParams& base, int total_frames, Schedule mode, BenchLog& log);
void run_worker(BenchLog& log);
```

I depend on D's `Scene`, `RenderParams`, `render_tile()`, and `Image`; I do not
touch shading or geometry internals.

## Log
<!-- Entries appended as code lands. Format: Idea / What I did / Why this, not the alternative. -->
