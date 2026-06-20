# Viva / defense cheat-sheet — likely questions & answers

Rehearse these out loud. Answers are deliberately short — say the core, then stop.
Deeper detail is in `docs/PROJECT.md` (esp. §4–§6 and Appendix A).

---

## 1. Big picture

**Q. What does the project do?**
An MPI(+OpenMP) distributed ray tracer. It renders an animated 3-D scene as image
frames, splits each frame into tiles, distributes the tiles across processes
(and machines), assembles the frames into a video, and measures the parallel
performance — speedup, granularity, load balance — while proving the result is
identical to a single-core renderer.

**Q. What exactly is parallelized, and why can it be?**
The pixels of every frame. Each pixel is computed independently — no pixel needs
another pixel's result — so the work is *embarrassingly parallel*. We group pixels
into 2-D tiles and render tiles concurrently.

**Q. Sequential vs parallel version — what's the difference?**
Same renderer code. Sequential loops over all tiles on one core (the correctness
baseline). The MPI version has a master hand tiles to worker processes. The
*rendering* is byte-for-byte the same; only *who runs each tile* changes.

---

## 2. Decomposition, mapping, communication (the report's core)

**Q. What decomposition strategy?**
**Hybrid**: data decomposition (each frame → tiles of pixels) plus task
decomposition (each tile is an independent task `(frame, x0,y0,x1,y1)`).

**Q. How are tasks mapped to processes?**
**Dynamic master–worker.** Rank 0 = master (makes tasks, assembles frames); ranks
1…P−1 = workers (request a tile, render, return it, repeat).

**Q. What MPI communication pattern?**
A **star** (master talks to every worker). `MPI_Bcast` the config to all;
`MPI_Send`/`MPI_Recv` for tasks and results; `MPI_Recv` with `MPI_ANY_SOURCE` so
the master takes whoever finishes first; `MPI_Gather` to collect timings.

**Q. Why master–worker, not a ring/tree/hypercube?**
Tiles are independent — no neighbour communication is needed — so a topology built
for neighbour exchange buys nothing. Master–worker is the natural fit and gives
free dynamic load balancing.

**Q. Blocking or non-blocking communication?**
Default is blocking (`Send`/`Recv`) — simple and the compute per tile dwarfs the
message cost. We also implemented an optional **non-blocking prefetch**
(`Isend`/`Irecv`) that overlaps the next-tile transfer with the current render.

**Q. How does the master know which tile a received result belongs to?**
It remembers what it handed each worker in a per-rank FIFO (`inflight[rank]`).
Because a worker returns results in order, the master pops the front of that
queue — so the result message is just raw pixel bytes, no header needed.

**Q. Do you serialize and send the scene to workers?**
No — and that's deliberate. The scene is a *pure function* of the frame index
(`build_demo_scene`), so each worker reconstructs the exact scene from the
broadcast parameters. Nothing but the binary, the 8-int config, and tile pixels
ever travels on the wire.

---

## 3. Load balancing

**Q. How do you balance load, and does it matter?**
Dynamic scheduling: a worker pulls the next tile the instant it's free, so fast
workers naturally do more. It matters because tiles cost different amounts — a
tile over the glass/mirror sphere triggers deep reflection/refraction recursion;
a background tile is cheap.

**Q. Dynamic vs static results?**
Measured (P=8, tile 64): dynamic keeps workers within **3 %** of each other;
static (`tile mod nworkers`, fixed up front) drifts to **27 %** and runs ~19 %
slower. That's our headline argument for dynamic.

**Q. Tile-size (granularity) tradeoff?**
Fine tiles → better balance but more messages/scheduling overhead (comm share
rose to ~8.6 % at 16×16). Coarse tiles → less overhead but risk imbalance. The
granularity experiment finds the sweet spot (≈64×64 here).

---

## 4. Hybrid MPI + OpenMP

**Q. What is the hybrid and why both?**
MPI parallelizes *across* processes/machines (distributed memory); OpenMP
parallelizes *within* a machine across its cores (shared memory). Real clusters
are multi-core nodes, so using both — one MPI process per node, many OpenMP
threads inside — is the standard HPC pattern.

**Q. Is the OpenMP loop safe (no race conditions)?**
Yes. `#pragma omp parallel for` splits the tile's rows across threads; each pixel
writes its own distinct location and seeds its own RNG, so there's no shared
mutable state and no locks. Output is bit-identical for any thread count.

**Q. Why `mpirun --bind-to none` for the hybrid?**
By default Open MPI pins each process to a single core, which would trap all the
OpenMP threads on one core. `--bind-to none` lets each process spread across the
node's cores.

---

## 5. Correctness

**Q. How do you know the parallel result is correct?**
We compare every MPI frame to the sequential frame with **MSE** (mean squared
error) and the max pixel difference. Every mode — dynamic, static, hybrid,
prefetch — gives **MSE = 0, max diff = 0**: byte-for-byte identical.

**Q. How can it be identical when you use random sampling (AA, soft shadows)?**
Each random sample is seeded by *where and when* it is —
`seed_for(frame, x, y, sample)` — never by which process draws it. So the renderer
is a deterministic pure function of (scene, parameters); splitting the work can't
change any pixel's value.

**Q. What is MSE?**
`MSE = (1/N) Σ (aᵢ − bᵢ)²` over all pixel bytes. 0 means the two images are
exactly equal.

---

## 6. Performance & experiments

**Q. What speedup did you get?**
Flat MPI: ~2.8× at P=4 and ~4–5× at P=8 (varies with scene/contention). Within one
worker, OpenMP alone scales ~4.86× over 1→7 threads.

**Q. Why isn't speedup linear (= P)?**
Three reasons: (1) rank 0 is a dedicated coordinator, so only P−1 cores actually
render — the realistic ceiling is P−1; (2) the single master serializes
scheduling traffic; (3) memory bandwidth and all-core clock throttling on one
chip. We report efficiency against the P−1 workers for a fair view.

**Q. Why is P=2 about the same as P=1?**
With P=2 there is one master + one worker, i.e. one rendering core — same as
sequential, plus a little MPI overhead. Speedup only appears from P=4 up.

**Q. State Amdahl's law and what it means here.**
`S(P) = 1/(f + (1−f)/P)`; a serial fraction `f` caps speedup at `1/f`. Our serial
part is mostly the master's coordination and frame writing, which is why the curve
flattens as P grows. (Gustafson: scaling improves if we grow the problem with P —
e.g. higher resolution on a real cluster.)

**Q. What is parallel efficiency?**
`E(P) = Speedup(P)/P` — speedup per processor; 1.0 (100 %) is ideal.

---

## 7. Graphics (if they ask)

**Q. How does ray tracing work, in one breath?**
For each pixel, shoot a ray from the camera; find the nearest surface it hits;
compute the colour there from the lights, shadows, and any reflected/refracted
rays; write the pixel.

**Q. How do you intersect a ray and a sphere?**
Substitute the ray `O+tD` into `|X−C|²=r²` → a quadratic `at²+bt+c=0`. The
discriminant says hit or miss; the smaller positive root is the nearest surface.

**Q. What's the normal and why do you need it?**
The unit vector perpendicular to the surface at the hit. Lighting depends on it —
e.g. diffuse brightness is `N·L` (cosine to the light), and reflection/refraction
are defined relative to it.

**Q. Hard vs soft shadows?**
Hard: one shadow ray to a point light (in shadow or not). Soft: sample many points
on an area light and average visibility — partial blocking gives a smooth
penumbra (Monte Carlo).

**Q. Reflection vs refraction?**
Reflection bounces the ray about the normal (`R = D − 2(D·N)N`). Refraction bends
it through glass by **Snell's law**; if it's too steep to exit it's **total
internal reflection**; the reflect/refract ratio is the **Fresnel** term (Schlick
approximation), and **Beer–Lambert** tints thick glass.

**Q. Why gamma correction?**
Monitors are non-linear. We do all lighting in linear space and encode `c^(1/2.2)`
at the very end so brightness looks right.

---

## 8. Tricky / "why didn't you…" questions

**Q. Isn't this just embarrassingly parallel — is it actually hard?**
The *decomposition* is easy, yes, and we say so. The engineering is where the
depth is: a provable byte-exact correctness guarantee, dynamic load balancing
with a measured static baseline, a two-level **MPI+OpenMP hybrid**, and
**non-blocking** communication — the last two are the "advanced" items the brief
lists as optional.

**Q. Why doesn't the master also render?**
It's a dedicated coordinator — simpler and the standard master–worker design. The
cost is one idle core (the P−1 ceiling). Letting it render while idle is our top
listed future improvement.

**Q. Did you run on real multiple machines?**
We benchmarked on one 8-core node (`-np` simulates the cluster). The code is
cluster-ready (`mpirun --hostfile`); `docs/CLUSTER_SETUP.md` is the full
multi-machine guide, and the cleanest reliable option is 4 identical cloud VMs.

**Q. Why is the prefetch speedup small?**
On one node MPI uses shared memory, so message latency is tiny — communication
isn't the bottleneck here. The overlap pays off as network latency grows (a real
cluster). Showing comm is *not* the bottleneck is itself a valid result.

**Q. What would you improve with more time?**
Let the master render when idle (recover the core); add a BVH so large scenes
scale; run a true multi-node weak-scaling study.

---

## 9. "Explain your part in 30 seconds" (per member)

**Member A — rendering core & math.** "I own the geometry and math: vectors, the
ray, ray–sphere/plane/triangle intersection, the camera, and the deterministic
pixel-seeded RNG. That RNG is what makes the whole renderer reproducible, which
the correctness proof relies on."

**Member B — lighting, materials, shading.** "I turn a ray–surface hit into a
colour: Phong diffuse+specular, hard and soft shadows, recursive reflection with
Fresnel, refractive glass with Snell + Beer–Lambert, a spotlight, and gamma. It's
all behind an abstract scene interface so it never depends on how the scene is
stored or distributed."

**Member C — MPI scheduling & communication.** "I make it parallel: the dynamic
master–worker scheduler over MPI with `ANY_SOURCE`, a static baseline for
comparison, the wire format, per-rank timing, and the two advanced modes — the
MPI+OpenMP hybrid and the non-blocking prefetch."

**Member D — integration, benchmark, animation, video.** "I tie A and B into the
renderer, drive the animation, handle image/video output, and own the
measurement: the per-rank timing, the CSV, the speedup/granularity/load-balance
charts, and the MSE correctness tool that proves parallel == sequential."
