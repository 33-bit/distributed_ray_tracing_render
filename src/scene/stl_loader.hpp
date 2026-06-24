#pragma once
// Member D — binary STL mesh import. STL is the simplest mesh format there
// is: a flat list of triangles, each already carrying its own face normal
// (no shared vertices, no indexing) — which happens to match this renderer's
// Triangle primitive exactly (flat per-face normal, no smooth shading). That
// is why STL, not OBJ/FBX/etc, is the format this loader supports.
//
// Binary STL layout (little-endian):
//   80 bytes   header (ignored)
//   4 bytes    uint32 triangle count N
//   N * 50 bytes:
//     12 bytes   face normal (3 floats, ignored — Triangle recomputes it)
//     36 bytes   3 vertices  (3 floats each)
//     2 bytes    attribute byte count (ignored)
//
// ASCII STL ("solid ..." text) is not supported — meshes from free model
// sites are overwhelmingly binary; rejecting ASCII with a clear error beats
// silently mis-parsing it as binary.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>
#include "core/vec3.hpp"

struct StlTriangle {
    Vec3 v0, v1, v2;
};

// Reads `path`, applies a uniform scale then a translation to every vertex
// (mesh-local space -> world space): world = local * scale + translate.
inline std::vector<StlTriangle> load_stl(const std::string& path, double scale,
                                         const Vec3& translate) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("mesh: cannot open STL file: " + path);

    char header[80];
    f.read(header, 80);
    if (f.gcount() != 80)
        throw std::runtime_error("mesh: STL file too short (truncated header): " + path);
    if (header[0] == 's' && header[1] == 'o' && header[2] == 'l' && header[3] == 'i' && header[4] == 'd')
        throw std::runtime_error("mesh: ASCII STL is not supported, re-export as binary STL: " + path);

    uint32_t count = 0;
    f.read(reinterpret_cast<char*>(&count), 4);
    if (!f)
        throw std::runtime_error("mesh: STL file too short (truncated triangle count): " + path);

    std::vector<StlTriangle> out;
    out.reserve(count);

    auto read_vec3 = [&]() -> Vec3 {
        float v[3];
        f.read(reinterpret_cast<char*>(v), 12);
        if (!f) throw std::runtime_error("mesh: STL file truncated mid-triangle: " + path);
        return Vec3(static_cast<double>(v[0]), static_cast<double>(v[1]), static_cast<double>(v[2]));
    };

    for (uint32_t i = 0; i < count; ++i) {
        Vec3 n = read_vec3();   // face normal — ignored, Triangle derives its own
        (void)n;
        Vec3 a = read_vec3() * scale + translate;
        Vec3 b = read_vec3() * scale + translate;
        Vec3 c = read_vec3() * scale + translate;
        uint16_t attr = 0;
        f.read(reinterpret_cast<char*>(&attr), 2);
        if (!f) throw std::runtime_error("mesh: STL file truncated mid-triangle: " + path);
        out.push_back(StlTriangle{a, b, c});
    }
    return out;
}
