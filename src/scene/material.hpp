#pragma once
// Member B — surface material. A small parameter bag that the shader reads;
// the `type` tag drives the high-level branch (emissive short-circuits, others
// run the Phong + reflection path) and is what Member C serializes over MPI.
#include "core/color.hpp"

enum class MatType { Diffuse, Specular, Reflective, Emissive };

struct Material {
    MatType type        = MatType::Diffuse;
    Color   albedo      = Color(0.8, 0.8, 0.8);  // base color
    double  specular    = 0.0;                   // Phong specular strength
    double  shininess   = 32.0;                  // Phong exponent
    double  reflectivity = 0.0;                  // mirror fraction in [0,1]
    Color   emission    = Color(0, 0, 0);        // self-emitted light

    // Named constructors keep scene-building readable in Member D's code.
    static Material diffuse(const Color& a) {
        Material m; m.type = MatType::Diffuse; m.albedo = a;
        m.specular = 0.3; m.shininess = 16.0; return m;
    }
    static Material glossy(const Color& a, double spec, double shin) {
        Material m; m.type = MatType::Specular; m.albedo = a;
        m.specular = spec; m.shininess = shin; return m;
    }
    static Material mirror(const Color& a, double refl) {
        Material m; m.type = MatType::Reflective; m.albedo = a;
        m.reflectivity = refl; m.specular = 0.5; m.shininess = 64.0; return m;
    }
    static Material emissive(const Color& e) {
        Material m; m.type = MatType::Emissive; m.emission = e; return m;
    }
};
