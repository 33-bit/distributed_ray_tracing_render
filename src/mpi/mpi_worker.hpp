#pragma once
// Member C — worker process (ranks 1..P-1).
//
// Two modes:
//   blocking  : recv task -> render -> send result -> repeat. Simple, the
//               correctness baseline.
//   prefetch  : double-buffered. While rendering the current tile, the next tile
//               is already in flight (MPI_Irecv) and the previous result is being
//               sent (MPI_Isend), so the master round-trip overlaps computation.
//               Requires the master to run depth-2 dispatch (lookahead=2).
//
// In both modes the worker keeps one reused full-frame Image and rebuilds the
// scene only when the frame changes.
#ifdef USE_MPI

#include <mpi.h>
#include <vector>
#include "mpi/mpi_tags.hpp"
#include "mpi/mpi_serializer.hpp"
#include "render/renderer.hpp"
#include "scene/scene.hpp"
#include "core/timer.hpp"
#include "benchmark/benchmark.hpp"
#include "scene/scene_parser.hpp"

inline void run_worker(const RenderParams& base, BenchLog& log, bool prefetch = false,
                       const SceneConfig* cfg = nullptr) {
    int rank; MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    log.rank = rank; log.role = 1;

    const double aspect = static_cast<double>(base.width) / base.height;
    Image img(base.width, base.height);   // reused scratch buffer
    int   cached_frame = 0;
    Scene scene = cfg ? build_scene_from_config(*cfg, aspect, 0, base.total_frames)
                      : build_demo_scene(aspect, 0, base.total_frames);
    scene.enable_bvh(base.use_bvh);

    auto render = [&](const Task& task) {
        if (task.frame != cached_frame) {
            scene = cfg ? build_scene_from_config(*cfg, aspect, task.frame, base.total_frames)
                        : build_demo_scene(aspect, task.frame, base.total_frames);
            scene.enable_bvh(base.use_bvh);
            cached_frame = task.frame;
        }
        RenderParams p = base; p.frame = task.frame;
        Renderer::render_tile(scene, p, task.tile, img);
    };

    Timer wall; wall.start();
    Timer t;

    // ---- blocking mode (baseline) ----
    if (!prefetch) {
        int task_buf[5];
        for (;;) {
            MPI_Status st;
            t.start();
            MPI_Recv(task_buf, 5, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
            log.idle_s += t.elapsed_s();
            if (st.MPI_TAG == TAG_STOP) break;

            Task task = mpi_proto::decode_task(task_buf);
            t.start(); render(task); log.comp_s += t.elapsed_s();

            std::vector<unsigned char> bytes = mpi_proto::pack_tile(img, task.tile);
            t.start();
            MPI_Send(bytes.data(), static_cast<int>(bytes.size()),
                     MPI_UNSIGNED_CHAR, 0, TAG_RESULT, MPI_COMM_WORLD);
            log.comm_s += t.elapsed_s();
            ++log.tiles;
        }
        log.total_s = wall.elapsed_s();
        return;
    }

    // ---- prefetch mode (non-blocking double buffer) ----
    int  bufA[5], bufB[5];
    int* cur = bufA;
    int* nxt = bufB;
    MPI_Status st;

    t.start();
    MPI_Recv(cur, 5, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);   // first task
    log.idle_s += t.elapsed_s();
    if (st.MPI_TAG == TAG_STOP) { log.total_s = wall.elapsed_s(); return; }

    MPI_Request req_task;
    MPI_Irecv(nxt, 5, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &req_task);  // prefetch next

    std::vector<unsigned char> sendbuf;
    MPI_Request req_send = MPI_REQUEST_NULL;

    for (;;) {
        Task task = mpi_proto::decode_task(cur);
        t.start(); render(task); log.comp_s += t.elapsed_s();   // overlaps in-flight Irecv/Isend

        t.start();
        if (req_send != MPI_REQUEST_NULL) MPI_Wait(&req_send, MPI_STATUS_IGNORE);
        sendbuf = mpi_proto::pack_tile(img, task.tile);
        MPI_Isend(sendbuf.data(), static_cast<int>(sendbuf.size()),
                  MPI_UNSIGNED_CHAR, 0, TAG_RESULT, MPI_COMM_WORLD, &req_send);
        log.comm_s += t.elapsed_s();
        ++log.tiles;

        t.start();
        MPI_Wait(&req_task, &st);            // the next task was prefetched during render
        log.idle_s += t.elapsed_s();
        if (st.MPI_TAG == TAG_STOP) break;

        int* tmp = cur; cur = nxt; nxt = tmp;
        MPI_Irecv(nxt, 5, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &req_task);
    }
    if (req_send != MPI_REQUEST_NULL) MPI_Wait(&req_send, MPI_STATUS_IGNORE);
    log.total_s = wall.elapsed_s();
}

#endif  // USE_MPI
