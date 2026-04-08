//
// Created by Neuroil on 2025/11/26.
//
#ifndef BLACKHOLE_PRAC_VK_HPP
#define BLACKHOLE_PRAC_VK_HPP

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_VULKAN

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#ifdef __linux__
#define GLFW_EXPOSE_NATIVE_LINUX
#endif

#include <GLFW/glfw3.h>
#include <vector>
#include <optional>
#include <set>
#include <string>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions();
};

extern const std::vector<Vertex> vertices;
extern const std::vector<uint16_t> indices;

// ubo_before
struct UniformBufferObject {
    // alignas(16) glm::mat4 model;
    // alignas(16) glm::mat4 view;
    // alignas(16) glm::mat4 proj;
    glm::vec3 cameraPos; float _pad0;
    glm::vec3 cameraDir; float _pad1;
    glm::vec3 cameraUp;  float _pad2;
    glm::vec3 cameraRight; float _pad3;

    glm::vec2 screenSize; glm::vec2 _pad4;

    glm::vec3 blackholePos_1; float blackholeRadius_1;
    glm::vec3 blackholePos_2; float blackholeRadius_2;
    glm::vec3 objectPos; float objectRadius;
};

// ubo
//  struct Params {
//     glm::vec3 cameraPos; float _pad0;
//     glm::vec3 cameraDir; float _pad1;
//     glm::vec3 cameraUp;  float _pad2;
//     glm::vec3 cameraRight; float _pad3;
//
//     glm::vec2 screenSize; glm::vec2 _pad4;
//
//     glm::vec3 objectPos; float objectRadius;
// };

struct PixelResult {
    // 用于储存每个像素的计算结果
    glm::vec4 finalColor;
    glm::vec4 hitPosition_and_padding; // 使用 vec4 确保对齐
    glm::vec4 pathLength_and_padding;  // 使用 vec4 确保对齐
};

// ----------------------------------------------------------------------------------------------
// 好累写不动了
// -------------------------------------------------------------------------------------
// 1.Descriptor Set Layout
//     定义资源的结构蓝图。 指定每个槽位 (binding) 是 UBO, SSBO, 还是 Sampler。
// 2.Pipeline Layout
//     将 DescriptorSetLayout 关联到 Pipeline。 (定义管线使用哪套资源)
// -------------------------------------------------------------------------------------
// 3.Descriptor Pool
//     描述符内存工厂。 预分配描述符 (Set 内的槽位) 的总容量。
// 4.Descriptor Set
//     实际的资源集合。 从 Pool 中分配一个 Layout 结构的实例。
// 5.Buffer/Image
//     创建底层数据存储。 (创建 VkBuffer 和 VkDeviceMemory)
// 6.Update Descriptor Sets
//     连接资源。 将第 5 步的 Buffer 链接到第 4 步 Set 中的指定 binding 槽位。
// -------------------------------------------------------------------------------------
// 7.vkCmdCopyBuffer
//     数据上传。 仅用于将数据从 CPU 可见的 临时缓冲区 复制到 GPU Only 的 SSBO 中。
//     时机： 记录 Command Buffer 时。
// 8.vkCmdBindDescriptorSets
//     绑定资源。 在绘制/计算前，告诉 GPU 使用第 4 步创建的 DescriptorSet。
//     时机： 记录 Command Buffer 时。
// 9.vkCmdPipelineBarrier
//     同步。 确保前一个操作 (如 SSBO 写入) 完成后，下一个操作 (如 SSBO 读取) 再开始。
//     时机： 记录 Command Buffer 时。
// 10.vkQueueSubmit
//     提交执行。 将包含所有 vkCmd 操作的 Command Buffer 提交给 GPU 队列执行。
// -------------------------------------------------------------------------------------
// 核心作用总结：
//     Binding: 资源的槽位索引，连接 Shader 和 Descriptor Set。
//     Descriptor Set: 一组资源的容器，方便整体切换。
//     Descriptor Pool: 描述符 Set 的分配器，提高资源分配效率。

struct QueueFamily{
    std::optional<uint32_t> computeGraphicQueueFamily;
    std::optional<uint32_t> presentQueueFamily;

    bool isComplete(){
        return computeGraphicQueueFamily.has_value() &&
               presentQueueFamily.has_value();
    }
};

struct SwapChainDetails{
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> modes;
};

struct windowInfo {
    uint32_t width = 800;
    uint32_t height = 600;
    const char* title = "Black Hole Ray Tracing Simulation COMPUTE_SHADER";
};

class vk {
public:
    void run();

private:
    void initWindow();
    void initVulkan();
    void mainLoop();
    void cleanup();

private:
    GLFWwindow* window = nullptr;
    windowInfo appWindowInfo;

private:
    VkInstance instance{};
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    QueueFamily q_family;
    VkDevice device;
    VkQueue computeGraphicQueue;
    VkQueue presentQueue;
    // VkQueue computeQueue;
    VkSwapchainKHR swapChain;
    VkExtent2D swapChainExtent;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainImageExtent;
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;
    VkRenderPass renderPass;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    std::vector<VkDescriptorSet> descriptorSets;
    VkPipelineLayout graphicPipelineLayout;
    VkPipelineLayout computePipelineLayout;
    VkPipeline graphicsPipeline;
    VkPipeline computePipeline;
    std::vector<VkFramebuffer> swapChainFramebuffers;
    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;
    std::vector<VkBuffer> shaderStorageBuffers;
    std::vector<VkDeviceMemory> shaderStorageBuffersMemory;
    VkDeviceSize ssboSize;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

private:
    uint32_t currentFrame = 0;

private:
    // Debug 相关
    bool framebufferResized = false;
    VkDebugUtilsMessengerEXT debugMessenger;

private:
    // 辅助函数
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger);
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
    SwapChainDetails querySwapChainSupport(VkPhysicalDevice c_device);
    bool checkDeviceExtensionSupported(VkPhysicalDevice c_device);
    QueueFamily findQueueFamily(VkPhysicalDevice c_device);
    bool isPhysicalDeviceSuitable(VkPhysicalDevice c_device);
    VkSurfaceFormatKHR chooseSurfaceFormat(const SwapChainDetails& details);
    VkPresentModeKHR choosePresentMode(const SwapChainDetails& details);
    VkExtent2D chooseExtent2D(const SwapChainDetails& details);
    static std::vector<char> readFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties, VkBuffer& buffer,
                  VkDeviceMemory& bufferMemory);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickupPhysicalDevice();
    void createLogicDevice();
    void createSwapChain();
    void createImageView();
    // 这个还没改
    void createRenderPass();
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void createFrameBuffers();
    void createCommandPool();
    // void createVertexBuffer();
    // void createIndexBuffer();
    void createComputePipeline(); // NEW
    void createUniformBuffers(); // 确保大小正确
    void createShaderStorageBuffers(); // 确保 SSBO 大小基于分辨率
    void createDescriptorPool(); // 添加 STORAGE_BUFFER 容量
    void createDescriptorSets(); // 同时绑定 UBO 和 SSBO
    void createCommandBuffers(); // 核心修改：Compute + Barrier + Graphics
    void createSyncObjects();

    void draw();

private:
    // 销毁函数
    void DestroyDebugUtilsMessengerEXT(VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator);
    void cleanupSwapChain();
};

#endif //BLACKHOLE_PRAC_VK_HPP