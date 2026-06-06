//
// test_kerr.cpp — Kerr 度规定点测试
// 验证: 度规对称性, 行列式符号, 史瓦西极限, 光子球/ISCO 对标
//

#include <cassert>
#include <cmath>
#include <cstdio>
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/vector_double4.hpp>
#include "physics/kerr.h"

static int tests_run  = 0;
static int tests_pass = 0;

#define TEST(name) do { tests_run++; printf("  %s ... ", name); } while(0)
#define PASS() do { tests_pass++; printf("OK\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return false; } while(0)

bool check_close(double a, double b, double tol, const char* label) {
    if (std::abs(a - b) > tol) {
        printf("FAIL: %s: |%.12g - %.12g| = %.2g > %.0e\n",
               label, a, b, std::abs(a - b), tol);
        return false;
    }
    return true;
}

// ── 度规对称性 ──
bool test_metric_symmetry() {
    TEST("g_dn symmetry (M=1, a=0.5)");
    Kerr kerr(1.0, 0.5);
    glm::dvec4 pos(0.0, 4.0, 1.2, 0.5);
    for (int mu = 0; mu < 4; mu++) {
        for (int nu = 0; nu < 4; nu++) {
            double g1 = kerr.g_dn(mu, nu, pos);
            double g2 = kerr.g_dn(nu, mu, pos);
            if (std::abs(g1 - g2) > 1e-15) {
                printf("FAIL: g_dn[%d][%d] != g_dn[%d][%d]: %.15g vs %.15g\n",
                       mu, nu, nu, mu, g1, g2);
                return false;
            }
        }
    }
    PASS();
    return true;
}

bool test_inverse_identity() {
    TEST("g^μν g_να = δ^μ_α (M=1, a=0.9)");
    Kerr kerr(1.0, 0.9);
    glm::dvec4 pos(0.0, 5.0, 1.0, 0.8);
    for (int mu = 0; mu < 4; mu++) {
        for (int alpha = 0; alpha < 4; alpha++) {
            double sum = 0.0;
            for (int nu = 0; nu < 4; nu++) {
                sum += kerr.g_up(mu, nu, pos) * kerr.g_dn(nu, alpha, pos);
            }
            double expected = (mu == alpha) ? 1.0 : 0.0;
            if (std::abs(sum - expected) > 1e-12) {
                printf("FAIL: δ[%d][%d] = %.12g, expected %.1f\n",
                       mu, alpha, sum, expected);
                return false;
            }
        }
    }
    PASS();
    return true;
}

// ── 史瓦西极限 (a=0) ──
bool test_schwarzschild_limit() {
    TEST("Schwarzschild limit (a=0, M=1)");
    Kerr kerr(1.0, 0.0);
    glm::dvec4 pos(0.0, 3.0, M_PI / 2.0, 0.0);
    double r = 3.0;

    // g_tt = -(1 - 2M/r)
    double gtt = kerr.g_dn(0, 0, pos);
    double expected_gtt = -(1.0 - 2.0 / r);
    if (!check_close(gtt, expected_gtt, 1e-15, "g_tt")) return false;

    // g_rr = (1 - 2M/r)^{-1}
    double grr = kerr.g_dn(1, 1, pos);
    double expected_grr = 1.0 / (1.0 - 2.0 / r);
    if (!check_close(grr, expected_grr, 1e-15, "g_rr")) return false;

    // g_φφ = r² sin²θ
    double gpp = kerr.g_dn(3, 3, pos);
    double expected_gpp = r * r * 1.0;  // sin²(π/2) = 1
    if (!check_close(gpp, expected_gpp, 1e-15, "g_φφ")) return false;

    // g_tφ = 0 (a=0)
    double gtp = kerr.g_dn(0, 3, pos);
    if (!check_close(gtp, 0.0, 1e-15, "g_tφ")) return false;

    PASS();
    return true;
}

// ── 视界半径 ──
bool test_horizon_radii() {
    TEST("horizon radii");

    // a=0: r_+ = 2M, r_- = 0
    Kerr kerr0(2.0, 0.0);
    if (!check_close(kerr0.horizon_radius(), 4.0, 1e-14, "r_+ (M=2,a=0)"))
        return false;
    if (!check_close(kerr0.inner_horizon_radius(), 0.0, 1e-14, "r_- (M=2,a=0)"))
        return false;

    // a=M (极端): r_+ = r_- = M
    Kerr kerr_ext(3.0, 3.0);
    if (!check_close(kerr_ext.horizon_radius(), 3.0, 1e-14, "r_+ (M=3,a=3)"))
        return false;
    if (!check_close(kerr_ext.inner_horizon_radius(), 3.0, 1e-14, "r_- (M=3,a=3)"))
        return false;

    // a=0.6M, M=1: r_+ = 1 + sqrt(1 - 0.36) = 1.8
    Kerr kerr_mid(1.0, 0.6);
    if (!check_close(kerr_mid.horizon_radius(), 1.8, 1e-14, "r_+ (M=1,a=0.6)"))
        return false;

    PASS();
    return true;
}

// ── 光子球半径对标 ──
bool test_photon_sphere() {
    TEST("photon sphere radii");

    // a=0 (Schwarzschild): r_ph = 3M
    Kerr kerr0(2.0, 0.0);
    if (!check_close(kerr0.photon_sphere_prograde(), 6.0, 1e-12,
                     "r_ph prograde (M=2,a=0)"))
        return false;
    if (!check_close(kerr0.photon_sphere_retrograde(), 6.0, 1e-12,
                     "r_ph retrograde (M=2,a=0)"))
        return false;

    // a=0.5M, M=1: prograde < 3M < retrograde
    Kerr kerr_half(1.0, 0.5);
    double rp = kerr_half.photon_sphere_prograde();
    double rr = kerr_half.photon_sphere_retrograde();
    if (!(rp < 3.0 && 3.0 < rr)) {
        printf("FAIL: photon sphere ordering: prograde=%.6f, retrograde=%.6f\n",
               rp, rr);
        return false;
    }

    PASS();
    return true;
}

// ── ISCO 对标 ──
bool test_isco() {
    TEST("ISCO radii");

    // a=0: ISCO = 6M
    Kerr kerr0(1.0, 0.0);
    if (!check_close(kerr0.isco_prograde(), 6.0, 1e-12, "ISCO prograde (a=0)"))
        return false;
    if (!check_close(kerr0.isco_retrograde(), 6.0, 1e-12, "ISCO retrograde (a=0)"))
        return false;

    // a→M extreme: prograde ISCO → M, retrograde ISCO → 9M
    Kerr kerr_ext(1.0, 0.999);
    double rp = kerr_ext.isco_prograde();
    double rr = kerr_ext.isco_retrograde();
    if (!(rp < 3.0)) {  // prograde < 3M (should be near 1M)
        printf("FAIL: ISCO prograde (a≈M) = %.6f, expected < 3\n", rp);
        return false;
    }
    if (!(rr > 6.0)) {  // retrograde > 6M (should be near 9M)
        printf("FAIL: ISCO retrograde (a≈M) = %.6f, expected > 6\n", rr);
        return false;
    }

    PASS();
    return true;
}

// ── 数值 Christoffel 交叉验证 ──
bool test_christoffel_crosscheck() {
    TEST("christoffel numerical self-consistency (M=1, a=0.7)");
    Kerr kerr(1.0, 0.7);
    glm::dvec4 pos(0.0, 4.0, 1.3, 0.6);

    // 数值 Christoffel 应满足对称性：Γ^μ_αβ = Γ^μ_βα
    int violations = 0;
    for (int mu = 0; mu < 4; mu++) {
        for (int alpha = 0; alpha < 4; alpha++) {
            for (int beta = alpha + 1; beta < 4; beta++) {
                double c1 = kerr.christoffel(mu, alpha, beta, pos);
                double c2 = kerr.christoffel(mu, beta, alpha, pos);
                if (std::abs(c1 - c2) > 1e-12) {
                    violations++;
                }
            }
        }
    }
    if (violations > 0) {
        printf("FAIL: %d symmetry violations\n", violations);
        return false;
    }
    PASS();
    return true;
}

// ── 守恒量在圆轨道上应成立 ──
bool test_circular_orbit_conservation() {
    TEST("Christoffel circular orbit (a=0, r=10M)");

    Kerr kerr(1.0, 0.0);
    double r = 10.0;
    glm::dvec4 pos(0.0, r, M_PI / 2.0, 0.0);
    double Gamma_r_tt = kerr.christoffel(1, 0, 0, pos);
    double Gamma_r_pp = kerr.christoffel(1, 3, 3, pos);

    double Omega2 = 1.0 / (r * r * r);
    double accel = Gamma_r_tt + Gamma_r_pp * Omega2;

    // 数值 Christoffel 容忍度放宽到 1e-8
    if (std::abs(accel) > 1e-8) {
        printf("FAIL: radial accel = %.2e > 1e-8\n", std::abs(accel));
        return false;
    }
    PASS();
    return true;
}

int main() {
    printf("=== test_kerr ===\n");

    bool ok = true;
    ok = test_metric_symmetry()          && ok;
    ok = test_inverse_identity()         && ok;
    ok = test_schwarzschild_limit()      && ok;
    ok = test_horizon_radii()            && ok;
    ok = test_photon_sphere()            && ok;
    ok = test_isco()                     && ok;
    ok = test_christoffel_crosscheck()   && ok;
    ok = test_circular_orbit_conservation() && ok;

    printf("\n%d/%d tests passed\n", tests_pass, tests_run);
    return ok ? 0 : 1;
}
