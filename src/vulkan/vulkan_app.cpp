//
// Created by neuroil on 2026/6/1.
//

#include "vulkan_app.h"

VulkanApp::VulkanApp(uint32_t w, uint32_t h) : context_(nullptr){
    context_ = std::make_unique<VulkanContext>(w,h);
    window_ = context_->get_p_windows();
}
VulkanApp::~VulkanApp() {

}

void VulkanApp::run() {
    main_loop();
    clean_up();
}

void VulkanApp::main_loop() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
    }
}

void VulkanApp::clean_up() {
    //
}


