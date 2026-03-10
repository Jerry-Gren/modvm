# src/CMakeLists.txt

# CONFIGURE_DEPENDS is a powerful modern CMake attribute. It ensures that 
# adding or removing .c files triggers an automatic re-configuration during 
# the next 'make' or build command, eliminating the need for manual 'cmake ..' calls.
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "core/*.c")
file(GLOB UTILS_SOURCES CONFIGURE_DEPENDS "utils/*.c")
file(GLOB BACKEND_SOURCES CONFIGURE_DEPENDS "backends/*/*.c")
file(GLOB HW_SOURCES CONFIGURE_DEPENDS "hw/*/*.c")

# Conditional Source Loading based on platform and architecture
# Utilizes ${MODVM_OS} (e.g., linux) and ${MODVM_ARCH} (e.g., x86_64) 
# detected in the top-level CMakeLists.
file(GLOB OS_SOURCES CONFIGURE_DEPENDS "os/posix/*.c") 
file(GLOB ARCH_SOURCES CONFIGURE_DEPENDS "motherboard/${MODVM_ARCH}/*.c")

# Discover Hypervisor-specific sources (currently focusing on KVM)
file(GLOB HYPERVISOR_SOURCES CONFIGURE_DEPENDS "hypervisor/kvm/*.c")
file(GLOB HYPERVISOR_ARCH_SOURCES CONFIGURE_DEPENDS "hypervisor/kvm/${MODVM_ARCH}/*.c")

# Build an OBJECT library
# Using an OBJECT library allows us to compile these files into object files 
# without creating a heavy static/shared library file, aiding in modular linking.
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

# Export public header directories
target_include_directories(modvm_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Bind dependencies directly to the OBJECT library
target_link_libraries(modvm_core PUBLIC Threads::Threads)

# Compile the main application entry point
add_executable(modvm main.c)

# Modern CMake allows linking directly against OBJECT libraries. 
# It automatically passes the underlying .o files to the final linker stage.
target_link_libraries(modvm PRIVATE modvm_core)