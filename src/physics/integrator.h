//
// Created by neuroil on 2026/6/6.
//

#ifndef BLACK_HOLE_PRAC_PHYSICS_INTEGRATOR_H
#define BLACK_HOLE_PRAC_PHYSICS_INTEGRATOR_H

#include "geodesic.h"
#include <functional>
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/vector_double4.hpp>

// ── 步进函数类型 ──
// 可替换: 默认 geodesic_rhs, 也可注入自定义 rhs
using StepFunc = std::function<GeodesicDerivatives(
    const GeodesicState&, const Metric&)>;

// ============================================================
// RK4 固定步长积分器
// ============================================================

inline GeodesicState rk4_step(const GeodesicState& state,
                              const Metric& metric,
                              double d_lambda,
                              const StepFunc& rhs = geodesic_rhs) {
    // k1
    GeodesicDerivatives k1 = rhs(state, metric);

    // k2
    GeodesicState s2;
    s2.pos = state.pos + 0.5 * d_lambda * k1.dPos_dlambda;
    s2.mom = state.mom + 0.5 * d_lambda * k1.dMom_dlambda;
    GeodesicDerivatives k2 = rhs(s2, metric);

    // k3
    GeodesicState s3;
    s3.pos = state.pos + 0.5 * d_lambda * k2.dPos_dlambda;
    s3.mom = state.mom + 0.5 * d_lambda * k2.dMom_dlambda;
    GeodesicDerivatives k3 = rhs(s3, metric);

    // k4
    GeodesicState s4;
    s4.pos = state.pos + d_lambda * k3.dPos_dlambda;
    s4.mom = state.mom + d_lambda * k3.dMom_dlambda;
    GeodesicDerivatives k4 = rhs(s4, metric);

    // 组合
    GeodesicState result;
    result.pos = state.pos + (d_lambda / 6.0) *
        (k1.dPos_dlambda + 2.0 * k2.dPos_dlambda +
         2.0 * k3.dPos_dlambda + k4.dPos_dlambda);
    result.mom = state.mom + (d_lambda / 6.0) *
        (k1.dMom_dlambda + 2.0 * k2.dMom_dlambda +
         2.0 * k3.dMom_dlambda + k4.dMom_dlambda);

    return result;
}

// ============================================================
// RK45 Dormand-Prince 自适应步长积分器
// ============================================================

struct RK45Result {
    GeodesicState state;
    double error_estimate;   // 嵌入误差估计 (L2 norm on phase space)
    double next_h;           // 建议下一步步长
};

// Dormand-Prince 8(7) 的简化 5(4) 嵌入对
// Butcher tableau 系数来自 Dormand & Prince (1980)
inline RK45Result rk45_step(const GeodesicState& state,
                            const Metric& metric,
                            double h,
                            double tolerance,
                            const StepFunc& rhs = geodesic_rhs) {
    // Dormand-Prince 5(4) Butcher 系数
    static constexpr double a2  = 1.0 / 5.0;
    static constexpr double a3  = 3.0 / 10.0;
    static constexpr double a4  = 4.0 / 5.0;
    static constexpr double a5  = 8.0 / 9.0;
    static constexpr double a6  = 1.0;
    static constexpr double a7  = 1.0;

    static constexpr double b21 = 1.0 / 5.0;

    static constexpr double b31 = 3.0 / 40.0;
    static constexpr double b32 = 9.0 / 40.0;

    static constexpr double b41 = 44.0 / 45.0;
    static constexpr double b42 = -56.0 / 15.0;
    static constexpr double b43 = 32.0 / 9.0;

    static constexpr double b51 = 19372.0 / 6561.0;
    static constexpr double b52 = -25360.0 / 2187.0;
    static constexpr double b53 = 64448.0 / 6561.0;
    static constexpr double b54 = -212.0 / 729.0;

    static constexpr double b61 = 9017.0 / 3168.0;
    static constexpr double b62 = -355.0 / 33.0;
    static constexpr double b63 = 46732.0 / 5247.0;
    static constexpr double b64 = 49.0 / 176.0;
    static constexpr double b65 = -5103.0 / 18656.0;

    static constexpr double b71 = 35.0 / 384.0;
    static constexpr double b72 = 0.0;
    static constexpr double b73 = 500.0 / 1113.0;
    static constexpr double b74 = 125.0 / 192.0;
    static constexpr double b75 = -2187.0 / 6784.0;
    static constexpr double b76 = 11.0 / 84.0;

    // 5 阶系数
    static constexpr double c1 = 35.0 / 384.0;
    static constexpr double c2 = 0.0;
    static constexpr double c3 = 500.0 / 1113.0;
    static constexpr double c4 = 125.0 / 192.0;
    static constexpr double c5 = -2187.0 / 6784.0;
    static constexpr double c6 = 11.0 / 84.0;
    static constexpr double c7 = 0.0;

    // 4 阶系数（嵌入）
    static constexpr double c1_e = 5179.0 / 57600.0;
    static constexpr double c2_e = 0.0;
    static constexpr double c3_e = 7571.0 / 16695.0;
    static constexpr double c4_e = 393.0 / 640.0;
    static constexpr double c5_e = -92097.0 / 339200.0;
    static constexpr double c6_e = 187.0 / 2100.0;
    static constexpr double c7_e = 1.0 / 40.0;

    // k1
    GeodesicDerivatives k1 = rhs(state, metric);

    // k2
    GeodesicState s2;
    s2.pos = state.pos + h * (b21 * k1.dPos_dlambda);
    s2.mom = state.mom + h * (b21 * k1.dMom_dlambda);
    GeodesicDerivatives k2 = rhs(s2, metric);

    // k3
    GeodesicState s3;
    s3.pos = state.pos + h * (b31 * k1.dPos_dlambda + b32 * k2.dPos_dlambda);
    s3.mom = state.mom + h * (b31 * k1.dMom_dlambda + b32 * k2.dMom_dlambda);
    GeodesicDerivatives k3 = rhs(s3, metric);

    // k4
    GeodesicState s4;
    s4.pos = state.pos + h * (b41 * k1.dPos_dlambda + b42 * k2.dPos_dlambda +
                               b43 * k3.dPos_dlambda);
    s4.mom = state.mom + h * (b41 * k1.dMom_dlambda + b42 * k2.dMom_dlambda +
                               b43 * k3.dMom_dlambda);
    GeodesicDerivatives k4 = rhs(s4, metric);

    // k5
    GeodesicState s5;
    s5.pos = state.pos + h * (b51 * k1.dPos_dlambda + b52 * k2.dPos_dlambda +
                               b53 * k3.dPos_dlambda + b54 * k4.dPos_dlambda);
    s5.mom = state.mom + h * (b51 * k1.dMom_dlambda + b52 * k2.dMom_dlambda +
                               b53 * k3.dMom_dlambda + b54 * k4.dMom_dlambda);
    GeodesicDerivatives k5 = rhs(s5, metric);

    // k6
    GeodesicState s6;
    s6.pos = state.pos + h * (b61 * k1.dPos_dlambda + b62 * k2.dPos_dlambda +
                               b63 * k3.dPos_dlambda + b64 * k4.dPos_dlambda +
                               b65 * k5.dPos_dlambda);
    s6.mom = state.mom + h * (b61 * k1.dMom_dlambda + b62 * k2.dMom_dlambda +
                               b63 * k3.dMom_dlambda + b64 * k4.dMom_dlambda +
                               b65 * k5.dMom_dlambda);
    GeodesicDerivatives k6 = rhs(s6, metric);

    // k7
    GeodesicState s7;
    s7.pos = state.pos + h * (b71 * k1.dPos_dlambda + b72 * k2.dPos_dlambda +
                               b73 * k3.dPos_dlambda + b74 * k4.dPos_dlambda +
                               b75 * k5.dPos_dlambda + b76 * k6.dPos_dlambda);
    s7.mom = state.mom + h * (b71 * k1.dMom_dlambda + b72 * k2.dMom_dlambda +
                               b73 * k3.dMom_dlambda + b74 * k4.dMom_dlambda +
                               b75 * k5.dMom_dlambda + b76 * k6.dMom_dlambda);
    GeodesicDerivatives k7 = rhs(s7, metric);

    // 5 阶解
    GeodesicState result;
    result.pos = state.pos + h * (c1 * k1.dPos_dlambda + c2 * k2.dPos_dlambda +
                                   c3 * k3.dPos_dlambda + c4 * k4.dPos_dlambda +
                                   c5 * k5.dPos_dlambda + c6 * k6.dPos_dlambda +
                                   c7 * k7.dPos_dlambda);
    result.mom = state.mom + h * (c1 * k1.dMom_dlambda + c2 * k2.dMom_dlambda +
                                   c3 * k3.dMom_dlambda + c4 * k4.dMom_dlambda +
                                   c5 * k5.dMom_dlambda + c6 * k6.dMom_dlambda +
                                   c7 * k7.dMom_dlambda);

    // 4 阶解 (嵌入)
    GeodesicState result_e;
    result_e.pos = state.pos + h * (c1_e * k1.dPos_dlambda +
                                     c2_e * k2.dPos_dlambda +
                                     c3_e * k3.dPos_dlambda +
                                     c4_e * k4.dPos_dlambda +
                                     c5_e * k5.dPos_dlambda +
                                     c6_e * k6.dPos_dlambda +
                                     c7_e * k7.dPos_dlambda);
    result_e.mom = state.mom + h * (c1_e * k1.dMom_dlambda +
                                     c2_e * k2.dMom_dlambda +
                                     c3_e * k3.dMom_dlambda +
                                     c4_e * k4.dMom_dlambda +
                                     c5_e * k5.dMom_dlambda +
                                     c6_e * k6.dMom_dlambda +
                                     c7_e * k7.dMom_dlambda);

    // 误差估计 (L2 norm over phase space)
    glm::dvec4 diff_pos = result.pos - result_e.pos;
    glm::dvec4 diff_mom = result.mom - result_e.mom;
    double err = std::sqrt(glm::dot(diff_pos, diff_pos) +
                           glm::dot(diff_mom, diff_mom));

    // 步长自适应 (standard PI controller)
    double safety = 0.9;
    double next_h = h;
    if (err > 0.0) {
        next_h = safety * h * std::pow(tolerance / err, 1.0 / 5.0);
    } else {
        next_h = 2.0 * h;  // 无误差时加倍
    }
    // clamp
    next_h = std::max(0.1 * h, std::min(5.0 * h, next_h));

    return {result, err, next_h};
}

#endif // BLACK_HOLE_PRAC_PHYSICS_INTEGRATOR_H
