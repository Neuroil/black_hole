# 着色器编译 — 自动发现 shaders/ 下所有 .comp .vert .frag
set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/src/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

file(GLOB SHADER_SRCS CONFIGURE_DEPENDS
    "${SHADER_SRC_DIR}/*.comp"
    "${SHADER_SRC_DIR}/*.vert"
    "${SHADER_SRC_DIR}/*.frag"
)

# 排除 ks_block_* 系列（旧实验文件）
list(FILTER SHADER_SRCS EXCLUDE REGEX "ks_block")
list(FILTER SHADER_SRCS EXCLUDE REGEX "\\.old$")

set(SHADER_SPV "")

foreach(SHADER_SRC ${SHADER_SRCS})
    get_filename_component(SHADER_NAME ${SHADER_SRC} NAME)
    set(SPV_OUT "${SHADER_OUT_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT "${SPV_OUT}"
        COMMAND glslc "${SHADER_SRC}" -o "${SPV_OUT}"
        DEPENDS "${SHADER_SRC}"
        COMMENT "Compiling ${SHADER_NAME} → SPIR-V"
    )
    list(APPEND SHADER_SPV "${SPV_OUT}")
endforeach()

message(STATUS "Shader sources: ${SHADER_SRCS}")

add_executable(simulation main.cpp
        vulkan/vulkan_app.cpp
        vulkan/vulkan_util.cpp
        vulkan/vulkan_context.cpp
        vulkan/gpu_buffer.cpp
        vulkan/gpu_image.cpp
        vulkan/compute_pipeline.cpp
        vulkan/graphics_pipeline.cpp
        vulkan/command_buffer.cpp)

add_dependencies(simulation shaders_spv)

add_custom_target(shaders_spv ALL DEPENDS ${SHADER_SPV})

target_compile_definitions(simulation PRIVATE SHADER_DIR="${SHADER_OUT_DIR}")

target_link_libraries(simulation glm::glm glfw Vulkan::Vulkan)