//
// Created by neuroil on 2026/6/1.
//

#ifndef BLACK_HOLE_PRAC_VULKANAPP_H
#define BLACK_HOLE_PRAC_VULKANAPP_H

#include "vulkan_context.h"
#include <GLFW/glfw3.h>
#include <memory>

class VulkanApp {
    std::unique_ptr<VulkanContext> context_;
    // uint32_t WIDTH, HEIGHT;
    GLFWwindow* window_ = nullptr;

    void main_loop();
    void clean_up();

public:
    VulkanApp(uint32_t w, uint32_t h);
    ~VulkanApp();
    void run();


};


#endif //BLACK_HOLE_PRAC_VULKANAPP_H