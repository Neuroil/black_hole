//
// Created by neuroil on 2026/6/6.
//

#ifndef BLACK_HOLE_PRAC_PHYSICS_METRIC_H
#define BLACK_HOLE_PRAC_PHYSICS_METRIC_H

#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/vector_double4.hpp>

// ── 度规纯虚接口 ──
// 所有具体度规（Kerr, Schwarzschild, Kerr-Newman 等）实现此接口。
// 坐标约定: pos = (t, r, θ, φ) in Boyer-Lindquist 坐标
// 指标约定: 希腊指标 μ,ν,α,β ∈ {0,1,2,3} 对应 {t,r,θ,φ}
// 符号约定: (-,+,+,+) — timelike 为负

class Metric {
public:
    virtual ~Metric() = default;

    // 协变度规分量 g_μν （下标）
    virtual double g_dn(int mu, int nu, const glm::dvec4& pos) const = 0;

    // 逆变度规分量 g^μν （上标）
    virtual double g_up(int mu, int nu, const glm::dvec4& pos) const = 0;

    // Christoffel 符号 Γ^μ_αβ
    // 定义: Γ^μ_αβ = (1/2) g^μν (∂_α g_νβ + ∂_β g_να - ∂_ν g_αβ)
    virtual double christoffel(int mu, int alpha, int beta,
                               const glm::dvec4& pos) const = 0;
};

#endif // BLACK_HOLE_PRAC_PHYSICS_METRIC_H
