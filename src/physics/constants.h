//
// Created by neuroil on 2026/6/6.
//

#ifndef BLACK_HOLE_PRAC_PHYSICS_CONSTANTS_H
#define BLACK_HOLE_PRAC_PHYSICS_CONSTANTS_H

namespace physics {

// ── 物理常量 (SI) ──
constexpr double G      = 6.67430e-11;   // 万有引力常数 [m³/(kg·s²)]
constexpr double c      = 2.99792458e8;  // 光速 [m/s]
constexpr double M_sun  = 1.98847e30;    // 太阳质量 [kg]

// ── 几何单位制转换 ──
// 长度: L_geom = L_SI / (G*M/c²)
// 时间: T_geom = T_SI / (G*M/c³)
// 使用方: Kerr 构造时 M 以几何单位（米）传入
inline constexpr double mass_to_geometric_length(double mass_kg) {
    return G * mass_kg / (c * c);
}

} // namespace physics

#endif // BLACK_HOLE_PRAC_PHYSICS_CONSTANTS_H
