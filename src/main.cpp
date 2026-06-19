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

#include "core/timer.hpp"
#include "render/renderer.hpp"
#include "render/tile.hpp"
#include "scene/scene.hpp"

namespace fs = std::filesystem;

struct Options {
    RenderParams params;
    std::string out_dir = "frames";
    std::string schedule = "dynamic";   // used by the MPI build (Task 6)
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
}
