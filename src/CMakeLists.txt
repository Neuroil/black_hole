# 着色器编译
set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/src/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

function(compile_shader SHADER_FILE)
    add_custom_command(
        OUTPUT "${SHADER_OUT_DIR}/${SHADER_FILE}.spv"
        COMMAND glslc "${SHADER_SRC_DIR}/${SHADER_FILE}" -o "${SHADER_OUT_DIR}/${SHADER_FILE}.spv"
        DEPENDS "${SHADER_SRC_DIR}/${SHADER_FILE}"
        COMMENT "Compiling ${SHADER_FILE} → SPIR-V"
    )
endfunction()

compile_shader(geodesic.comp)
compile_shader(fullscreen.vert)
compile_shader(fullscreen.frag)

set(SHADER_SPV
    "${SHADER_OUT_DIR}/geodesic.comp.spv"
    "${SHADER_OUT_DIR}/fullscreen.vert.spv"
    "${SHADER_OUT_DIR}/fullscreen.frag.spv"
)

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