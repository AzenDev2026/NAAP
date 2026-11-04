cmake_minimum_required(VERSION 3.16)
project(freeze-manager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

add_executable(freeze-manager
    src/main.cpp
    src/freezer.cpp
)
target_include_directories(freeze-manager PRIVATE ${SYSTEMD_INCLUDE_DIRS})
target_link_libraries(freeze-manager PRIVATE ${SYSTEMD_LIBRARIES})

# 安装二进制
# KWin 脚本由 install.sh 负责复制到
# ~/.local/share/kwin/scripts/freeze-manager
# （KWin 只认这个固定位置，且 cmake 里 $ENV{HOME} 在 sudo 下会出错）
install(TARGETS freeze-manager RUNTIME DESTINATION bin)

# 单元测试：验证 cgroup 路径解析与安全校验逻辑
enable_testing()
add_executable(test-cgroup test_cgroup.cpp src/freezer.cpp)
target_include_directories(test-cgroup PRIVATE src)
target_compile_features(test-cgroup PRIVATE cxx_std_17)
add_test(NAME cgroup-helpers COMMAND test-cgroup)