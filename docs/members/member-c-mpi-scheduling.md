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

### 2026-06-19 — Master-worker tile scheduling (`mpi_tags`, `mpi_serializer`, `mpi_master`, `mpi_worker`)

**Idea.** Distribute the per-frame tiles across ranks with a dynamic
master-worker scheduler (rank 0 hands out work, ranks 1..P-1 render), and keep a
static `task_id % nworkers` mode as the comparison baseline. The renderer
(Member D) must not need to change — the MPI layer only changes *who calls*
`render_tile`.

**What I did.**
- `mpi_tags.hpp`: tags `TASK / RESULT / STOP` + `Schedule{Dynamic,Static}`.
- `mpi_serializer.hpp`: the wire format — config↔8 ints, task↔5 ints, tile
  pixels↔RGB bytes. Crucially, **no scene geometry on the wire**.
- `mpi_worker.hpp`: recv task → rebuild scene for that frame → `render_tile` →
  send bytes. Loops until `STOP`.
- `mpi_master.hpp`: build all `(frame,tile)` tasks, seed one per worker, then on
  each result (received via `MPI_ANY_SOURCE`) place the tile and hand that worker
  its next task (or stop it). Assemble each frame and write it the moment its
  last tile lands.
- `main.cpp`: `MPI_Init`, `MPI_Bcast` the config, rank 0 → master else worker.

**Why this, not the alternative.**
- *Reconstruct the scene from broadcast params instead of `MPI_Bcast`-ing
  serialized geometry.* This is the most important call in my subsystem.
  Member D's `build_demo_scene(aspect, frame, total)` is a pure function, so a
  worker can rebuild the exact scene for any frame from 8 ints. Serializing the
  polymorphic object list every frame would be more code, more bug surface, and
  more bytes — and it would buy nothing, because the determinism it needs is
  already guaranteed. (Full geometry serialization is easy to add — the
  materials/objects are POD — but it is unnecessary here, so YAGNI.)
- *Self-scheduling: a worker's RESULT is its request for more work.* No separate
  REQUEST message. Fewer messages, simpler state machine, and it is the textbook
  load-balancing master-worker loop.
- *`MPI_ANY_SOURCE` receive.* The master takes whichever worker finishes first
  and immediately refills it — fast workers naturally do more tiles. That is the
  whole point of dynamic scheduling and what the granularity experiment measures.
- *`pending[rank]` on the master instead of a header in each result.* Because the
  master remembers which task it gave each rank, the result message is just raw
  tile bytes — it needs no `(frame,tile)` header. `MPI_Probe` + `MPI_Get_count`
  size the receive buffer.
- *Tiles as RGB bytes, not doubles.* The worker quantizes exactly like
  `Image::write_ppm`, so the master's assembled frame is bit-identical to the
  sequential one, at 1/8th the bytes of sending doubles. (Communication is tiny
  next to render cost anyway, as the proposal notes — but cheaper is still
  better.)
- *One loop for both schedule modes.* Dynamic pulls from a global cursor; static
  pulls from per-worker queues (`task_id % nworkers`). Identical messaging — the
  only difference is the source of "next task," which is exactly the conceptual
  difference between the two strategies.
- *Frame-major task order + write-and-evict assembly.* Keeps only a few frames
  buffered at once instead of all F, bounding master memory.
- *`np==1` falls back to local rendering* so the speedup baseline T1 runs through
  the same binary/code path.

**Verification — the payoff.** Rendered the same 4-frame scene four ways:
`seq`, `mpirun -np 4` dynamic, `-np 4` static, `-np 1`. **Every frame is
byte-for-byte identical** across all four (`cmp` clean, MSE = 0 exactly). So the
distributed renderer is provably correct: scheduling and worker count change
*who* computes a pixel, never *what* it computes.

> Debug note for the team: the determinism made this easy to validate, but my
> first test *script* used an unquoted `$ARGS` variable — zsh does not
> word-split those, so the flags were swallowed and every run silently used
> `frames=1`. The renderer was right the whole time; the harness was wrong.
> Lesson: pass flags literally (or use a zsh array) when scripting `mpirun`.

### 2026-06-19 — Per-rank timing + gather (with Member D)

**Idea.** To explain *where* time goes (and prove dynamic balances better than
static), each rank must report how long it spent computing vs communicating vs
waiting.

**What I did.** Added `BenchLog{comp,comm,idle,tiles,total}` and instrumented
both loops: a worker charges `idle` to the blocking `Recv` that waits for a
task, `comp` to scene-build + `render_tile`, `comm` to the result `Send`; the
master charges `idle` to `MPI_Probe` (waiting for any result) and `comm` to
recv/send. After rendering, `gather_logs()` does one `MPI_Gather` of a 7-double
record per rank to rank 0.

**Why this, not the alternative.**
- *Three-way split (comp/comm/idle), not just total.* The speedup experiment
  needs "runtime with vs without communication," and the load-balance experiment
  needs idle/compute per rank — both fall straight out of this split.
- *`MPI_Gather` of a fixed 7-double record, not a custom MPI struct datatype.*
  Packing to doubles is a few lines, portable, and avoids `MPI_Type_create_struct`
  ceremony for what is a tiny one-shot collective.
- *Charge the task-wait to `idle`, the transfer to `comm`.* With blocking calls
  the two are physically interleaved, but attributing the `Recv`-for-task wait to
  idle and the `Send`/result `Recv` to comm is the honest, defensible split.

**Result.** 400×300, spp 8, depth 6, 6 frames, tile 32, `-np 4`: dynamic worker
compute spread = **3.5 %** vs static **7.3 %**, and dynamic gave the slow worker
*fewer* tiles (250 vs 265) to compensate while static forced 260 each. The
balanced demo scene keeps the gap modest; coarser tiles widen it (granularity
experiment, Task 8).
