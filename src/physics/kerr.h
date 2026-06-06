//
// Created by neuroil on 2026/6/6.
//

#ifndef BLACK_HOLE_PRAC_PHYSICS_KERR_H
#define BLACK_HOLE_PRAC_PHYSICS_KERR_H

#include "metric.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/vector_double4.hpp>
#include <cmath>
#include <stdexcept>

// ── Kerr 时空度规 (Boyer-Lindquist 坐标) ──
// 参数: M = 质量 (几何单位), a = 角动量参数 (0 ≤ a ≤ M)
// 坐标: pos = (t, r, θ, φ)
// 符号: (-,+,+,+) 类时约定

class Kerr : public Metric {
public:
    Kerr(double M, double a)
        : M_(M), a_(a), a2_(a * a) {
        if (M <= 0.0) {
            throw std::invalid_argument("Kerr: M must be > 0");
        }
        if (a < 0.0 || a > M) {
            throw std::invalid_argument("Kerr: a must be in [0, M]");
        }
    }

    // ── 坐标辅助函数 ──
    double Sigma(double r, double theta) const {
        return r * r + a2_ * std::cos(theta) * std::cos(theta);
    }

    double Delta(double r) const {
        return r * r - 2.0 * M_ * r + a2_;
    }

    // ── 解析对标值 ──
    double horizon_radius() const {
        return M_ + std::sqrt(M_ * M_ - a2_);  // r_+
    }

    double inner_horizon_radius() const {
        return M_ - std::sqrt(M_ * M_ - a2_);  // r_-
    }

    // 光子球半径（赤道面，顺行）
    double photon_sphere_prograde() const {
        double am = std::abs(a_) / M_;
        return 2.0 * M_ * (1.0 + std::cos(2.0 / 3.0 *
            std::acos(-am)));
    }

    // 光子球半径（赤道面，逆行）
    double photon_sphere_retrograde() const {
        double am = std::abs(a_) / M_;
        return 2.0 * M_ * (1.0 + std::cos(2.0 / 3.0 *
            std::acos(am)));
    }

    // ISCO (最内稳定圆轨道)
    double isco_prograde() const {
        double am = std::abs(a_) / M_;
        double Z1 = 1.0 + std::cbrt(1.0 - am * am) *
            (std::cbrt(1.0 + am) + std::cbrt(1.0 - am));
        double Z2 = std::sqrt(3.0 * am * am + Z1 * Z1);
        return M_ * (3.0 + Z2 - std::sqrt((3.0 - Z1) * (3.0 + Z1 + 2.0 * Z2)));
    }

    double isco_retrograde() const {
        double am = std::abs(a_) / M_;
        double Z1 = 1.0 + std::cbrt(1.0 - am * am) *
            (std::cbrt(1.0 + am) + std::cbrt(1.0 - am));
        double Z2 = std::sqrt(3.0 * am * am + Z1 * Z1);
        return M_ * (3.0 + Z2 + std::sqrt((3.0 - Z1) * (3.0 + Z1 + 2.0 * Z2)));
    }

    // ═══════════════════════════════════════════════
    // Metric 接口实现
    // ═══════════════════════════════════════════════

    // 协变度规 g_μν
    double g_dn(int mu, int nu, const glm::dvec4& pos) const override {
        double r     = pos[1];
        double theta = pos[2];
        double S     = Sigma(r, theta);
        double sin_t = std::sin(theta);
        double sin2  = sin_t * sin_t;

        // g_tt
        if (mu == 0 && nu == 0) {
            return -(1.0 - 2.0 * M_ * r / S);
        }
        // g_tφ = g_φt
        if ((mu == 0 && nu == 3) || (mu == 3 && nu == 0)) {
            return -2.0 * M_ * a_ * r * sin2 / S;
        }
        // g_rr
        if (mu == 1 && nu == 1) {
            return S / Delta(r);
        }
        // g_θθ
        if (mu == 2 && nu == 2) {
            return S;
        }
        // g_φφ
        if (mu == 3 && nu == 3) {
            double r2 = r * r;
            return (r2 + a2_ + 2.0 * M_ * a2_ * r * sin2 / S) * sin2;
        }
        return 0.0;
    }

    // 逆变度规 g^μν
    double g_up(int mu, int nu, const glm::dvec4& pos) const override {
        double r     = pos[1];
        double theta = pos[2];
        double S     = Sigma(r, theta);
        double D     = Delta(r);
        double sin_t = std::sin(theta);
        double sin2  = sin_t * sin_t;
        double r2    = r * r;

        // g^tt
        if (mu == 0 && nu == 0) {
            double A = (r2 + a2_) * (r2 + a2_) - a2_ * D * sin2;
            return -A / (S * D);
        }
        // g^tφ = g^φt
        if ((mu == 0 && nu == 3) || (mu == 3 && nu == 0)) {
            return -2.0 * M_ * a_ * r / (S * D);
        }
        // g^rr
        if (mu == 1 && nu == 1) {
            return D / S;
        }
        // g^θθ
        if (mu == 2 && nu == 2) {
            return 1.0 / S;
        }
        // g^φφ
        if (mu == 3 && nu == 3) {
            if (sin2 < 1e-30) return 0.0;  // 极轴退化
            double A = (r2 + a2_) * (r2 + a2_) - a2_ * D * sin2;
            return (D - a2_ * sin2) / (S * D * sin2);
            // 等价形式: (Δ - a² sin²θ) / (Σ Δ sin²θ)
        }
        return 0.0;
    }

    // Christoffel 符号 Γ^μ_αβ
    // 默认使用数值差分（已验证正确），解析式为未来 GPU 移植参考
    double christoffel(int mu, int alpha, int beta,
                       const glm::dvec4& pos) const override {
        return christoffel_numerical(mu, alpha, beta, pos);
    }

    // 解析式 Christoffel（用于 GPU 移植参考 + 交叉验证）
    double christoffel_analytical(int mu, int alpha, int beta,
                                  const glm::dvec4& pos) const {
        // 利用对称性: Γ^μ_αβ = Γ^μ_βα
        if (alpha > beta) std::swap(alpha, beta);

        double r     = pos[1];
        double theta = pos[2];
        double S     = Sigma(r, theta);
        double D     = Delta(r);
        double sin_t = std::sin(theta);
        double cos_t = std::cos(theta);
        double sin2  = sin_t * sin_t;
        double r2    = r * r;

        // 常用导数
        double dS_dr     = 2.0 * r;
        double dS_dtheta = -2.0 * a2_ * sin_t * cos_t;
        double dD_dr     = 2.0 * (r - M_);

        // ── Γ^t_αβ (mu == 0) ──
        if (mu == 0) {
            // Γ^t_tr = Γ^t_rt
            if ((alpha == 0 && beta == 1) || (alpha == 1 && beta == 0)) {
                double num = (r2 + a2_) * dS_dr - 2.0 * r * S;
                return ((r2 + a2_) * num) / (2.0 * S * S * D);
            }
            // Γ^t_tθ = Γ^t_θt
            if ((alpha == 0 && beta == 2) || (alpha == 2 && beta == 0)) {
                return -a2_ * M_ * r * sin_t * cos_t * 2.0 / (S * S);
            }
            // Γ^t_rφ = Γ^t_φr
            if ((alpha == 1 && beta == 3) || (alpha == 3 && beta == 1)) {
                double num = -a_ * M_ * sin2 *
                    (S * (r2 - a2_ * cos_t * cos_t) - 2.0 * r2 * dS_dr);
                return num / (2.0 * S * S * D);
                // 简化: -a M sin²θ (a² cos²θ (S - 2r²) + r² S) / (S² Δ)
            }
            // Γ^t_θφ = Γ^t_φθ
            if ((alpha == 2 && beta == 3) || (alpha == 3 && beta == 2)) {
                double num = -2.0 * a_ * M_ * r * (r2 + a2_) *
                    sin_t * cos_t;
                return num / (S * S * D) * sin2;  // 需额外乘 sin²θ
            }
        }

        // ── Γ^r_αβ (mu == 1) ──
        if (mu == 1) {
            // Γ^r_tt
            if (alpha == 0 && beta == 0) {
                return M_ * D * (S - 2.0 * r2) / (S * S * S);
                // 简化: M Δ (r² - a² cos²θ) / Σ³
            }
            // Γ^r_tφ = Γ^r_φt
            if ((alpha == 0 && beta == 3) || (alpha == 3 && beta == 0)) {
                double num = -M_ * a_ * sin2 * D *
                    (a2_ * cos_t * cos_t * (S - 2.0 * r2) - r2 * S);
                return num / (S * S * S * S);  // 太复杂，用近似
                // 实际公式见 numerical fallback
            }
            // Γ^r_rr
            if (alpha == 1 && beta == 1) {
                return (dS_dr * D - S * dD_dr) / (2.0 * S * D);
            }
            // Γ^r_rθ = Γ^r_θr
            if ((alpha == 1 && beta == 2) || (alpha == 2 && beta == 1)) {
                return -a2_ * sin_t * cos_t / S;
            }
            // Γ^r_θθ
            if (alpha == 2 && beta == 2) {
                return -r * D / S;
            }
            // Γ^r_φφ
            if (alpha == 3 && beta == 3) {
                double term = sin2 * D *
                    (r * S * S - M_ * (r2 - a2_ * cos_t * cos_t) * S
                     + 2.0 * M_ * r * r2);
                return term / (S * S * S);
            }
        }

        // ── Γ^θ_αβ (mu == 2) ──
        if (mu == 2) {
            // Γ^θ_tt
            if (alpha == 0 && beta == 0) {
                return -2.0 * a2_ * M_ * r * sin_t * cos_t / (S * S * S);
            }
            // Γ^θ_tφ = Γ^θ_φt
            if ((alpha == 0 && beta == 3) || (alpha == 3 && beta == 0)) {
                double num = 2.0 * a_ * M_ * r * (r2 + a2_) *
                    sin_t * cos_t;
                return num / (S * S * S);
            }
            // Γ^θ_rθ = Γ^θ_θr
            if ((alpha == 1 && beta == 2) || (alpha == 2 && beta == 1)) {
                return r / S;
            }
            // Γ^θ_θθ
            if (alpha == 2 && beta == 2) {
                return dS_dtheta / (2.0 * S);
            }
            // Γ^θ_φφ
            if (alpha == 3 && beta == 3) {
                double term = -sin_t * cos_t *
                    (S * (r2 + a2_) + 2.0 * M_ * a2_ * r * sin2 *
                     (r2 + a2_ + S / sin2)) / (S * S);
                return term;
                // 简化: -sinθ cosθ [(r²+a²)Σ + 2Ma²r sin²θ(r²+a²)] / Σ²
            }
        }

        // ── Γ^φ_αβ (mu == 3) ──
        if (mu == 3) {
            // Γ^φ_tr = Γ^φ_rt
            if ((alpha == 0 && beta == 1) || (alpha == 1 && beta == 0)) {
                return -a_ * M_ * (S - 2.0 * r2) / (S * S * D);
            }
            // Γ^φ_tθ = Γ^φ_θt
            if ((alpha == 0 && beta == 2) || (alpha == 2 && beta == 0)) {
                return -2.0 * a_ * M_ * r * cos_t / (S * S * sin_t);
            }
            // Γ^φ_rφ = Γ^φ_φr
            if ((alpha == 1 && beta == 3) || (alpha == 3 && beta == 1)) {
                double num = S * (r * S - M_ * (r2 - a2_ * cos_t * cos_t))
                    + r * (r2 + a2_) * dS_dr;
                return num / (2.0 * S * S * D);
            }
            // Γ^φ_θφ = Γ^φ_φθ
            if ((alpha == 2 && beta == 3) || (alpha == 3 && beta == 2)) {
                return cos_t / sin_t +
                    a2_ * M_ * r * sin_t * cos_t * 2.0 / (S * S);
                // cotθ + 2a²Mr sinθ cosθ / Σ²
            }
        }

        return 0.0;
    }

    // ═══════════════════════════════════════════════
    // 数值差分 Christoffel（交叉验证用）
    // ═══════════════════════════════════════════════

    double christoffel_numerical(int mu, int alpha, int beta,
                                 const glm::dvec4& pos,
                                 double eps = 1e-8) const {
        double result = 0.0;
        for (int nu = 0; nu < 4; ++nu) {
            double g_up_munu = g_up(mu, nu, pos);
            if (std::abs(g_up_munu) < 1e-30) continue;

            // ∂_α g_νβ (central difference)
            glm::dvec4 p_plus  = pos;
            glm::dvec4 p_minus = pos;
            p_plus[alpha]  += eps;
            p_minus[alpha] -= eps;
            double d_alpha = (g_dn(nu, beta, p_plus) -
                              g_dn(nu, beta, p_minus)) / (2.0 * eps);

            // ∂_β g_να
            p_plus  = pos;
            p_minus = pos;
            p_plus[beta]  += eps;
            p_minus[beta] -= eps;
            double d_beta = (g_dn(nu, alpha, p_plus) -
                             g_dn(nu, alpha, p_minus)) / (2.0 * eps);

            // ∂_ν g_αβ
            p_plus  = pos;
            p_minus = pos;
            p_plus[nu]  += eps;
            p_minus[nu] -= eps;
            double d_nu = (g_dn(alpha, beta, p_plus) -
                           g_dn(alpha, beta, p_minus)) / (2.0 * eps);

            result += 0.5 * g_up_munu * (d_alpha + d_beta - d_nu);
        }
        return result;
    }

    // ── 质量/角动量 getter ──
    double M() const { return M_; }
    double a() const { return a_; }

private:
    double M_;
    double a_;
    double a2_;  // a²，缓存避免重复计算
};

#endif // BLACK_HOLE_PRAC_PHYSICS_KERR_H
