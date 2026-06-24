#pragma once
// Scene config loader: reads a JSON scene description and builds a Scene.
//
// Two entry points:
//   parse_scene_config(json_text)  -> SceneConfig (intermediate representation)
//   build_scene_from_config(cfg, aspect, frame, total_frames) -> Scene
//
// The builder is a pure function of (config, aspect, frame, total_frames), just
// like build_demo_scene(), so MPI workers can reconstruct the scene identically
// from the same config string.

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <algorithm>

#include "scene/json_parser.hpp"
#include "scene/scene.hpp"
#include "scene/stl_loader.hpp"
#include "scene/obj_loader.hpp"

// ---- Intermediate representation -----------------------------------------

struct OrbitConfig {
    Vec3   center = Vec3(0, 0, 0);
    double radius = 5.0;
    double height = 2.0;
    double phase  = 0.0;    // offset in radians
    double speed  = 1.0;    // orbits per animation cycle (negative = reverse)
    double tilt_deg = 0.0;  // tilt orbit plane: 0=horizontal, 90=vertical arc over sky
};

struct CameraConfig {
    Vec3   eye    = Vec3(0, 2.3, 7);
    Vec3   lookat = Vec3(0, 1, 0);
    Vec3   up     = Vec3(0, 1, 0);
    double vfov   = 40.0;
    double aperture   = 0.0;
    double focus_dist = 0.0;
    bool   has_orbit = false;
    OrbitConfig orbit;
};

struct MaterialConfig {
    std::string name;
    std::string type;       // "diffuse", "glossy", "mirror", "dielectric", "checkerboard", "emissive", "colored_glass"
    Color  albedo    = Color(0.8, 0.8, 0.8);
    Color  albedo2   = Color(0.2, 0.2, 0.2);
    double scale     = 1.0;
    double specular  = -1.0;  // -1 = use type default
    double shininess = -1.0;
    double reflectivity = 0.9;
    double ior       = 1.5;
    Color  emission  = Color(0, 0, 0);
    Color  day_emission = Color(0, 0, 0);
    Color  night_emission = Color(0, 0, 0);
    bool   has_cycle_emission = false;
    Color  absorption = Color(0, 0, 0);
    double roughness = -1.0;
};

struct ObjectMotionConfig {
    bool        enabled = false;
    std::string type;       // "linear" or "orbit"

    // Linear motion: add offset * normalized_frame_time to the object's base
    // position over the full animation.
    Vec3 offset = Vec3(0, 0, 0);

    // Orbit motion: rotate the object's base anchor around center on the XZ
    // plane. If radius/height are provided, use the same absolute orbit style
    // as camera/lights instead.
    Vec3   center = Vec3(0, 0, 0);
    double radius = 0.0;
    double height = 0.0;
    bool   has_radius = false;
    bool   has_height = false;
    double phase  = 0.0;
    double speed  = 1.0;

    // Optional self-rotation around the Y axis, in rotations per animation.
    // Useful for STL meshes: orbit moves the object through the world, spin_y
    // changes the silhouette.
    double spin_x = 0.0;
    double spin_phase_x = 0.0;
    double spin_y = 0.0;
    double spin_phase = 0.0;
    bool   has_spin_center = false;
    Vec3   spin_center = Vec3(0, 0, 0);

    // Optional mesh deformation for simple wing flapping. Coordinates are in
    // world units relative to the object's anchor after scale/translate.
    double wing_flap_deg = 0.0;
    double wing_flap_speed = 1.0;
    double wing_flap_phase = 0.0;
    double wing_start = 0.0;
    double wing_falloff = 0.4;
    double wing_hinge_x = 0.0;
    double wing_hinge_y = 0.0;
    std::string wing_axis = "z";
};

struct ObjectConfig {
    std::string type;       // "sphere", "plane", "triangle", "box", "mesh"
    std::string material;   // material name reference
    Vec3   center, point, normal;
    double radius = 1.0;
    Vec3   size = Vec3(1, 1, 1);
    Vec3   v0, v1, v2;
    // "mesh": a binary STL or a text OBJ file, loaded as a flat list of
    // Triangle objects (see scene/stl_loader.hpp, scene/obj_loader.hpp); the
    // ".obj"/".stl" extension on `path` picks the loader. `path` is resolved
    // relative to the current working directory, the same convention as the
    // --scene flag itself — every MPI rank must see the same file at the
    // same relative path (single-node/shared-filesystem only).
    //
    // OBJ files can carry several "usemtl" groups (e.g. a building's walls,
    // roof, doors, windows); `mesh_material_map` maps each OBJ group name to
    // one of this scene's own material names. A group with no entry falls
    // back to `material`. STL has no groups, so it always uses `material`.
    std::string path;
    double mesh_scale = 1.0;
    Vec3   mesh_rotate = Vec3(0, 0, 0);
    Vec3   mesh_translate = Vec3(0, 0, 0);
    std::map<std::string, std::string> mesh_material_map;
    bool   has_orbit = false;
    OrbitConfig orbit;
    ObjectMotionConfig motion;
};

struct LightConfig {
    std::string type;       // "area", "point", "spot"
    Vec3   position;
    Color  intensity = Color(1, 1, 1);
    Color  day_intensity = Color(0, 0, 0);
    Color  night_intensity = Color(0, 0, 0);
    bool   has_cycle_intensity = false;
    double radius = 0.0;
    Vec3   direction = Vec3(0, -1, 0);
    double inner_deg = 20.0;
    double outer_deg = 35.0;
    bool   attenuate = false;
    double atten_k   = 0.0;
    bool   has_orbit = false;
    OrbitConfig orbit;
};

struct DayNightConfig {
    bool enabled = false;
    Color day_top = Color(0.48, 0.55, 0.68);
    Color day_bottom = Color(0.95, 0.82, 0.62);
    Color night_top = Color(0.02, 0.025, 0.06);
    Color night_bottom = Color(0.12, 0.10, 0.16);
};

struct SceneConfig {
    Color  bg_top    = Color(0.45, 0.62, 0.95);
    Color  bg_bottom = Color(0.95, 0.95, 0.98);
    double bg_horizon = 1.0;   // see Scene::bg_horizon (scene.hpp)
    CameraConfig camera;
    DayNightConfig day_night;
    std::vector<MaterialConfig> materials;
    std::vector<ObjectConfig>   objects;
    std::vector<LightConfig>    lights;
};

// ---- Helpers -------------------------------------------------------------

inline std::string read_file_to_string(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("cannot open scene file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

namespace scene_parse_detail {

inline Vec3 to_vec3(const JsonValue& v) {
    return Vec3(v[0].as_num(), v[1].as_num(), v[2].as_num());
}

inline OrbitConfig to_orbit(const JsonValue& v) {
    OrbitConfig o;
    if (v.has("center"))   o.center   = to_vec3(v["center"]);
    if (v.has("radius"))   o.radius   = v["radius"].as_num();
    if (v.has("height"))   o.height   = v["height"].as_num();
    if (v.has("phase"))    o.phase    = v["phase"].as_num();
    if (v.has("speed"))    o.speed    = v["speed"].as_num();
    if (v.has("tilt_deg")) o.tilt_deg = v["tilt_deg"].as_num();
    return o;
}

inline CameraConfig parse_camera(const JsonValue& v) {
    CameraConfig c;
    if (v.has("eye"))    c.eye    = to_vec3(v["eye"]);
    if (v.has("lookat")) c.lookat = to_vec3(v["lookat"]);
    if (v.has("up"))     c.up     = to_vec3(v["up"]);
    if (v.has("vfov"))       c.vfov       = v["vfov"].as_num();
    if (v.has("aperture"))   c.aperture   = v["aperture"].as_num();
    if (v.has("focus_dist")) c.focus_dist = v["focus_dist"].as_num();
    if (v.has("orbit")) {
        c.has_orbit = true;
        c.orbit = to_orbit(v["orbit"]);
    }
    return c;
}

inline MaterialConfig parse_material(const std::string& name, const JsonValue& v) {
    MaterialConfig m;
    m.name = name;
    m.type = v["type"].as_str();
    if (v.has("albedo"))       m.albedo       = to_vec3(v["albedo"]);
    if (v.has("albedo2"))      m.albedo2      = to_vec3(v["albedo2"]);
    if (v.has("scale"))        m.scale        = v["scale"].as_num();
    if (v.has("specular"))     m.specular     = v["specular"].as_num();
    if (v.has("shininess"))    m.shininess    = v["shininess"].as_num();
    if (v.has("reflectivity")) m.reflectivity = v["reflectivity"].as_num();
    if (v.has("ior"))          m.ior          = v["ior"].as_num();
    if (v.has("emission"))     m.emission     = to_vec3(v["emission"]);
    if (v.has("day_emission")) {
        m.day_emission = to_vec3(v["day_emission"]);
        m.has_cycle_emission = true;
    }
    if (v.has("night_emission")) {
        m.night_emission = to_vec3(v["night_emission"]);
        m.has_cycle_emission = true;
    }
    if (v.has("absorption"))   m.absorption   = to_vec3(v["absorption"]);
    if (v.has("roughness"))    m.roughness    = v["roughness"].as_num();
    return m;
}

inline ObjectMotionConfig parse_object_motion(const JsonValue& v) {
    ObjectMotionConfig m;
    m.enabled = true;
    m.type = v.has("type") ? v["type"].as_str() : "linear";

    if (m.type == "linear") {
        if (v.has("offset"))   m.offset = to_vec3(v["offset"]);
        if (v.has("velocity")) m.offset = to_vec3(v["velocity"]);
        if (v.has("delta"))    m.offset = to_vec3(v["delta"]);
    } else if (m.type == "orbit") {
        if (v.has("center")) {
            m.center = to_vec3(v["center"]);
        }
        if (v.has("radius")) {
            m.radius = v["radius"].as_num();
            m.has_radius = true;
        }
        if (v.has("height")) {
            m.height = v["height"].as_num();
            m.has_height = true;
        }
        if (v.has("phase")) m.phase = v["phase"].as_num();
        if (v.has("speed")) m.speed = v["speed"].as_num();
    } else {
        throw std::runtime_error("scene: unknown object motion type '" + m.type + "'");
    }

    if (v.has("spin_x"))       m.spin_x = v["spin_x"].as_num();
    if (v.has("spin_phase_x")) m.spin_phase_x = v["spin_phase_x"].as_num();
    if (v.has("spin_y"))       m.spin_y = v["spin_y"].as_num();
    if (v.has("spin_phase"))   m.spin_phase = v["spin_phase"].as_num();
    if (v.has("spin_center")) {
        m.spin_center = to_vec3(v["spin_center"]);
        m.has_spin_center = true;
    }
    if (v.has("wing_flap_deg"))   m.wing_flap_deg = v["wing_flap_deg"].as_num();
    if (v.has("wing_flap_speed")) m.wing_flap_speed = v["wing_flap_speed"].as_num();
    if (v.has("wing_flap_phase")) m.wing_flap_phase = v["wing_flap_phase"].as_num();
    if (v.has("wing_start"))      m.wing_start = v["wing_start"].as_num();
    if (v.has("wing_falloff"))    m.wing_falloff = v["wing_falloff"].as_num();
    if (v.has("wing_hinge_x"))    m.wing_hinge_x = v["wing_hinge_x"].as_num();
    if (v.has("wing_hinge_y"))    m.wing_hinge_y = v["wing_hinge_y"].as_num();
    if (v.has("wing_axis"))       m.wing_axis = v["wing_axis"].as_str();

    return m;
}

inline ObjectConfig parse_object(const JsonValue& v) {
    ObjectConfig o;
    o.type = v["type"].as_str();
    o.material = v["material"].as_str();
    if (v.has("center")) o.center = to_vec3(v["center"]);
    if (v.has("radius")) o.radius = v["radius"].as_num();
    if (v.has("point"))  o.point  = to_vec3(v["point"]);
    if (v.has("normal")) o.normal = to_vec3(v["normal"]);
    if (v.has("size"))   o.size   = to_vec3(v["size"]);
    if (v.has("v0"))     o.v0     = to_vec3(v["v0"]);
    if (v.has("v1"))     o.v1     = to_vec3(v["v1"]);
    if (v.has("v2"))     o.v2     = to_vec3(v["v2"]);
    if (v.has("path"))   o.path   = v["path"].as_str();
    if (v.has("scale"))  o.mesh_scale = v["scale"].as_num();
    if (v.has("rotate")) o.mesh_rotate = to_vec3(v["rotate"]);
    if (v.has("translate")) o.mesh_translate = to_vec3(v["translate"]);
    if (v.has("material_map"))
        for (const auto& [group, mat] : v["material_map"].obj)
            o.mesh_material_map[group] = mat.as_str();
    if (v.has("orbit")) {
        o.has_orbit = true;
        o.orbit = to_orbit(v["orbit"]);
    }
    if (v.has("motion")) o.motion = parse_object_motion(v["motion"]);
    return o;
}

inline LightConfig parse_light(const JsonValue& v) {
    LightConfig l;
    l.type = v["type"].as_str();
    if (v.has("position"))  l.position  = to_vec3(v["position"]);
    if (v.has("intensity")) l.intensity = to_vec3(v["intensity"]);
    if (v.has("day_intensity")) {
        l.day_intensity = to_vec3(v["day_intensity"]);
        l.has_cycle_intensity = true;
    }
    if (v.has("night_intensity")) {
        l.night_intensity = to_vec3(v["night_intensity"]);
        l.has_cycle_intensity = true;
    }
    if (v.has("radius"))    l.radius    = v["radius"].as_num();
    if (v.has("direction")) l.direction = to_vec3(v["direction"]);
    if (v.has("inner_deg")) l.inner_deg = v["inner_deg"].as_num();
    if (v.has("outer_deg")) l.outer_deg = v["outer_deg"].as_num();
    if (v.has("attenuate")) l.attenuate = v["attenuate"].as_bool();
    if (v.has("atten_k"))   l.atten_k   = v["atten_k"].as_num();
    if (v.has("orbit")) {
        l.has_orbit = true;
        l.orbit = to_orbit(v["orbit"]);
    }
    return l;
}

inline DayNightConfig parse_day_night(const JsonValue& v) {
    DayNightConfig d;
    d.enabled = true;
    if (v.has("day_top"))      d.day_top      = to_vec3(v["day_top"]);
    if (v.has("day_bottom"))   d.day_bottom   = to_vec3(v["day_bottom"]);
    if (v.has("night_top"))    d.night_top    = to_vec3(v["night_top"]);
    if (v.has("night_bottom")) d.night_bottom = to_vec3(v["night_bottom"]);
    return d;
}

inline Vec3 rotate_y_around(const Vec3& p, const Vec3& center, double angle) {
    double s = std::sin(angle);
    double c = std::cos(angle);
    Vec3 rel = p - center;
    return Vec3(center.x + rel.x * c + rel.z * s,
                p.y,
                center.z - rel.x * s + rel.z * c);
}

inline Vec3 rotate_x_around(const Vec3& p, const Vec3& center, double angle) {
    double s = std::sin(angle);
    double c = std::cos(angle);
    Vec3 rel = p - center;
    return Vec3(p.x,
                center.y + rel.y * c - rel.z * s,
                center.z + rel.y * s + rel.z * c);
}

inline Vec3 rotate_z_around(const Vec3& p, const Vec3& center, double angle) {
    double s = std::sin(angle);
    double c = std::cos(angle);
    Vec3 rel = p - center;
    return Vec3(center.x + rel.x * c - rel.y * s,
                center.y + rel.x * s + rel.y * c,
                p.z);
}

inline Vec3 object_anchor(const ObjectConfig& oc) {
    if (oc.type == "sphere" || oc.type == "box") return oc.center;
    if (oc.type == "plane") return oc.point;
    if (oc.type == "triangle") return (oc.v0 + oc.v1 + oc.v2) / 3.0;
    if (oc.type == "mesh") return oc.mesh_translate;
    return Vec3(0, 0, 0);
}

inline Vec3 object_motion_delta(const ObjectConfig& oc, double t) {
    if (!oc.motion.enabled) return Vec3(0, 0, 0);

    if (oc.motion.type == "linear") {
        return oc.motion.offset * t;
    }

    if (oc.motion.type == "orbit") {
        Vec3 anchor = object_anchor(oc);
        double angle = oc.motion.speed * 2.0 * PI * t + oc.motion.phase;

        if (oc.motion.has_radius || oc.motion.has_height) {
            double radius = oc.motion.has_radius
                          ? oc.motion.radius
                          : std::sqrt((anchor.x - oc.motion.center.x) * (anchor.x - oc.motion.center.x) +
                                      (anchor.z - oc.motion.center.z) * (anchor.z - oc.motion.center.z));
            double height = oc.motion.has_height ? oc.motion.height : anchor.y;
            Vec3 target(oc.motion.center.x + radius * std::sin(angle),
                        height,
                        oc.motion.center.z + radius * std::cos(angle));
            return target - anchor;
        }

        return rotate_y_around(anchor, oc.motion.center, angle) - anchor;
    }

    return Vec3(0, 0, 0);
}

inline double object_spin_angle_x(const ObjectConfig& oc, double t) {
    if (!oc.motion.enabled) return 0.0;
    return oc.motion.spin_x * 2.0 * PI * t + oc.motion.spin_phase_x;
}

inline double object_spin_angle(const ObjectConfig& oc, double t) {
    if (!oc.motion.enabled) return 0.0;
    return oc.motion.spin_y * 2.0 * PI * t + oc.motion.spin_phase;
}

inline Vec3 object_spin_center(const ObjectConfig& oc, const Vec3& motion) {
    return oc.motion.has_spin_center ? oc.motion.spin_center : object_anchor(oc) + motion;
}

inline Vec3 apply_wing_flap(const ObjectConfig& oc, const Vec3& p, const Vec3& anchor,
                            double t) {
    if (!oc.motion.enabled || oc.motion.wing_flap_deg == 0.0) return p;

    double rel_x = p.x - anchor.x;
    double side = (rel_x >= 0.0) ? 1.0 : -1.0;
    double outboard = std::fabs(rel_x);
    double falloff = std::max(oc.motion.wing_falloff, 1e-6);
    double weight = smoothstep(oc.motion.wing_start,
                               oc.motion.wing_start + falloff,
                               outboard);
    if (weight <= 0.0) return p;

    double flap = oc.motion.wing_flap_deg * PI / 180.0 *
                  std::sin(oc.motion.wing_flap_speed * 2.0 * PI * t +
                           oc.motion.wing_flap_phase);
    Vec3 hinge(anchor.x + side * oc.motion.wing_hinge_x,
               anchor.y + oc.motion.wing_hinge_y,
               anchor.z);
    double angle = side * flap * weight;
    if (oc.motion.wing_axis == "y") return rotate_y_around(p, hinge, angle);
    return rotate_z_around(p, hinge, angle);
}

} // namespace scene_parse_detail

// ---- Public API ----------------------------------------------------------

inline SceneConfig parse_scene_config(const std::string& json_text) {
    using namespace scene_parse_detail;
    JsonValue root = JsonParser::parse(json_text);
    SceneConfig cfg;

    if (root.has("background")) {
        const auto& bg = root["background"];
        if (bg.has("top"))     cfg.bg_top     = to_vec3(bg["top"]);
        if (bg.has("bottom"))  cfg.bg_bottom  = to_vec3(bg["bottom"]);
        if (bg.has("horizon")) cfg.bg_horizon = bg["horizon"].as_num();
    }
    if (root.has("camera")) {
        cfg.camera = parse_camera(root["camera"]);
    }
    if (root.has("day_night")) {
        cfg.day_night = parse_day_night(root["day_night"]);
    }
    if (root.has("materials")) {
        const auto& mats = root["materials"];
        for (const auto& [name, val] : mats.obj)
            cfg.materials.push_back(parse_material(name, val));
    }
    if (root.has("objects")) {
        for (const auto& o : root["objects"].arr)
            cfg.objects.push_back(parse_object(o));
    }
    if (root.has("lights")) {
        for (const auto& l : root["lights"].arr)
            cfg.lights.push_back(parse_light(l));
    }
    return cfg;
}

inline Scene build_scene_from_config(const SceneConfig& cfg, double aspect,
                                     int frame, int total_frames) {
    Scene s;
    s.bg_top     = cfg.bg_top;
    s.bg_bottom  = cfg.bg_bottom;
    s.bg_horizon = cfg.bg_horizon;

    // Normalized animation time
    double t = (total_frames > 1) ? static_cast<double>(frame) / total_frames : 0.0;
    double night_mix = cfg.day_night.enabled ? 0.5 - 0.5 * std::cos(2.0 * PI * t) : 0.0;
    double day_mix = 1.0 - night_mix;
    if (cfg.day_night.enabled) {
        s.bg_top = lerp(cfg.day_night.day_top, cfg.day_night.night_top, night_mix);
        s.bg_bottom = lerp(cfg.day_night.day_bottom, cfg.day_night.night_bottom, night_mix);
    }

    // --- Materials (build name -> index map) ---
    std::map<std::string, int> mat_map;
    for (const auto& mc : cfg.materials) {
        Material m;
        if (mc.type == "diffuse") {
            m = Material::diffuse(mc.albedo);
            if (mc.specular  >= 0) m.specular  = mc.specular;
            if (mc.shininess >= 0) m.shininess = mc.shininess;
        } else if (mc.type == "glossy") {
            double spec = (mc.specular >= 0)  ? mc.specular  : 0.6;
            double shin = (mc.shininess >= 0) ? mc.shininess : 48.0;
            m = Material::glossy(mc.albedo, spec, shin);
        } else if (mc.type == "mirror") {
            m = Material::mirror(mc.albedo, mc.reflectivity);
        } else if (mc.type == "dielectric") {
            m = Material::dielectric(mc.ior, mc.albedo);
        } else if (mc.type == "colored_glass") {
            m = Material::colored_glass(mc.ior, mc.absorption);
        } else if (mc.type == "checkerboard") {
            m = Material::checkerboard(mc.albedo, mc.albedo2, mc.scale);
        } else if (mc.type == "emissive") {
            Color emission = mc.has_cycle_emission
                           ? mc.day_emission * day_mix + mc.night_emission * night_mix
                           : mc.emission;
            m = Material::emissive(emission);
        } else {
            // Unknown type, fall back to diffuse
            m = Material::diffuse(mc.albedo);
        }
        if (mc.roughness >= 0) m.roughness = mc.roughness;
        int idx = s.add_material(m);
        mat_map[mc.name] = idx;
    }

    auto resolve_mat = [&](const std::string& name) -> int {
        auto it = mat_map.find(name);
        if (it == mat_map.end())
            throw std::runtime_error("scene: unknown material '" + name + "'");
        return it->second;
    };

    // --- Objects ---
    for (const auto& oc : cfg.objects) {
        int mi = resolve_mat(oc.material);
        Vec3 motion = scene_parse_detail::object_motion_delta(oc, t);
        double spin_x_angle = scene_parse_detail::object_spin_angle_x(oc, t);
        double spin_angle = scene_parse_detail::object_spin_angle(oc, t);
        Vec3 spin_center = scene_parse_detail::object_spin_center(oc, motion);
        Vec3 anchor = scene_parse_detail::object_anchor(oc) + motion;
        auto add_deformed_triangle = [&](Vec3 a, Vec3 b, Vec3 c, int mat) {
            a = scene_parse_detail::apply_wing_flap(oc, a, anchor, t);
            b = scene_parse_detail::apply_wing_flap(oc, b, anchor, t);
            c = scene_parse_detail::apply_wing_flap(oc, c, anchor, t);
            if (spin_x_angle != 0.0) {
                a = scene_parse_detail::rotate_x_around(a, spin_center, spin_x_angle);
                b = scene_parse_detail::rotate_x_around(b, spin_center, spin_x_angle);
                c = scene_parse_detail::rotate_x_around(c, spin_center, spin_x_angle);
            }
            if (spin_angle != 0.0) {
                a = scene_parse_detail::rotate_y_around(a, spin_center, spin_angle);
                b = scene_parse_detail::rotate_y_around(b, spin_center, spin_angle);
                c = scene_parse_detail::rotate_y_around(c, spin_center, spin_angle);
            }
            s.add_triangle(a, b, c, mat);
        };
        if (oc.type == "sphere") {
            Vec3 center = oc.center;
            if (oc.has_orbit) {
                double angle = oc.orbit.speed * 2.0 * PI * t + oc.orbit.phase;
                double tilt  = oc.orbit.tilt_deg * PI / 180.0;
                double local_x = oc.orbit.radius * std::sin(angle);
                double local_y = oc.orbit.radius * std::cos(angle);
                center = Vec3(oc.orbit.center.x + local_x,
                              oc.orbit.height   + local_y * std::sin(tilt),
                              oc.orbit.center.z + local_y * std::cos(tilt));
            }
            s.add_sphere(center + motion, oc.radius, mi);
        } else if (oc.type == "plane") {
            s.add_plane(oc.point + motion, oc.normal, mi);
        } else if (oc.type == "triangle") {
            add_deformed_triangle(oc.v0 + motion, oc.v1 + motion, oc.v2 + motion, mi);
        } else if (oc.type == "box") {
            s.add_box(oc.center + motion, oc.size, mi);
        } else if (oc.type == "mesh") {
            bool is_obj = oc.path.size() >= 4 &&
                          (oc.path.compare(oc.path.size() - 4, 4, ".obj") == 0 ||
                           oc.path.compare(oc.path.size() - 4, 4, ".OBJ") == 0);
            if (is_obj) {
                for (const ObjTriangle& t : load_obj(oc.path, oc.mesh_scale, oc.mesh_translate + motion)) {
                    auto it = oc.mesh_material_map.find(t.group);
                    int gi = (it != oc.mesh_material_map.end()) ? resolve_mat(it->second) : mi;
                    add_deformed_triangle(t.v0, t.v1, t.v2, gi);
                }
            } else {
                for (const StlTriangle& t : load_stl(oc.path, oc.mesh_scale, oc.mesh_rotate, oc.mesh_translate + motion)) {
                    add_deformed_triangle(t.v0, t.v1, t.v2, mi);
                }
            }
        } else {
            throw std::runtime_error("scene: unknown object type '" + oc.type + "'");
        }
    }

    // --- Camera (with optional orbit animation) ---
    if (cfg.camera.has_orbit) {
        const auto& orb = cfg.camera.orbit;
        double angle = orb.speed * 2.0 * PI * t + orb.phase;
        Vec3 eye(orb.center.x + orb.radius * std::sin(angle),
                 orb.height,
                 orb.center.z + orb.radius * std::cos(angle));
        s.camera = Camera(eye, cfg.camera.lookat, cfg.camera.up, cfg.camera.vfov, aspect,
                          cfg.camera.aperture, cfg.camera.focus_dist);
    } else {
        s.camera = Camera(cfg.camera.eye, cfg.camera.lookat, cfg.camera.up,
                          cfg.camera.vfov, aspect,
                          cfg.camera.aperture, cfg.camera.focus_dist);
    }

    // --- Lights (with optional orbit animation) ---
    for (const auto& lc : cfg.lights) {
        Vec3 pos = lc.position;
        Color intensity = lc.has_cycle_intensity
                        ? lc.day_intensity * day_mix + lc.night_intensity * night_mix
                        : lc.intensity;
        if (lc.has_orbit) {
            double angle = lc.orbit.speed * 2.0 * PI * t + lc.orbit.phase;
            double tilt  = lc.orbit.tilt_deg * PI / 180.0;
            // local orbit coords: u = along orbit, v = perpendicular in tilt plane
            // horizontal (tilt=0): x=sin, y=0, z=cos
            // vertical   (tilt=90): x=sin, y=cos, z=0
            double local_x = lc.orbit.radius * std::sin(angle);
            double local_y = lc.orbit.radius * std::cos(angle);
            // Rotate tilt around X-axis: tilts the orbit plane from XZ up to XY
            pos = Vec3(lc.orbit.center.x + local_x,
                       lc.orbit.height   + local_y * std::sin(tilt),
                       lc.orbit.center.z + local_y * std::cos(tilt));
        }
        if (lc.type == "spot") {
            Light l = Light::spot(pos, intensity, lc.direction,
                                  lc.inner_deg, lc.outer_deg, lc.radius);
            if (lc.attenuate) l.with_attenuation(lc.atten_k);
            s.lights_.push_back(l);
        } else {
            // "area" or "point" (point is just area with radius=0)
            Light l = Light{pos, intensity, lc.radius};
            if (lc.attenuate) l.with_attenuation(lc.atten_k);
            s.lights_.push_back(l);
        }
    }

    return s;
}
