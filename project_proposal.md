# MPI-Based Distributed Ray Tracing Renderer with Animated Shadow Output

## 1. Project Overview

This project implements a distributed ray tracing renderer using **MPI and C++**. The renderer generates an animated 3D scene with lighting, shadows, reflections, anti-aliasing, and moving camera or light sources. Each rendered frame is saved as an image, and the sequence of frames is later combined into a video.

The main goal is to demonstrate how a computationally expensive rendering workload can be accelerated by distributing independent rendering tasks across multiple physical machines in an MPI cluster.

In ray tracing, each pixel is computed by shooting rays from the camera into a virtual 3D scene. The color of a pixel depends on ray-object intersections, light visibility, surface material, shadow rays, and optional recursive reflection rays. Since pixels and image regions can be computed independently, ray tracing is a strong candidate for parallelization.

The final output should be a video showing a rendered 3D scene with visible lighting effects such as shadows, moving shadows, reflections, and camera movement.

---

## 2. Target Output

The project should produce an animated video rendered by the MPI cluster.

A suitable final demo could show:

* A camera rotating around a 3D scene.
* A ground plane receiving shadows.
* Multiple spheres or simple objects with different materials.
* A moving light source that changes the shadow direction over time.
* Reflective objects such as a metallic or mirror-like sphere.
* Soft shadows created by sampling an area light.
* Anti-aliased images to reduce jagged edges.

The renderer should output individual image frames, for example:

```text
frames/frame_0000.ppm
frames/frame_0001.ppm
frames/frame_0002.ppm
...
```

These frames can then be converted into a video using an external tool such as `ffmpeg`.

---

## 3. Problem Description

The problem is to render a sequence of images representing an animated 3D scene.

The input consists of:

* Image resolution: width and height.
* Number of animation frames.
* Camera parameters.
* Scene objects.
* Object materials.
* Light sources.
* Samples per pixel.
* Maximum reflection depth.
* Tile size for parallel rendering.
* Number of MPI processes.

The output consists of:

* Rendered image frames.
* A final video assembled from those frames.
* Benchmark logs for runtime, communication time, computation time, speedup, and load balance.

The computational cost increases with:

```text
Number of frames × image resolution × samples per pixel × ray depth × shadow samples
```

Therefore, the effective problem size can be described as:

```text
N = F × W × H × S × D × L
```

Where:

```text
F = number of frames
W = image width
H = image height
S = samples per pixel
D = maximum ray recursion depth
L = number of shadow samples
```

This definition is useful for the experimental section because it connects the input size directly to the rendering workload.

---

## 4. Rendering Method

The renderer should use recursive ray tracing.

For each pixel, the program generates one or more camera rays. Each ray is tested against objects in the scene. If the ray hits an object, the renderer computes the color at the hit point using lighting, shadow testing, and material properties.

A simplified rendering pipeline is:

```text
Generate camera ray
    ↓
Find closest object intersection
    ↓
Compute local lighting
    ↓
Cast shadow ray toward light source
    ↓
Compute reflection ray if the material is reflective
    ↓
Combine local color and reflected color
    ↓
Write final pixel color
```

The minimum recommended rendering features are:

* Ray-sphere intersection.
* Ray-plane intersection.
* Diffuse lighting.
* Specular lighting.
* Hard shadows.
* Recursive reflection.
* Anti-aliasing.
* Animated camera or animated light.

Higher-quality features include:

* Soft shadows using area light sampling.
* Multiple materials.
* Moving objects.
* Gamma correction.
* Simple scene configuration system.
* Optional acceleration structure such as BVH.

For the project deadline, BVH should be treated as optional. A good MPI renderer with dynamic scheduling, shadows, reflection, and video output is already strong enough.

---

## 5. Parallelization Level

The project uses a combination of **data parallelism** and **task parallelism**.

The data being rendered is the image plane. Each frame is divided into smaller 2D rectangular regions called tiles. Since each tile contains a group of pixels that can be rendered independently, the image is decomposed spatially.

At the same time, each tile is treated as an independent rendering task. MPI worker processes repeatedly receive tile tasks, render them, and return the computed pixels to the master process.

Therefore, the parallel model can be described as:

```text
Hybrid data-task parallelism
```

More specifically:

* Data parallelism: the image is split into pixel regions.
* Task parallelism: each tile is assigned as an independent task.
* Distributed memory parallelism: each MPI process performs rendering independently and communicates results through MPI messages.

This explanation fits well with the report requirement about the level of parallelism.

---

## 6. Decomposition Strategy

The recommended decomposition strategy is a **hybrid decomposition**.

The main decomposition is **data decomposition**, because the image is divided into 2D tiles. However, the runtime assignment of these tiles to worker processes is task-based, so the implementation also uses **task decomposition**.

The image plane is divided as follows:

```text
Frame
 ├── Tile 0
 ├── Tile 1
 ├── Tile 2
 ├── ...
 └── Tile K
```

Each tile contains a rectangular block of pixels, for example:

```text
16 × 16 pixels
32 × 32 pixels
64 × 64 pixels
```

The tile size controls the granularity of the parallel workload.

Smaller tiles create more tasks and usually improve load balancing, but they increase communication and scheduling overhead. Larger tiles reduce communication overhead, but they may cause load imbalance because some processes may receive heavier image regions than others.

The project should treat tile size as an important experimental parameter.

---

## 7. Mapping Technique

The preferred mapping technique is **dynamic master-worker task assignment**.

The MPI ranks can be organized as:

```text
Rank 0: Master process
Rank 1 to Rank P-1: Worker processes
```

The master process is responsible for:

* Generating rendering tasks.
* Sending tasks to workers.
* Receiving rendered tile results.
* Assembling complete frames.
* Writing frame images to disk.
* Collecting timing information.

Each worker process is responsible for:

* Receiving scene information.
* Requesting a tile task.
* Rendering the assigned tile.
* Sending the tile result back to the master.
* Reporting computation and communication time.

This design is easier to implement and explain than complex topologies such as ring, tree, or hypercube. It also fits the nature of ray tracing because tiles are independent and do not require neighbor communication.

A static mapping mode can also be included as a comparison baseline. In static mapping, tasks are assigned by task ID:

```text
owner = task_id mod number_of_processes
```

However, the main implementation should use dynamic scheduling because it gives better load balancing.

---

## 8. Communication Strategy

The communication model should be described as a **master-worker communication topology**.

The communication pattern is star-shaped:

```text
          Worker 1
             |
Worker 2 -- Master -- Worker 3
             |
          Worker 4
```

The master communicates directly with every worker.

The typical message flow is:

```text
Master broadcasts scene configuration
Worker requests task
Master sends tile task
Worker renders tile
Worker sends tile result
Master stores tile in frame buffer
Master sends stop signal when all tasks are completed
```

The implementation can use blocking MPI communication for simplicity:

* `MPI_Bcast` for broadcasting global configuration.
* `MPI_Send` and `MPI_Recv` for task distribution and result collection.
* `MPI_Recv` with `MPI_ANY_SOURCE` so the master can receive results from whichever worker finishes first.
* `MPI_Reduce` or `MPI_Gather` for collecting timing statistics.

Non-blocking communication is optional. It can improve sophistication, but it is not required for a successful version. Blocking communication is acceptable because the computation time of rendering a tile is usually much larger than the message overhead.

---

## 9. Load Balancing Considerations

Load balancing is an important part of this project.

Although every tile may contain the same number of pixels, not every tile requires the same computation time. A tile that contains reflective objects, shadows, or many intersections may take longer than a tile containing mostly background.

Possible causes of workload imbalance include:

* Some tiles contain more objects.
* Some tiles require more shadow tests.
* Some materials trigger recursive reflection.
* Some pixels need more ray bounces.
* Some machines in the cluster may have different CPU performance.

Dynamic tile scheduling helps solve this problem. Instead of assigning a fixed number of tiles to each process at the beginning, workers request new work whenever they finish their current tile.

This allows faster workers to process more tiles and slower workers to process fewer tiles.

The granularity experiment should evaluate whether the selected tile size gives good load balance. A useful rule is:

```text
If the idle time difference between any two processes is greater than 25%,
the granularity should be adjusted.
```

If the system is imbalanced, the tile size should usually be reduced to create finer-grained tasks.

---

## 10. Correctness Validation Direction

The correctness of the parallel renderer should be validated by comparing it with a sequential renderer.

The sequential version and parallel version should use the same:

* Scene.
* Camera.
* Light configuration.
* Rendering parameters.
* Random seed strategy.
* Number of frames.
* Image resolution.

For deterministic comparison, random numbers should be generated using pixel-based seeds rather than process-based seeds. This ensures that a pixel has the same random samples regardless of which MPI process renders it.

A good seed formula could be based on:

```text
frame_id, pixel_x, pixel_y, sample_id
```

Correctness can be checked using:

```text
Mean Squared Error between sequential and parallel frames
Maximum absolute pixel difference
Average pixel difference
Visual comparison
```

If deterministic sampling is implemented correctly, the sequential and parallel outputs should be identical or nearly identical, except for small floating-point differences.

---

## 11. Experimental Evaluation Direction

The report should evaluate the program using several experiments.

The first experiment should determine the input size `N` such that the full program runs for approximately 2 to 3 minutes using the total number of available physical CPU cores.

For example, if the cluster has:

```text
3 machines × 4 physical cores = 12 MPI processes
```

Then the program should be tested with 12 processes while varying:

* Resolution.
* Number of frames.
* Samples per pixel.
* Shadow samples.
* Maximum ray depth.

The goal is to find a workload size that is large enough to make parallel execution meaningful.

The second experiment should analyze granularity. The input size is fixed at `N`, and the number of MPI processes is fixed at the total number of physical cores. Then the program is tested with different tile sizes, such as:

```text
16 × 16
32 × 32
64 × 64
128 × 128
```

For each tile size, the program should record per-process:

* Computation time.
* Communication time.
* Idle time.
* Total runtime.

The third experiment should evaluate speedup. The input size should be increased to `2N`, and the number of MPI processes should vary:

```text
1, 2, 4, 8, ..., up to the total available core count
```

The report should show:

```text
Speedup(P) = T1 / TP
Efficiency(P) = Speedup(P) / P
```

The charts should include both:

* Runtime including communication.
* Runtime excluding communication.

This distinction helps show the real cost of MPI communication.

---

## 12. Suggested Program Architecture

The codebase should be modular so that each member can understand and explain their part.

A reasonable structure is:

```text
src/
  main.cpp

  core/
    vec3.hpp
    ray.hpp
    color.hpp
    random.hpp
    timer.hpp

  scene/
    camera.hpp
    material.hpp
    object.hpp
    sphere.hpp
    plane.hpp
    light.hpp
    scene.hpp

  render/
    renderer.hpp
    tile.hpp
    image.hpp
    shading.hpp

  mpi/
    mpi_tags.hpp
    mpi_master.hpp
    mpi_worker.hpp
    mpi_serializer.hpp

  benchmark/
    benchmark.hpp
    csv_logger.hpp
```

The project should separate rendering logic from MPI logic. This makes the program easier to debug because the sequential renderer can be tested before adding distributed execution.

A clean separation could be:

```text
Renderer module:
    Knows how to render a tile.

MPI module:
    Knows how to distribute tiles and collect results.

Benchmark module:
    Knows how to measure and export timing data.
```

This separation also helps during the oral defense because each member can explain a clear subsystem.

---

## 13. Recommended Scope Control

The project should avoid becoming too large. The most important part is not photorealism, but a working MPI renderer with measurable parallel performance.

The recommended core scope is:

```text
Must have:
- MPI cluster execution.
- Tile-based distributed rendering.
- Dynamic task scheduling.
- Shadows.
- Reflection.
- Animation.
- Video output.
- Correctness comparison.
- Benchmark charts.

Nice to have:
- Soft shadows.
- Anti-aliasing.
- Moving light.
- Multiple materials.
- Static vs dynamic scheduling comparison.

Optional:
- BVH acceleration.
- Refraction.
- Texture mapping.
- Triangle mesh loading.
- Non-blocking MPI.
```

The project should prioritize the MPI parallelization and experimental evaluation before adding advanced graphics features.

A visually simple scene with strong shadows and reflections is better than a complex scene that cannot be benchmarked properly.

---

## 14. Report Writing Direction

The report should be written around the parallel computing requirements rather than only around computer graphics.

The central story should be:

```text
Ray tracing is computationally expensive.
The image can be decomposed into independent tiles.
MPI distributes these tiles across machines.
Dynamic scheduling improves load balance.
The parallel renderer produces the same result as the sequential renderer.
The experiments measure runtime, communication cost, granularity, and speedup.
```

The report should clearly answer:

```text
What is being parallelized?
Why is the problem parallelizable?
What decomposition strategy is used?
How are tasks mapped to processes?
What MPI communication pattern is used?
How is load balancing handled?
How is correctness verified?
How is performance measured?
```

The most important diagrams to include are:

* Rendering pipeline diagram.
* Tile decomposition diagram.
* Master-worker MPI architecture diagram.
* Communication flow diagram.
* Runtime chart.
* Granularity stacked bar chart.
* Speedup chart.
* Final rendered frame example.

---

## 15. Planning Orientation

When creating the actual implementation plan, the group should avoid starting directly with MPI. The better direction is to first ensure that the sequential renderer works correctly, then parallelize it.

The planning should be organized around milestones, not around random features.

A good planning mindset is:

```text
First make one frame render correctly.
Then make multiple frames render correctly.
Then split one frame into tiles.
Then distribute tiles using MPI.
Then collect results correctly.
Then add timing logs.
Then tune rendering parameters for benchmark size N.
Then run experiments and generate charts.
```

The implementation plan should also define clear ownership. Since the group has four members, the work should be divided by subsystem:

```text
Rendering core
Lighting and materials
MPI scheduling and communication
Benchmarking, animation, and report
```

Each subsystem should have a clear interface so that members do not block each other too much.

The most important planning rule is:

```text
Do not add advanced visual effects before the distributed rendering pipeline works.
```

A simple renderer that runs correctly on the MPI cluster and produces strong benchmark results is more valuable than a visually ambitious renderer that fails during demo.

---

## 16. Final Project Positioning

The project can be positioned as a distributed rendering system rather than just a graphics program.

A strong project description is:

```text
This project implements an MPI-based distributed ray tracing renderer that generates animated 3D scenes with shadows and reflections. The rendering workload is decomposed into independent image tiles, and a master-worker scheduling model dynamically distributes these tiles across multiple physical machines. The system demonstrates hybrid data-task parallelism, dynamic load balancing, and distributed result aggregation. The final output is a rendered video, while the performance evaluation analyzes correctness, runtime, communication overhead, granularity, speedup, and parallel efficiency.
```

This positioning directly matches the course requirements and gives the project a clear parallel computing focus.

