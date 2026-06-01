add_executable(simulation main.cpp
        vulkan/VulkanApp.cpp
        vulkan/vulkan_util.cpp
        vulkan/VulkanContext.cpp)

target_link_libraries(simulation glm::glm glfw Vulkan::Vulkan)