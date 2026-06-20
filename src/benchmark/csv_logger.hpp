#pragma once
// Member D — append per-rank BenchLogs to a CSV. Append (not overwrite) so a
// whole experiment sweep (many nprocs / tile sizes) accumulates into one file
// that tools/make_charts.py reads. The header is written only when the file is
// first created.
#include <string>
#include <vector>
#include <fstream>
#include "benchmark/benchmark.hpp"
#include "render/renderer.hpp"   // RenderParams

struct CsvLogger {
    static void write(const std::string& path, const std::string& schedule,
                      const RenderParams& p, int nprocs, int threads,
                      const std::vector<BenchLog>& logs) {
        bool exists = std::ifstream(path).good();
        std::ofstream out(path, std::ios::app);
        if (!exists)
            out << "nprocs,threads,schedule,width,height,spp,depth,shadow_samples,frames,tile,"
                   "rank,role,comp_s,comm_s,idle_s,tiles,total_s\n";
        for (const BenchLog& b : logs) {
            out << nprocs << ',' << threads << ',' << schedule << ','
                << p.width << ',' << p.height << ',' << p.spp << ',' << p.max_depth << ','
                << p.shadow_samples << ',' << p.total_frames << ',' << p.tile_size << ','
                << b.rank << ',' << (b.role ? "worker" : "master") << ','
                << b.comp_s << ',' << b.comm_s << ',' << b.idle_s << ','
                << b.tiles << ',' << b.total_s << '\n';
        }
    }
};
