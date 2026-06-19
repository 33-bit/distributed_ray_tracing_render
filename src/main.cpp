// Member D — program entry point.
//
// Sequential mode (this file, no -DUSE_MPI): render every frame on one core and
// write PPMs. The MPI master/worker branch is added in Task 6 under #ifdef
// USE_MPI; the sequential path below stays the correctness baseline.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

#include "core/timer.hpp"
#include "render/renderer.hpp"
#include "render/tile.hpp"
#include "scene/scene.hpp"

#ifdef USE_MPI
#include <mpi.h>
#include "mpi/mpi_tags.hpp"
#include "mpi/mpi_serializer.hpp"
#include "mpi/mpi_master.hpp"
#include "mpi/mpi_worker.hpp"
#include "benchmark/benchmark.hpp"
#include "benchmark/csv_logger.hpp"
#endif

namespace fs = std::filesystem;

struct Options {
    RenderParams params;
    std::string out_dir = "frames";
    std::string schedule = "dynamic";   // dynamic | static (MPI build)
    std::string bench_csv;              // if set, append per-rank timing here
};

static Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : "0"; };
        if      (a == "--width")          o.params.width          = std::atoi(val());
        else if (a == "--height")         o.params.height         = std::atoi(val());
        else if (a == "--spp")            o.params.spp            = std::atoi(val());
        else if (a == "--depth")          o.params.max_depth      = std::atoi(val());
        else if (a == "--shadow-samples") o.params.shadow_samples = std::atoi(val());
        else if (a == "--frames")         o.params.total_frames   = std::atoi(val());
        else if (a == "--tile")           o.params.tile_size      = std::atoi(val());
        else if (a == "--out")            o.out_dir               = val();
        else if (a == "--schedule")       o.schedule              = val();
        else if (a == "--bench")          o.bench_csv             = val();
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); }
    }
    return o;
}

// Render one frame on a single core. Returns seconds spent in render_tile.
double render_frame_sequential(const RenderParams& base, int frame, const std::string& out_dir) {
    RenderParams p = base;
    p.frame = frame;
    double aspect = static_cast<double>(p.width) / p.height;
    Scene scene = build_demo_scene(aspect, frame, p.total_frames);
    Image img(p.width, p.height);
    std::vector<Tile> tiles = make_tiles(p.width, p.height, p.tile_size);

    Timer t; t.start();
    for (const Tile& tl : tiles)
        Renderer::render_tile(scene, p, tl, img);
    double secs = t.elapsed_s();

    char path[512];
    std::snprintf(path, sizeof(path), "%s/frame_%04d.ppm", out_dir.c_str(), frame);
    img.write_ppm(path);
    return secs;
}

int main(int argc, char** argv) {
    Options o = parse_args(argc, argv);

#ifdef USE_MPI
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Broadcast the render configuration from the master to all ranks — the
    // canonical "broadcast scene configuration" step (the scene geometry itself
    // is reconstructed deterministically per frame, see mpi_serializer.hpp).
    int pbuf[8];
    if (rank == 0) mpi_proto::encode_params(o.params, pbuf);
    MPI_Bcast(pbuf, 8, MPI_INT, 0, MPI_COMM_WORLD);
    RenderParams params = (rank == 0) ? o.params : mpi_proto::decode_params(pbuf);
    Schedule mode = (o.schedule == "static") ? Schedule::Static : Schedule::Dynamic;

    BenchLog log;
    if (rank == 0) {
        fs::create_directories(o.out_dir);
        std::printf("[mpi] %d proc(s)  %s  %dx%d spp=%d depth=%d shadow=%d frames=%d tile=%d -> %s/\n",
                    size, o.schedule.c_str(), params.width, params.height, params.spp,
                    params.max_depth, params.shadow_samples, params.total_frames,
                    params.tile_size, o.out_dir.c_str());
        run_master(params, o.out_dir, mode, log);
    } else {
        run_worker(params, log);
    }

    std::vector<BenchLog> all = gather_logs(log, rank, size);
    if (rank == 0) {
        // Console summary: makespan + how balanced the workers' compute was.
        double comp_min = 1e30, comp_max = 0.0, comp_sum = 0.0;
        int nw = 0;
        for (const BenchLog& b : all)
            if (b.role == 1) { comp_min = std::min(comp_min, b.comp_s);
                               comp_max = std::max(comp_max, b.comp_s);
                               comp_sum += b.comp_s; ++nw; }
        std::printf("[mpi] makespan %.3fs", log.total_s);
        if (nw > 0)
            std::printf("  | worker comp min %.3f max %.3f avg %.3f  imbalance %.1f%%",
                        comp_min, comp_max, comp_sum / nw,
                        comp_max > 0 ? 100.0 * (comp_max - comp_min) / comp_max : 0.0);
        std::printf("\n");
        if (!o.bench_csv.empty()) {
            CsvLogger::write(o.bench_csv, o.schedule, params, size, all);
            std::printf("[mpi] benchmark appended -> %s\n", o.bench_csv.c_str());
        }
    }
    MPI_Finalize();
    return 0;
#else
    fs::create_directories(o.out_dir);
    std::printf("[seq] %dx%d  spp=%d  depth=%d  shadow=%d  frames=%d  tile=%d  -> %s/\n",
                o.params.width, o.params.height, o.params.spp, o.params.max_depth,
                o.params.shadow_samples, o.params.total_frames, o.params.tile_size,
                o.out_dir.c_str());

    Timer total; total.start();
    for (int f = 0; f < o.params.total_frames; ++f) {
        double secs = render_frame_sequential(o.params, f, o.out_dir);
        std::printf("[seq] frame %4d/%d  %.3fs\n", f + 1, o.params.total_frames, secs);
    }
    std::printf("[seq] done in %.3fs\n", total.elapsed_s());
    return 0;
#endif
}
