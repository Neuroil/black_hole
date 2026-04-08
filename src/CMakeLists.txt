include_directories(../include)

add_executable(main main.cpp engine/vk.cpp engine/vertex_data.cpp)

target_link_libraries(main PRIVATE glfw glm::glm Vulkan::Vulkan)

# --- 构建后操作：将编译好的 SPIR-V 着色器复制到可执行文件目录下，
# 以便运行时无论当前工作目录为何都能加载这些着色器文件。
# 该步骤实现了方案 A（将 SPV 文件复制到输出目录）。它会在可执行文件
# 所在目录旁创建 `src/shader` 子目录并复制三个 SPV 文件，避免当进程
# 工作目录为可执行目录时找不到着色器文件导致的错误（例如双击 exe 或
# IDE 将工作目录设置为构建目录时）。
add_custom_command(TARGET main POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:main>/src/shader"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/src/shader/comp.spv"
		"$<TARGET_FILE_DIR:main>/src/shader/comp.spv"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/src/shader/vert.spv"
		"$<TARGET_FILE_DIR:main>/src/shader/vert.spv"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/src/shader/frag.spv"
		"$<TARGET_FILE_DIR:main>/src/shader/frag.spv"
)
