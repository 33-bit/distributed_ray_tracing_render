#pragma once
// Member D — RGB framebuffer + binary PPM (P6) writer.
//
// Pixels are stored row-major with (0,0) at the TOP-LEFT, matching PPM's
// top-to-bottom scan order; the renderer flips the vertical axis when it maps a
// pixel row to a camera ray. Colors are assumed already tone-mapped + gamma-
// corrected (the renderer does that), so the writer just quantizes to 8-bit.
#include <vector>
#include <string>
#include <fstream>
#include "core/color.hpp"

struct Image {
    int w, h;
    std::vector<Color> pixels;

    Image(int width, int height) : w(width), h(height), pixels(width * height, Color(0, 0, 0)) {}

    void        set(int x, int y, const Color& c) { pixels[y * w + x] = c; }
    const Color& get(int x, int y) const          { return pixels[y * w + x]; }

    static unsigned char to_byte(double v) {
        return static_cast<unsigned char>(clampd(v, 0.0, 1.0) * 255.0 + 0.5);
    }

    void write_ppm(const std::string& path) const {
        std::ofstream out(path, std::ios::binary);
        out << "P6\n" << w << " " << h << "\n255\n";
        for (const Color& c : pixels) {
            out.put(static_cast<char>(to_byte(c.x)));
            out.put(static_cast<char>(to_byte(c.y)));
            out.put(static_cast<char>(to_byte(c.z)));
        }
    }
};
