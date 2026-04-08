//
// Created by Neuroil on 2025/11/26.
//
#include "vk.h"
#include "object.h"
#include <iostream>

int main() {
    vk app;
    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}