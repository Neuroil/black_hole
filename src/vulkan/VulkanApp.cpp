//
// Created by neuroil on 2026/6/1.
//

#include "VulkanApp.h"

VulkanApp::VulkanApp(uint32_t w, uint32_t h) : WIDTH(w), HEIGHT(h), context_(nullptr){
    init_window();
    context_ = std::make_unique<VulkanContext>(window);
}
VulkanApp::~VulkanApp() {
    glfwDestroyWindow(window);

    glfwTerminate();
}

void VulkanApp::run() {
    // main_loop
    main_loop();
    // release
    clean_up();
}

void VulkanApp::init_window() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Blackhole GRRT Simulation", nullptr, nullptr);
}


void VulkanApp::main_loop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }
}

void VulkanApp::clean_up() {
    //
}


