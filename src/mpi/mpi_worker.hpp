#pragma once
// Member C — worker process (ranks 1..P-1).
//
// Loop: receive a task (or stop) -> rebuild the scene for that frame -> render
// the tile -> send the pixels back. The worker is stateless across tasks except
// for one reused full-frame Image buffer (only the tile region is touched, and
// only that region is packed), so there is no per-task allocation churn.
#ifdef USE_MPI

#include <mpi.h>
#include <vector>
#include "mpi/mpi_tags.hpp"
#include "mpi/mpi_serializer.hpp"
#include "render/renderer.hpp"
#include "scene/scene.hpp"

inline void run_worker(const RenderParams& base) {
    const double aspect = static_cast<double>(base.width) / base.height;
    Image img(base.width, base.height);   // reused scratch buffer
    int task_buf[5];

    for (;;) {
        MPI_Status st;
        MPI_Recv(task_buf, 5, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
        if (st.MPI_TAG == TAG_STOP) break;

        Task task = mpi_proto::decode_task(task_buf);
        RenderParams p = base;
        p.frame = task.frame;

        Scene scene = build_demo_scene(aspect, task.frame, base.total_frames);
        Renderer::render_tile(scene, p, task.tile, img);

        std::vector<unsigned char> bytes = mpi_proto::pack_tile(img, task.tile);
        MPI_Send(bytes.data(), static_cast<int>(bytes.size()),
                 MPI_UNSIGNED_CHAR, 0, TAG_RESULT, MPI_COMM_WORLD);
    }
}

#endif  // USE_MPI
