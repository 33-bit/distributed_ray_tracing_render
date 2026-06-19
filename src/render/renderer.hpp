#pragma once
// Member D — the renderer glue. render_tile() is the one function that ties the
// whole pipeline together: for every pixel in a tile it generates anti-aliased
// camera rays (Member A), shades them (Member B), then tone-maps + gamma-
// corrects the average. It is deliberately stateless and tile-scoped so Member
// C can call it from any MPI worker on any tile, in any order.
#include "core/random.hpp"
#include "render/shading.hpp"
#include "render/image.hpp"
#include "render/tile.hpp"
#include "scene/scene.hpp"

// All knobs that define the render workload (the "N" of the experiments).
struct RenderParams {
    int width = 400, height = 300;
    int spp = 4;                 // samples per pixel (anti-aliasing)
    int max_depth = 4;           // reflection recursion cap
    int shadow_samples = 1;      // soft-shadow rays per light (area lights)
    int frame = 0, total_frames = 1;
    int tile_size = 32;          // granularity knob
};

struct Renderer {
    // Render one tile into `img`. Determinism: each sample's RNG is seeded by
    // seed_for(frame, px, py, s) — independent of tile/process, so the result
    // is identical under sequential and MPI execution.
    static void render_tile(const Scene& scene, const RenderParams& p,
                            const Tile& t, Image& img) {
        const double inv_w = 1.0 / p.width;
        const double inv_h = 1.0 / p.height;

        for (int py = t.y0; py < t.y1; ++py) {
            for (int px = t.x0; px < t.x1; ++px) {
                Color sum(0, 0, 0);
                for (int s = 0; s < p.spp; ++s) {
                    RNG rng(seed_for(p.frame, px, py, s));
                    // jitter within the pixel for AA; single-sample -> pixel center
                    double jx = (p.spp > 1) ? rng.next() : 0.5;
                    double jy = (p.spp > 1) ? rng.next() : 0.5;
                    double u = (px + jx) * inv_w;
                    // flip vertically: image row 0 is the top, camera v=0 is the bottom
                    double v = ((p.height - 1 - py) + jy) * inv_h;
                    Ray ray = scene.camera.get_ray(u, v);
                    sum += shade(scene, ray, 0, p.max_depth, p.shadow_samples, rng);
                }
                Color col = sum * (1.0 / p.spp);
                col = tone_map_reinhard(col);   // tame HDR highlights
                col = gamma_correct(col);       // linear -> display
                img.set(px, py, col);
            }
        }
    }
};
