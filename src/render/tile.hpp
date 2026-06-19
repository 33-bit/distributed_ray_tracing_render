#pragma once
// Member D — image-plane tiles. A Tile is a half-open pixel rectangle
// [x0,x1) x [y0,y1). Tiles are the unit of work the MPI scheduler hands out, so
// tile_size is the granularity knob in the experiments (16/32/64/128).
#include <vector>
#include <algorithm>

struct Tile {
    int x0, y0, x1, y1;

    int width()  const { return x1 - x0; }
    int height() const { return y1 - y0; }
    int pixels() const { return width() * height(); }
};

// Row-major tiling of a WxH image into ts-sized blocks (edge tiles are clipped).
inline std::vector<Tile> make_tiles(int W, int H, int ts) {
    std::vector<Tile> tiles;
    for (int y = 0; y < H; y += ts)
        for (int x = 0; x < W; x += ts)
            tiles.push_back(Tile{ x, y, std::min(x + ts, W), std::min(y + ts, H) });
    return tiles;
}
