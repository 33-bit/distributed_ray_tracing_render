#pragma once
// Member C — master process (rank 0).
//
// Responsibilities: build the (frame, tile) task list, hand tasks to workers,
// collect rendered tiles via MPI_ANY_SOURCE, assemble each frame, and write it
// to disk as soon as its last tile arrives.
//
// Scheduling modes share one loop; the only difference is where "the next task
// for this worker" comes from:
//   Dynamic -> a single global cursor (whoever finishes first gets the next
//              tile)  => self-balancing.
//   Static  -> each worker has a fixed queue (task_id % nworkers)  => a worker
//              that drew heavy tiles falls behind while others idle. This is the
//              baseline the load-balance experiment compares against.
#ifdef USE_MPI

#include <mpi.h>
#include <vector>
#include <map>
#include <string>
#include <cstdio>
#include "mpi/mpi_tags.hpp"
#include "mpi/mpi_serializer.hpp"
#include "render/renderer.hpp"
#include "render/tile.hpp"
#include "scene/scene.hpp"

inline void run_master(const RenderParams& base, const std::string& out_dir, Schedule mode) {
    int size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    const int nworkers = size - 1;
    const double aspect = static_cast<double>(base.width) / base.height;

    // Build every (frame, tile) task, frame-major so frames finish ~in order
    // and at most a few are buffered at once.
    const std::vector<Tile> tiles = make_tiles(base.width, base.height, base.tile_size);
    std::vector<Task> tasks;
    tasks.reserve(static_cast<size_t>(base.total_frames) * tiles.size());
    for (int f = 0; f < base.total_frames; ++f)
        for (const Tile& t : tiles) tasks.push_back(Task{ f, t });
    const int T = static_cast<int>(tasks.size());

    // Per-frame assembly: lazily create an Image, write + evict when complete.
    std::map<int, Image> frame_img;
    std::vector<int> remaining(base.total_frames, static_cast<int>(tiles.size()));

    auto frame_buffer = [&](int f) -> Image& {
        auto it = frame_img.find(f);
        if (it == frame_img.end())
            it = frame_img.emplace(f, Image(base.width, base.height)).first;
        return it->second;
    };
    auto finish_frame = [&](int f) {
        char path[512];
        std::snprintf(path, sizeof(path), "%s/frame_%04d.ppm", out_dir.c_str(), f);
        frame_img.at(f).write_ppm(path);   // entry created by frame_buffer(); .at avoids needing a default ctor
        frame_img.erase(f);
    };

    // np == 1: no workers, render everything locally (the speedup baseline T1).
    if (nworkers <= 0) {
        for (const Task& tk : tasks) {
            RenderParams p = base; p.frame = tk.frame;
            Scene sc = build_demo_scene(aspect, tk.frame, base.total_frames);
            Renderer::render_tile(sc, p, tk.tile, frame_buffer(tk.frame));
            if (--remaining[tk.frame] == 0) finish_frame(tk.frame);
        }
        return;
    }

    // Task sources: a global cursor (dynamic) and per-worker queues (static).
    int next_global = 0;
    std::vector<std::vector<int>> wq(nworkers);
    for (int i = 0; i < T; ++i) wq[i % nworkers].push_back(i);
    std::vector<size_t> wcursor(nworkers, 0);
    std::vector<int> pending(size, -1);   // pending[rank] = task index in flight

    auto next_task_for = [&](int rank) -> int {
        if (mode == Schedule::Dynamic)
            return (next_global < T) ? next_global++ : -1;
        int w = rank - 1;
        return (wcursor[w] < wq[w].size()) ? wq[w][wcursor[w]++] : -1;
    };
    auto send_task = [&](int rank, int ti) {
        int buf[5];
        mpi_proto::encode_task(tasks[ti].frame, tasks[ti].tile, buf);
        MPI_Send(buf, 5, MPI_INT, rank, TAG_TASK, MPI_COMM_WORLD);
        pending[rank] = ti;
    };
    auto send_stop = [&](int rank) {
        int z[5] = { 0, 0, 0, 0, 0 };
        MPI_Send(z, 5, MPI_INT, rank, TAG_STOP, MPI_COMM_WORLD);
    };

    // Seed each worker with one task (or stop it if there is nothing for it).
    for (int r = 1; r <= nworkers; ++r) {
        int ti = next_task_for(r);
        if (ti >= 0) send_task(r, ti); else send_stop(r);
    }

    // Collect results; on each, place the tile, then refill or stop that worker.
    int completed = 0;
    while (completed < T) {
        MPI_Status st;
        MPI_Probe(MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &st);
        int src = st.MPI_SOURCE;
        int count;
        MPI_Get_count(&st, MPI_UNSIGNED_CHAR, &count);
        std::vector<unsigned char> buf(count);
        MPI_Recv(buf.data(), count, MPI_UNSIGNED_CHAR, src, TAG_RESULT, MPI_COMM_WORLD, &st);

        const Task& tk = tasks[pending[src]];
        mpi_proto::unpack_tile(frame_buffer(tk.frame), tk.tile, buf.data());
        ++completed;
        if (--remaining[tk.frame] == 0) finish_frame(tk.frame);

        int nt = next_task_for(src);
        if (nt >= 0) send_task(src, nt); else send_stop(src);
    }
}

#endif  // USE_MPI
