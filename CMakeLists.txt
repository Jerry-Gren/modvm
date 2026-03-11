cmake_minimum_required(VERSION 3.15)

project(modvm
    VERSION 0.1.0
    DESCRIPTION "A Modular and Multi-arch Virtual Machine Monitor"
    LANGUAGES C
)

# Enforce C11 standard and disable compiler-specific extensions (e.g., GNU extensions)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Enable strict compiler warnings for quality assurance
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror -Wno-unused-value)
endif()

option(ENABLE_ASAN "Enable AddressSanitizer for memory error detection" ON)

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer is ENABLED")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Detect host CPU architecture and set corresponding preprocessor macros
if(CMAKE_SYSTEM_PROCESSOR MATCHES "amd64|x86_64|AMD64")
    set(MODVM_ARCH "x86_64")
    add_compile_definitions(ARCH_X86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    set(MODVM_ARCH "aarch64")
    add_compile_definitions(ARCH_AARCH64)
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Detect host Operating System and set corresponding preprocessor macros
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MODVM_OS "linux")
    add_compile_definitions(OS_LINUX)
else()
    message(FATAL_ERROR "Unsupported OS: ${CMAKE_SYSTEM_NAME}")
endif()

message(STATUS "Configuring ModVM for ${MODVM_OS}-${MODVM_ARCH}")

# Locate and configure the standard threading library (handles -pthread automatically)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Enable CTest support for automated testing
enable_testing()

# Delegate build tasks to source and test subdirectories
add_subdirectory(src)
add_subdirectory(tests)