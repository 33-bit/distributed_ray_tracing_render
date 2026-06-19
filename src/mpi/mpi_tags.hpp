#pragma once
// Member C — MPI message tags and scheduling mode.
//
// The protocol is deliberately tiny: the master sends a 5-int TASK (or a STOP),
// the worker replies with the tile's RGB bytes tagged RESULT. There is no
// separate "request" message — a worker's RESULT *is* its request for more
// work (the master responds to each result with the next task or a stop),
// which is the classic self-scheduling master-worker loop.
#ifdef USE_MPI

enum MpiTag {
    TAG_TASK   = 1,   // master -> worker: a tile to render (5 ints)
    TAG_RESULT = 2,   // worker -> master: rendered tile bytes
    TAG_STOP   = 3    // master -> worker: no more work, exit
};

// How tiles are bound to workers.
//   Dynamic = pull from one global queue (faster workers do more) -> balanced.
//   Static  = each worker owns tiles where task_id % nworkers == worker_id-1
//             (fixed up front) -> the comparison baseline that can imbalance.
enum class Schedule { Dynamic, Static };

#endif  // USE_MPI
