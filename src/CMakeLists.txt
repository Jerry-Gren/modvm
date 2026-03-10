# src/CMakeLists.txt

# 1. 使用 GLOB 自动收集各个模块的源文件
# CONFIGURE_DEPENDS 是现代 CMake 的神仙属性，它能确保你新建/删除 .c 文件时，
# 下一次 make 能够感知到变化并自动触发 CMake 重新配置，不再需要手动执行 cmake。
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "core/*.c")
file(GLOB UTILS_SOURCES CONFIGURE_DEPENDS "utils/*.c")
file(GLOB BACKEND_SOURCES CONFIGURE_DEPENDS "backends/*/*.c")
file(GLOB HW_SOURCES CONFIGURE_DEPENDS "hw/*/*.c")

# 2. 根据顶层 CMakeLists 探测到的平台和架构，条件加载源文件
# 这里用到了顶层的 ${MODVM_OS} (如 posix) 和 ${MODVM_ARCH} (如 x86_64)
file(GLOB OS_SOURCES CONFIGURE_DEPENDS "os/posix/*.c") 
file(GLOB ARCH_SOURCES CONFIGURE_DEPENDS "motherboard/${MODVM_ARCH}/*.c")

# 3. 收集虚拟化后端 (目前只收集 KVM)
file(GLOB HYPERVISOR_SOURCES CONFIGURE_DEPENDS "hypervisor/kvm/*.c")
file(GLOB HYPERVISOR_ARCH_SOURCES CONFIGURE_DEPENDS "hypervisor/kvm/${MODVM_ARCH}/*.c")

# 4. 核心优化：构建 OBJECT 库
add_library(modvm_core OBJECT
    ${CORE_SOURCES}
    ${UTILS_SOURCES}
    ${BACKEND_SOURCES}
    ${HW_SOURCES}
    ${OS_SOURCES}
    ${ARCH_SOURCES}
    ${HYPERVISOR_SOURCES}
    ${HYPERVISOR_ARCH_SOURCES}
)

# 继承公共头文件
target_include_directories(modvm_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
# OBJECT 库可以自身绑定依赖
target_link_libraries(modvm_core PUBLIC Threads::Threads)

# 5. 编译主程序入口
add_executable(modvm main.c)

# 现代 CMake 允许直接 target_link 包含 OBJECT 库，
# 它会自动把那些游离的 .o 文件投递给最终的链接器。
target_link_libraries(modvm PRIVATE modvm_core)