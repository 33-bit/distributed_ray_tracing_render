// Unit-test driver for the renderer core (Members A & B).
//
// This is the project's TDD safety net: every member appends asserts here when
// they land a module, so `make test` stays green as the codebase grows. We use
// a tiny hand-rolled CHECK macro instead of a test framework to keep the build
// dependency-free (only STL + MPI are allowed).
//
// Run:  make test
#include <cstdio>
#include <cmath>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

// Floating-point approximate equality for geometry/shading checks.
[[maybe_unused]] static bool approx(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) < eps;
}

int main() {
    std::printf("Running core unit tests...\n");

    // === Member A (rendering core) tests appended in Task 2 ===
    // === Member B (shading) tests appended in Task 3 ===

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
