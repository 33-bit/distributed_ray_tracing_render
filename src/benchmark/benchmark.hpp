#pragma once
// Member C + D — per-rank performance record and its collective gather.
//
// Each rank fills a BenchLog while it runs (workers in mpi_worker, master in
// mpi_master). After rendering, all ranks call gather_logs() so rank 0 ends up
// with everyone's numbers and can emit one CSV row per rank (csv_logger.hpp).
//
// Time is split three ways so the experiments can separate "useful work" from
// "MPI overhead" and "waiting":
//   comp = rendering (build scene + render_tile)
//   comm = bytes on the wire (MPI_Send/Recv of tasks and tiles)
//   idle = blocked waiting for the other side (worker waits for a task;
//          master waits in MPI_Probe for the next result)
#include <string>
#include <vector>

struct BenchLog {
    int    rank   = 0;
    int    role   = 0;     // 0 = master, 1 = worker
    double comp_s = 0.0;
    double comm_s = 0.0;
    double idle_s = 0.0;
    long   tiles  = 0;
    double total_s = 0.0;  // wall time this rank was alive
};

#ifdef USE_MPI
#include <mpi.h>

// Collective: every rank contributes its BenchLog; rank 0 returns all of them
// (others return an empty vector). Packed as 7 doubles to use a single
// MPI_Gather of a fixed-size record.
inline std::vector<BenchLog> gather_logs(const BenchLog& mine, int rank, int size) {
    double s[7] = { static_cast<double>(mine.rank),  static_cast<double>(mine.role),
                    mine.comp_s, mine.comm_s, mine.idle_s,
                    static_cast<double>(mine.tiles), mine.total_s };
    std::vector<double> r(rank == 0 ? static_cast<size_t>(size) * 7 : 0);
    MPI_Gather(s, 7, MPI_DOUBLE, r.data(), 7, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    std::vector<BenchLog> out;
    if (rank == 0) {
        out.resize(size);
        for (int i = 0; i < size; ++i) {
            BenchLog b;
            b.rank   = static_cast<int>(r[i * 7 + 0]);
            b.role   = static_cast<int>(r[i * 7 + 1]);
            b.comp_s = r[i * 7 + 2];
            b.comm_s = r[i * 7 + 3];
            b.idle_s = r[i * 7 + 4];
            b.tiles  = static_cast<long>(r[i * 7 + 5]);
            b.total_s = r[i * 7 + 6];
            out[i] = b;
        }
    }
    return out;
}
#endif  // USE_MPI
