//
// test_integrator.cpp — 积分器精度测试
// 验证: RK4 收敛阶, RK45 误差估计, RK4 vs RK45 一致性
//

#include <cassert>
#include <cmath>
#include <cstdio>
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/vector_double4.hpp>
#include "physics/kerr.h"
#include "physics/geodesic.h"
#include "physics/integrator.h"

static int tests_run  = 0;
static int tests_pass = 0;

#define TEST(name) do { tests_run++; printf("  %s ... ", name); } while(0)
#define PASS() do { tests_pass++; printf("OK\n"); } while(0)

bool check_close(double a, double b, double tol, const char* label) {
    if (std::abs(a - b) > tol) {
        printf("FAIL: %s: |%.12g - %.12g| = %.2g > %.0e\n",
               label, a, b, std::abs(a - b), tol);
        return false;
    }
    return true;
}

// ── 辅助: 建立 Schwarzschild 光子 ──
GeodesicState make_photon_schwarzschild(const Kerr& kerr,
                                         double r0, double b) {
    GeodesicState s;
    s.pos = glm::dvec4(0.0, r0, M_PI / 2.0, 0.0);
    double pt = 1.0, pp = b * pt;
    double g_rr = kerr.g_dn(1, 1, s.pos);
    double pr = -std::sqrt(std::abs(
        -(kerr.g_dn(0,0,s.pos) * pt * pt +
          kerr.g_dn(3,3,s.pos) * pp * pp) / g_rr));
    s.mom = glm::dvec4(pt, pr, 0.0, pp);
    return s;
}

// ── RK4 收敛阶测试 ──
bool test_rk4_convergence() {
    TEST("RK4 convergence order (Schwarzschild geodesic)");
    Kerr kerr(1.0, 0.0);

    GeodesicState s0 = make_photon_schwarzschild(kerr, 15.0, 3.5);

    // 参考解: 极小步长
    GeodesicState s_ref = s0;
    for (int i = 0; i < 1000; i++)
        s_ref = rk4_step(s_ref, kerr, 0.001);
    double T_ref = s_ref.pos[0];

    // 4 组步长
    double h[4]    = {0.2,  0.1,  0.05,  0.025};
    int    N[4]    = {5,    10,   20,    40};
    double errs[4];

    for (int k = 0; k < 4; k++) {
        GeodesicState s = s0;
        for (int i = 0; i < N[k]; i++)
            s = rk4_step(s, kerr, h[k]);
        errs[k] = std::abs(s.pos[0] - T_ref);
    }

    double r1 = errs[0] / errs[1];  // h×2 → expect ×16
    double r2 = errs[1] / errs[2];
    double r3 = errs[2] / errs[3];

    printf("OK (err: %.2e %.2e %.2e %.2e, ratio: %.1f %.1f %.1f)\n",
           errs[0], errs[1], errs[2], errs[3], r1, r2, r3);

    if (r1 < 8.0 || r2 < 8.0 || r3 < 8.0) {
        printf("FAIL: convergence order < 3\n");
        return false;
    }

    tests_pass++;
    return true;
}

// ── RK45 误差估计 ──
bool test_rk45_error_estimate() {
    TEST("RK45 Dormand-Prince error estimate");
    Kerr kerr(1.0, 0.0);
    GeodesicState s0 = make_photon_schwarzschild(kerr, 20.0, 4.0);

    double h = 0.5;
    RK45Result r = rk45_step(s0, kerr, h, 1e-10);

    if (r.error_estimate <= 0.0) {
        printf("FAIL: zero error estimate\n");
        return false;
    }
    if (r.next_h <= 0.0 || r.next_h > 5.0 * h) {
        printf("FAIL: next_h=%.6g\n", r.next_h);
        return false;
    }

    printf("OK (err=%.2e, next_h=%.3f)\n", r.error_estimate, r.next_h);
    tests_pass++;
    return true;
}

// ── RK4 vs RK45 一致性 ──
bool test_rk4_rk45_consistency() {
    TEST("RK4 vs RK45 consistency (small step)");
    Kerr kerr(1.0, 0.3);

    double r0 = 12.0, theta0 = M_PI / 3.0;
    GeodesicState s0;
    s0.pos = glm::dvec4(0.0, r0, theta0, 0.0);

    double pt = 1.0, pr = -0.15, ptheta = 0.02;
    double A = kerr.g_dn(3,3,s0.pos);
    double B = 2.0 * kerr.g_dn(0,3,s0.pos) * pt;
    double C = kerr.g_dn(0,0,s0.pos)*pt*pt +
               kerr.g_dn(1,1,s0.pos)*pr*pr +
               kerr.g_dn(2,2,s0.pos)*ptheta*ptheta;
    double disc = B*B - 4.0*A*C;
    double pp = (-B + std::sqrt(disc)) / (2.0*A);
    s0.mom = glm::dvec4(pt, pr, ptheta, pp);

    GeodesicState s_rk4  = rk4_step(s0, kerr, 0.01);
    RK45Result   r       = rk45_step(s0, kerr, 0.01, 1e-12);
    GeodesicState s_rk45 = r.state;

    double diff = glm::length(s_rk4.pos - s_rk45.pos) +
                  glm::length(s_rk4.mom - s_rk45.mom);

    if (diff > 1e-8) {
        printf("FAIL: mismatch %.2e\n", diff);
        return false;
    }
    printf("OK (diff=%.1e)\n", diff);
    tests_pass++;
    return true;
}

int main() {
    printf("=== test_integrator ===\n");
    bool ok = true;
    ok = test_rk4_convergence()     && ok;
    ok = test_rk45_error_estimate() && ok;
    ok = test_rk4_rk45_consistency() && ok;
    printf("\n%d/%d tests passed\n", tests_pass, tests_run);
    return ok ? 0 : 1;
}
