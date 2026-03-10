cmake_minimum_required(VERSION 3.15)

project(modvm
    VERSION 0.1.0
    DESCRIPTION "A Modular and Multi-arch Virtual Machine Monitor"
    LANGUAGES C
)

# 强制使用 C11 标准，禁用特定编译器的语法扩展 (如 GNU C 扩展)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 开启严格的编译器警告防御
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror -Wno-unused-value)
endif()

# 宿主机 CPU 架构探测与宏定义分发
if(CMAKE_SYSTEM_PROCESSOR MATCHES "amd64|x86_64|AMD64")
    set(MODVM_ARCH "x86_64")
    add_compile_definitions(ARCH_X86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    set(MODVM_ARCH "aarch64")
    add_compile_definitions(ARCH_AARCH64)
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# 宿主机 OS 探测与宏定义分发
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MODVM_OS "linux")
    add_compile_definitions(OS_LINUX)
else()
    message(FATAL_ERROR "Unsupported OS: ${CMAKE_SYSTEM_NAME}")
endif()

message(STATUS "Configuring ModVM for ${MODVM_OS}-${MODVM_ARCH}")

# 查找并配置标准线程库 (自动处理 -pthread 依赖)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# 开启 CTest 测试支持
enable_testing()

# 将构建任务分发给源码目录
add_subdirectory(src)
add_subdirectory(tests)