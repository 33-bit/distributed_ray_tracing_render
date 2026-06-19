#pragma once
// Member A — deterministic RNG. This is the linchpin of correctness validation.
//
// Every random sample (anti-aliasing jitter, soft-shadow disk points) is drawn
// from an RNG seeded by seed_for(frame, x, y, sample) — a hash of WHERE and
// WHEN the sample is, never of WHICH process computes it. So a given pixel
// draws the identical sequence under the sequential renderer and under any MPI
// rank, which is what lets us assert MSE(seq, mpi) ~ 0 in Task 6.
//
// Generator: splitmix64. Tiny, no global state, excellent statistical quality
// for graphics sampling, and trivially reproducible from a single 64-bit seed.
#include <cstdint>
#include "core/vec3.hpp"

struct RNG {
    uint64_t state;

    explicit RNG(uint64_t seed) : state(seed) {}

    uint64_t next_u64() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // uniform double in [0,1) using the top 53 bits (full mantissa precision)
    double next() { return (next_u64() >> 11) * (1.0 / 9007199254740992.0); }

    double range(double lo, double hi) { return lo + (hi - lo) * next(); }

    // uniform point in the unit disk (z=0) — area-light / lens sampling
    Vec3 in_unit_disk() {
        for (;;) {
            Vec3 p(range(-1, 1), range(-1, 1), 0);
            if (p.length_squared() < 1.0) return p;
        }
    }

    // uniform point in the unit sphere — diffuse-ish scatter if needed
    Vec3 in_unit_sphere() {
        for (;;) {
            Vec3 p(range(-1, 1), range(-1, 1), range(-1, 1));
            if (p.length_squared() < 1.0) return p;
        }
    }
};

// Combine four integers into one 64-bit seed (boost-style hash_combine).
// Pixel-based, NOT rank-based — see the determinism note above.
inline uint64_t seed_for(int frame, int x, int y, int sample) {
    uint64_t h = 1469598103934665603ULL;   // FNV-1a offset basis
    auto mix = [&](uint64_t v) {
        h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    };
    mix(static_cast<uint32_t>(frame));
    mix(static_cast<uint32_t>(x));
    mix(static_cast<uint32_t>(y));
    mix(static_cast<uint32_t>(sample));
    return h ? h : 0x12345678ULL;   // never hand the generator a zero seed
}
