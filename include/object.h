// Created by Neuroil on 2026/04/01

#ifndef OBJECTS_HPP
#define OBJECTS_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

// 物理常数
namespace Physics {
    const double C = 299792458.0;
    const double G = 6.67430e-11;
}

/**
 * @brief 天体数据结构
 * 增加 alignas(16) 确保在 Vulkan/OpenGL 的 Shader 中布局一致
 */
struct alignas(16) ObjectData {
    glm::vec4 posRadius; // xyz = position, w = radius
    glm::vec4 color;     // rgb = color, a = unused
    float mass;
    glm::vec3 velocity;

    // 默认构造函数
    ObjectData() 
        : posRadius(0.0f), color(1.0f), mass(0.0f), velocity(0.0f) {}

    ObjectData(glm::vec4 pr, glm::vec4 col, float m, glm::vec3 vel = glm::vec3(0.0f))
        : posRadius(pr), color(col), mass(m), velocity(vel) {}
};

/**
 * @brief 黑洞物理类
 */
class BlackHole {
public:
    glm::vec3 position;
    double mass;
    double r_s; // 史瓦西半径

    BlackHole(glm::vec3 pos, double m) : position(pos), mass(m) {
        r_s = (2.0 * Physics::G * mass) / (Physics::C * Physics::C);
    }

    // 检查点是否在事件视界内
    bool isInside(glm::vec3 p) const {
        return glm::distance(p, position) < r_s;
    }
};

/**
 * @brief 轨道摄像机类 (与渲染 API 无关)
 */
class OrbitCamera {
public:
    glm::vec3 target = glm::vec3(0.0f);
    float radius = 6.34194e10f;
    float azimuth = 0.0f;
    float elevation = 1.570796f; // M_PI / 2

    // 限制参数
    float minRadius = 1e10f, maxRadius = 1e12f;
    float orbitSpeed = 0.005f;
    double zoomSpeed = 25e9f;

    // 状态标志
    bool isMoving = false;

    // 获取相机在世界空间的位置
    glm::vec3 getPosition() const {
        float clElev = glm::clamp(elevation, 0.01f, 3.13159f);
        return glm::vec3(
            radius * sin(clElev) * cos(azimuth),
            radius * cos(clElev),
            radius * sin(clElev) * sin(azimuth)
        );
    }

    // 获取 View 矩阵 (Vulkan 用户需要注意 Y 轴翻转，但在逻辑层保持通用)
    glm::mat4 getViewMatrix() const {
        return glm::lookAt(getPosition(), target, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void rotate(float dx, float dy) {
        azimuth += dx * orbitSpeed;
        elevation -= dy * orbitSpeed;
        elevation = glm::clamp(elevation, 0.01f, 3.13159f);
    }

    void zoom(float yoffset) {
        radius -= yoffset * (float)zoomSpeed;
        radius = glm::clamp(radius, minRadius, maxRadius);
    }
};

/**
 * @brief 简单的引力计算引擎
 */
namespace GravityEngine {
    inline void applyNBodyGravity(std::vector<ObjectData>& objects, bool enabled, double dt) {
        if (!enabled) return;

        for (size_t i = 0; i < objects.size(); ++i) {
            glm::vec3 totalAcc(0.0f);
            for (size_t j = 0; j < objects.size(); ++j) {
                if (i == j) continue;

                glm::vec3 diff = glm::vec3(objects[j].posRadius) - glm::vec3(objects[i].posRadius);
                float dist = glm::length(diff);

                if (dist > 1.0f) { // 避免除以零
                    float force = (Physics::G * objects[i].mass * objects[j].mass) / (dist * dist);
                    totalAcc += glm::normalize(diff) * (force / objects[i].mass);
                }
            }
            // 更新速度和位置
            objects[i].velocity += totalAcc * (float)dt;
            objects[i].posRadius += glm::vec4(objects[i].velocity * (float)dt, 0.0f);
        }
    }
}

#endif // OBJECTS_HPP
