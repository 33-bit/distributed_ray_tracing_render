#pragma once
// Member D — a wall-clock stopwatch used for both the sequential timing and,
// later, the per-rank computation/communication/idle breakdown. std::chrono so
// it is portable and independent of MPI (MPI_Wtime is used only inside the MPI
// layer, where it is the natural choice).
#include <chrono>

struct Timer {
    std::chrono::high_resolution_clock::time_point t0;

    void start() { t0 = std::chrono::high_resolution_clock::now(); }

    double elapsed_s() const {
        auto t1 = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(t1 - t0).count();
    }
};
