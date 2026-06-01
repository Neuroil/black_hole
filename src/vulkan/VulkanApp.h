//
// Created by neuroil on 2026/6/1.
//

#ifndef BLACK_HOLE_PRAC_VULKANAPP_H
#define BLACK_HOLE_PRAC_VULKANAPP_H

#include "VulkanContext.h"
#include <GLFW/glfw3.h>

class VulkanApp {
    uint32_t WIDTH, HEIGHT;
    GLFWwindow* window = nullptr;

    void init_window();
    void init_vulkan();
    void main_loop();
    void clean_up();

public:
    VulkanApp(uint32_t w, uint32_t h);
    ~VulkanApp();
    void run();


};


#endif //BLACK_HOLE_PRAC_VULKANAPP_H