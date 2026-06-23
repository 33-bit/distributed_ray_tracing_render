#pragma once
// Member B — surface material. A small parameter bag that the shader reads;
// the `type` tag drives the high-level branch (emissive short-circuits, others
// run the Phong + reflection path) and is what Member C serializes over MPI.
#include "core/color.hpp"

enum class MatType { Diffuse, Specular, Reflective, Emissive, Dielectric };

struct Material {
    MatType type        = MatType::Diffuse;
    Color   albedo      = Color(0.8, 0.8, 0.8);  // base color / glass tint
    double  specular    = 0.0;                   // Phong specular strength
    double  shininess   = 32.0;                  // Phong exponent
    double  reflectivity = 0.0;                  // mirror fraction in [0,1]
    Color   emission    = Color(0, 0, 0);        // self-emitted light
    double  ior         = 1.5;                   // index of refraction (Dielectric)
    Color   absorption  = Color(0, 0, 0);        // Beer-Lambert absorption/unit length (tinted glass)
    double  roughness   = 0.0;                   // reflection blur (0=mirror, >0=frosted)

    // Procedural checkerboard: when set, the diffuse albedo alternates between
    // `albedo` and `albedo2` in world-space cells of size 1/checker_scale.
    bool    checker      = false;
    double  checker_scale = 1.0;
    Color   albedo2     = Color(0.2, 0.2, 0.2);

    // Named constructors keep scene-building readable in Member D's code.
    static Material diffuse(const Color& a) {
        Material m; m.type = MatType::Diffuse; m.albedo = a;
        m.specular = 0.3; m.shininess = 16.0; return m;
    }
    static Material glossy(const Color& a, double spec, double shin) {
        Material m; m.type = MatType::Specular; m.albedo = a;
        m.specular = spec; m.shininess = shin;
        m.reflectivity = spec * 0.3;
        m.roughness = 1.0 / (1.0 + shin / 8.0);
        return m;
    }
    static Material mirror(const Color& a, double refl) {
        Material m; m.type = MatType::Reflective; m.albedo = a;
        m.reflectivity = refl; m.specular = 0.5; m.shininess = 64.0; return m;
    }
    static Material emissive(const Color& e) {
        Material m; m.type = MatType::Emissive; m.emission = e; return m;
    }
    static Material checkerboard(const Color& a, const Color& b, double scale) {
        Material m; m.type = MatType::Diffuse; m.albedo = a; m.albedo2 = b;
        m.checker = true; m.checker_scale = scale;
        m.specular = 0.1; m.shininess = 8.0; return m;
    }
    static Material dielectric(double index_of_refraction, const Color& tint = Color(1, 1, 1)) {
        Material m; m.type = MatType::Dielectric; m.albedo = tint;
        m.ior = index_of_refraction; return m;
    }
    static Material colored_glass(double index_of_refraction, const Color& absorption) {
        Material m; m.type = MatType::Dielectric; m.ior = index_of_refraction;
        m.absorption = absorption; return m;   // Beer-Lambert tint by path length
    }
};
