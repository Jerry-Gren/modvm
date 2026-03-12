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

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(STATUS "Clang detected: Enabling Static Thread Safety Analysis")
    add_compile_options(-Wthread-safety)
    add_compile_definitions(WARN_CONTEXT_ANALYSIS)
endif()

option(ENABLE_SANITIZERS "Enable Address and Undefined Behavior Sanitizers" ON)

if(ENABLE_SANITIZERS)
    message(STATUS "Sanitizers (ASAN & UBSAN) are ENABLED")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

option(ENABLE_HARDENING "Enable security hardening flags (PIE, RELRO, Stack Protector)" ON)

if(ENABLE_HARDENING)
    message(STATUS "Security Hardening is ENABLED")
    
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    
    add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2 -O2)
    
    add_link_options("-Wl,-z,relro,-z,now")
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