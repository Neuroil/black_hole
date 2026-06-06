//
// Created by neuroil on 2026/6/6.
//

#ifndef BLACK_HOLE_PRAC_PHYSICS_GEODESIC_H
#define BLACK_HOLE_PRAC_PHYSICS_GEODESIC_H

#include "metric.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/vector_double4.hpp>

// ── 测地线状态 ──
// pos = x^μ = (t, r, θ, φ)  Boyer-Lindquist 坐标
// mom = p^μ = (p^t, p^r, p^θ, p^φ)  四动量（对光子: p^μ p_μ = 0）

struct GeodesicState {
    glm::dvec4 pos;
    glm::dvec4 mom;
};

// ── 测地线方程右端 ──
// dx^μ/dλ = p^μ
// dp^μ/dλ = -Γ^μ_αβ p^α p^β

struct GeodesicDerivatives {
    glm::dvec4 dPos_dlambda;
    glm::dvec4 dMom_dlambda;
};

// 测地线方程右端函数（注入积分器用）
inline GeodesicDerivatives geodesic_rhs(const GeodesicState& state,
                                        const Metric& metric) {
    GeodesicDerivatives deriv;
    deriv.dPos_dlambda = state.mom;

    // dp^μ/dλ = -Γ^μ_αβ p^α p^β
    deriv.dMom_dlambda = glm::dvec4(0.0);
    for (int mu = 0; mu < 4; ++mu) {
        double sum = 0.0;
        for (int alpha = 0; alpha < 4; ++alpha) {
            for (int beta = 0; beta < 4; ++beta) {
                sum += metric.christoffel(mu, alpha, beta, state.pos)
                     * state.mom[alpha] * state.mom[beta];
            }
        }
        deriv.dMom_dlambda[mu] = -sum;
    }
    return deriv;
}

// ── 守恒量 ──
// Kerr 时空的四个守恒量（沿测地线恒定）

// 能量 E = -p_t (时间平移不变性)
inline double energy(const GeodesicState& state) {
    return -state.mom[0];  // p^t 的协变分量 = g_tμ p^μ
    // 注意: 这里返回的是协变 p_t 的负值
    // 实际计算: p_t = g_tμ p^μ，E = -p_t
}

// 角动量 L_z = p_φ (轴对称性)
inline double angular_momentum_z(const GeodesicState& state) {
    return state.mom[3];  // p^φ 的协变分量 = g_φμ p^μ
    // 实际计算: p_φ = g_φμ p^μ，L_z = p_φ
}

// ── 守恒量（需 metric 的正确版本） ──

// 协变动量 p_μ = g_μν p^ν
inline glm::dvec4 covariant_momentum(const GeodesicState& state,
                                     const Metric& metric) {
    glm::dvec4 p_dn(0.0);
    for (int mu = 0; mu < 4; ++mu) {
        for (int nu = 0; nu < 4; ++nu) {
            p_dn[mu] += metric.g_dn(mu, nu, state.pos) * state.mom[nu];
        }
    }
    return p_dn;
}

// 能量 E = -p_t（用 metric 计算协变分量）
inline double energy_conserved(const GeodesicState& state,
                               const Metric& metric) {
    glm::dvec4 p_dn = covariant_momentum(state, metric);
    return -p_dn[0];  // E = -p_t
}

// 角动量 L_z = p_φ
inline double angular_momentum_z_conserved(const GeodesicState& state,
                                           const Metric& metric) {
    glm::dvec4 p_dn = covariant_momentum(state, metric);
    return p_dn[3];   // L_z = p_φ
}

// Carter 常数 Q = p_θ² + cos²θ [a²(μ² - p_t²) + p_φ²/sin²θ]
// μ² = 0 对光子（类光测地线）
inline double carter_constant(const GeodesicState& state,
                              const Metric& metric,
                              double a) {
    glm::dvec4 p_dn = covariant_momentum(state, metric);
    double p_theta = p_dn[2];
    double p_t     = p_dn[0];
    double p_phi   = p_dn[3];
    double theta   = state.pos[2];
    double cos_t   = std::cos(theta);
    double sin_t   = std::sin(theta);

    double Q = p_theta * p_theta;

    // a²(μ² - p_t²) 项: μ²=0 for photons
    if (sin_t > 1e-15) {
        Q += cos_t * cos_t * (-a * a * p_t * p_t +
                               p_phi * p_phi / (sin_t * sin_t));
    } else {
        // sinθ→0 时 Carter 常数退化为 p_θ²
        // p_φ²/sin²θ 项有奇点，但 Kerr 赤道面测地线此处不发散
    }

    return Q;
}

#endif // BLACK_HOLE_PRAC_PHYSICS_GEODESIC_H
