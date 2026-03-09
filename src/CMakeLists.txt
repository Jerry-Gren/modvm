# src/CMakeLists.txt

# 将所有底层机制打包为静态引擎库
add_library(modvm_core STATIC
    core/bus.c
    core/log.c
    core/memory.c
    core/machine.c
    core/loader.c
    core/os_thread_posix.c
    core/os_page_posix.c
    core/machine_registry.c
    core/device_registry.c
    core/event_loop.c
    core/irq.c
    arch/x86_64/kvm_vcpu.c
    arch/x86_64/kvm_vm.c
    arch/x86_64/machine_pc.c
    hw/char/uart_16550a.c
    hw/char/chardev_stdio.c
    hw/misc/debug_exit.c
)

# 暴露公共头文件给链接此库的目标
target_include_directories(modvm_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

# 极度重要：将操作系统的线程库私有链接到引擎核心中
target_link_libraries(modvm_core PRIVATE Threads::Threads)

# 声明最终的宿主虚拟机入口程序
add_executable(modvm main.c)

# 将入口程序与核心引擎库进行链接
target_link_libraries(modvm PRIVATE
    -Wl,--whole-archive modvm_core -Wl,--no-whole-archive
)