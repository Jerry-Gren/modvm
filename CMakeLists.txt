# 1. 设置 CMake 要求的最低版本和项目信息
cmake_minimum_required(VERSION 3.15)
project(modvm 
    VERSION 0.1.0 
    DESCRIPTION "A Modular and Multi-arch Virtual Machine Monitor"
    LANGUAGES C
)

# 2. 设置 C 语言标准 (推荐 C11，兼顾现代特性与底层兼容性)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 3. 开启严格的编译警告 (写底层代码，把警告当错误处理是好习惯)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror -Wno-unused-value)
endif()

# 4. 定义全局宏：探测当前目标 CPU 架构
if(CMAKE_SYSTEM_PROCESSOR MATCHES "amd64|x86_64|AMD64")
    set(MODVM_ARCH "x86_64")
    add_compile_definitions(ARCH_X86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    set(MODVM_ARCH "aarch64")
    add_compile_definitions(ARCH_AARCH64)
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

message(STATUS "Building modvm for architecture: ${MODVM_ARCH}")

# 5. 定义全局宏：探测当前操作系统
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MODVM_OS "linux")
    add_compile_definitions(OS_LINUX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(MODVM_OS "windows")
    add_compile_definitions(OS_WINDOWS)
else()
    message(FATAL_ERROR "Unsupported OS: ${CMAKE_SYSTEM_NAME}")
endif()

message(STATUS "Building modvm for OS: ${MODVM_OS}")

# 发现系统线程库 (这会自动处理 -pthread 或 -lpthread 等平台差异)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# 6. 设置公共头文件搜索路径 (让所有子目录都能找到 <modvm/...>)
include_directories(${CMAKE_SOURCE_DIR}/include)

# 7. 引入子目录 (将构建任务分发下去)
add_subdirectory(src)

# 8. (可选) 开启测试
enable_testing()
add_subdirectory(tests)