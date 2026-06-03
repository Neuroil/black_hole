add_executable(simulation main.cpp
        vulkan/vulkan_app.cpp
        vulkan/vulkan_util.cpp
        vulkan/vulkan_context.cpp
        vulkan/gpu_buffer.cpp
        vulkan/gpu_image.cpp)

target_link_libraries(simulation glm::glm glfw Vulkan::Vulkan)