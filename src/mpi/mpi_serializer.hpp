#pragma once
// Member C — the MPI wire format. One place that knows how a task, a tile
// result, and the global config turn into flat buffers that MPI can ship.
//
// Design note: I do NOT serialize scene geometry. The scene is a pure function
// build_demo_scene(aspect, frame, total) (Member D), so a worker reconstructs
// the exact scene for any frame from just the broadcast RenderParams. This
// avoids re-sending objects/materials every frame AND guarantees every rank
// renders bit-identically to the sequential baseline. The only things that move
// on the wire are: the config (once, broadcast), per-tile task descriptors
// (5 ints), and rendered tile pixels (RGB bytes).
#ifdef USE_MPI

#include <vector>
#include "render/tile.hpp"
#include "render/image.hpp"
#include "render/renderer.hpp"   // RenderParams

// A unit of work: which frame, which tile.
struct Task {
    int  frame;
    Tile tile;
};

namespace mpi_proto {

// --- global config (broadcast once) <-> 8 ints ---
inline void encode_params(const RenderParams& p, int out[8]) {
    out[0] = p.width;  out[1] = p.height;        out[2] = p.spp;
    out[3] = p.max_depth; out[4] = p.shadow_samples;
    out[5] = p.total_frames; out[6] = p.tile_size; out[7] = 0;  // reserved
}
inline RenderParams decode_params(const int in[8]) {
    RenderParams p;
    p.width = in[0]; p.height = in[1]; p.spp = in[2];
    p.max_depth = in[3]; p.shadow_samples = in[4];
    p.total_frames = in[5]; p.tile_size = in[6];
    return p;
}

// --- task descriptor <-> 5 ints ---
inline void encode_task(int frame, const Tile& t, int out[5]) {
    out[0] = frame; out[1] = t.x0; out[2] = t.y0; out[3] = t.x1; out[4] = t.y1;
}
inline Task decode_task(const int in[5]) {
    return Task{ in[0], Tile{ in[1], in[2], in[3], in[4] } };
}

// --- tile pixels <-> RGB byte buffer (row-major within the tile) ---
// Quantization is identical to Image::write_ppm, so master-assembled frames are
// byte-for-byte equal to what the sequential renderer would write.
inline std::vector<unsigned char> pack_tile(const Image& img, const Tile& t) {
    std::vector<unsigned char> buf;
    buf.reserve(static_cast<size_t>(t.pixels()) * 3);
    for (int y = t.y0; y < t.y1; ++y)
        for (int x = t.x0; x < t.x1; ++x) {
            const Color& c = img.get(x, y);
            buf.push_back(Image::to_byte(c.x));
            buf.push_back(Image::to_byte(c.y));
            buf.push_back(Image::to_byte(c.z));
        }
    return buf;
}
inline void unpack_tile(Image& img, const Tile& t, const unsigned char* buf) {
    size_t i = 0;
    for (int y = t.y0; y < t.y1; ++y)
        for (int x = t.x0; x < t.x1; ++x) {
            double r = buf[i++] / 255.0, g = buf[i++] / 255.0, b = buf[i++] / 255.0;
            img.set(x, y, Color(r, g, b));
        }
}

}  // namespace mpi_proto

#endif  // USE_MPI
