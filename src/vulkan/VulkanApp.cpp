//
// Created by neuroil on 2026/6/1.
//

#include "VulkanApp.h"

VulkanApp::VulkanApp(uint32_t w, uint32_t h) : WIDTH(w), HEIGHT(h){}
VulkanApp::~VulkanApp() {
    glfwDestroyWindow(window);

    glfwTerminate();
}

void VulkanApp::run() {
    // init
    init_window();
    init_vulkan();
    // main_loop
    main_loop();
    // release
    clean_up();
}

void VulkanApp::init_window() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole GRRT", nullptr, nullptr);
}

void VulkanApp::init_vulkan() {}


void VulkanApp::main_loop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }
}

void VulkanApp::clean_up() {
    //
}


