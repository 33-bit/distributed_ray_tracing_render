#pragma once
// Member D — Wavefront OBJ mesh import (text format).
//
// Supports what's actually needed to bring in a downloaded model: vertex
// positions ("v"), faces of any vertex count ("f", fan-triangulated), and
// per-group materials via "usemtl" (each triangle remembers which usemtl name
// was active when it was declared, so a multi-material OBJ like a castle
// with separate "roof"/"door"/"window" groups can map each group to a
// different scene material — see scene_parser.hpp's "material_map").
//
// Deliberately NOT supported: texture coordinates ("vt"), normals ("vn" —
// Triangle always derives its own flat per-face normal, same as the STL
// loader), multiple objects via "mtllib" texture files, negative relative
// vertex indices beyond the simple case below. Real-world OBJ exports (DAZ
// Studio, Blender) commonly include face lines like "v/vt/vn" — only the
// first (vertex) index of each slash-separated group is used.
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "core/vec3.hpp"

struct ObjTriangle {
    Vec3 v0, v1, v2;
    std::string group;   // the "usemtl" name active when this face was read
};

inline std::vector<ObjTriangle> load_obj(const std::string& path, double scale,
                                         const Vec3& translate) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("mesh: cannot open OBJ file: " + path);

    std::vector<Vec3> verts;
    std::vector<ObjTriangle> out;
    std::string group = "default";
    std::string line;

    auto resolve_index = [&](int idx) -> int {
        // OBJ indices are 1-based; negative means relative to the end of the
        // vertex list so far.
        if (idx > 0) return idx - 1;
        return static_cast<int>(verts.size()) + idx;
    };
    // "v", "v/vt", "v//vn", "v/vt/vn" -> the leading vertex index only.
    auto first_index = [&](const std::string& tok) -> int {
        size_t slash = tok.find('/');
        std::string head = (slash == std::string::npos) ? tok : tok.substr(0, slash);
        return resolve_index(std::stoi(head));
    };

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            double x, y, z;
            ss >> x >> y >> z;
            verts.push_back(Vec3(x, y, z) * scale + translate);
        } else if (tag == "usemtl") {
            ss >> group;
        } else if (tag == "f") {
            std::vector<int> idx;
            std::string tok;
            while (ss >> tok) idx.push_back(first_index(tok));
            if (idx.size() < 3) continue;
            for (size_t i = 1; i + 1 < idx.size(); ++i) {
                int a = idx[0], b = idx[i], c = idx[i + 1];
                if (a < 0 || a >= (int)verts.size() ||
                    b < 0 || b >= (int)verts.size() ||
                    c < 0 || c >= (int)verts.size())
                    throw std::runtime_error("mesh: OBJ face references an out-of-range vertex: " + path);
                out.push_back(ObjTriangle{verts[a], verts[b], verts[c], group});
            }
        }
        // everything else (vt, vn, mtllib, g, o, #comments, s) is ignored.
    }
    return out;
}
