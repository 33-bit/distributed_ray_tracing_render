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

## Theory I should know (my part)

> Full version: `docs/PROJECT.md` Appendix A.2–A.3. My job: take the (already
> correct) renderer and make many machines/cores run it together.

**Why it parallelizes at all.** Every pixel is computed independently — no pixel
needs another's result. That's **data parallelism**, the *embarrassingly
parallel* best case. I split each frame into 2-D **tiles** (decomposition) and
hand tiles out to processors (**mapping**).

**Two kinds of parallel memory:**
- **Distributed memory (MPI):** many **processes**, each with its *own* memory,
  possibly on different machines, cooperating by **sending messages**. Each has a
  **rank** in a **communicator** (`MPI_COMM_WORLD`).
- **Shared memory (OpenMP):** many **threads** on one machine sharing the *same*
  memory, via compiler pragmas. We use **both** (hybrid).

**MPI calls I use:**
- Point-to-point: `MPI_Send`/`MPI_Recv` (*blocking* — wait till done),
  `MPI_Isend`/`MPI_Irecv` (*non-blocking* — start now, `MPI_Wait` later).
- Collectives: `MPI_Bcast` (one→all, the config), `MPI_Gather` (all→one, the
  timings).
- `MPI_ANY_SOURCE` — receive from whichever worker finishes first (enables
  dynamic load balancing).

**Master–worker.** Rank 0 (**master**) generates tiles and assembles frames; ranks
1…P−1 (**workers**) loop: ask → render → return. **Dynamic** scheduling = a free
worker immediately pulls the next tile, so uneven tile costs even out. **Static**
(`tile mod nworkers`) fixes the split up front and can strand a slow worker — my
load-balance experiment shows dynamic 3 % imbalance vs static 27 %.

**OpenMP (hybrid).** `#pragma omp parallel for` splits a loop's iterations across
a node's cores (**fork–join**: spawn threads, run, rejoin). I thread each worker's
tile rows, so the system is two-level: MPI across nodes, OpenMP within a node.

**Non-blocking / overlap.** A blocking worker idles while waiting for its next
task. With `Isend`/`Irecv` it *starts* the transfer and keeps rendering, then
waits — **hiding communication latency behind computation** (my `--prefetch`).

**How we judge it (the numbers I report):**
- **Speedup** `S(P)=T(1)/T(P)`, **Efficiency** `E(P)=S(P)/P` (ideal = 1).
- **Amdahl's law** `S(P)=1/(f+(1−f)/P)`: a serial fraction `f` caps speedup at
  `1/f` — why it saturates.
- **Granularity** = compute ÷ communication per task: too fine → message overhead;
  too coarse → imbalance.
- **Communication cost** ≈ `latency + size/bandwidth` — tiny on one node, large
  over a network (why prefetch helps more on a real cluster).

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

### 2026-06-20 — MPI + OpenMP hybrid

**Idea.** Single-node MPI ignores the obvious second axis of parallelism: each
worker owns many cores. Add a shared-memory layer so the system is genuinely
two-level — distributed across processes, threaded within each.

**What I did.** `#pragma omp parallel for` over the tile's rows in `render_tile`,
a `--threads N` flag (`omp_set_num_threads`), and OS-detected OpenMP in the
Makefile (libomp + `-Xpreprocessor` on macOS, plain `-fopenmp` on Linux). Threads
recorded in the CSV.

**Why this, not the alternative.**
- *Thread the pixel rows, not whole tiles per thread.* Rows are the natural
  independent unit inside `render_tile`; each pixel writes a distinct location and
  seeds its own RNG, so the output is bit-identical for any thread count — no
  locks, determinism preserved (re-verified MSE=0).
- *`schedule(dynamic,1)`* because per-row cost varies (glass/mirror rows are
  heavier), so dynamic keeps threads balanced.
- *Gotcha:* `mpirun --bind-to none` is required, else Open MPI pins each process
  to one core and OpenMP cannot expand.

**Result.** One worker scales **8.48 s → 1.74 s over 1→7 threads (4.86×)**. The
hybrid (e.g. 2 procs × 7 threads) is competitive with flat MPI (8 procs × 1) and
is the canonical pattern for a cluster of multi-core nodes.

### 2026-06-20 — Non-blocking prefetch

**Idea.** In the blocking loop a worker stalls after sending a result until the
master replies with the next task. Hide that round-trip by always keeping the
next tile in flight.

**What I did.** A `--prefetch` mode: the master runs **depth-2 dispatch** (a
per-rank FIFO of in-flight tasks); the worker **double-buffers** — posts
`MPI_Irecv` for the next tile and `MPI_Isend` for the previous result, both
overlapping the current render, then `MPI_Wait`s on the prefetched task. The
blocking path (lookahead=1) is untouched as the baseline.

**Why this, not the alternative.**
- *Generalized the master to depth-N* rather than writing a second master —
  depth 1 reproduces the old behaviour exactly, depth 2 enables prefetch, and
  results still arrive in per-rank FIFO order so tracking stays a simple deque.
- *Wait on the previous `Isend` before repacking the send buffer* — the one
  correctness subtlety of reusing a single buffer; cheap because the master
  consumes promptly.

**Result.** Correct (MSE=0, no deadlock). The gain is small on one node
(0.79→0.78 s; 0.625→0.617 s at fine tiles) because shared-memory MPI latency is
tiny — the benefit grows with real network latency. That communication is *not*
the bottleneck here is itself a useful finding.
