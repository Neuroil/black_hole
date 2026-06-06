cmake_minimum_required(VERSION 3.10)

# 物理层测试 — 纯数学，不链接 Vulkan
# 每个测试是独立可执行文件，返回 0=pass, 1=fail

find_package(glm CONFIG REQUIRED)

include_directories("${CMAKE_SOURCE_DIR}/src")

# test_kerr — 度规定点测试
add_executable(test_kerr test_kerr.cpp)
target_link_libraries(test_kerr glm::glm)

# test_geodesic — 守恒量测试
add_executable(test_geodesic test_geodesic.cpp)
target_link_libraries(test_geodesic glm::glm)

# test_integrator — 积分器精度测试
add_executable(test_integrator test_integrator.cpp)
target_link_libraries(test_integrator glm::glm)

# 注册到 CTest
add_test(NAME test_kerr       COMMAND test_kerr)
add_test(NAME test_geodesic   COMMAND test_geodesic)
add_test(NAME test_integrator COMMAND test_integrator)
