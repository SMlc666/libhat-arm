cmake_minimum_required(VERSION 3.12)

set(LIBHAT_VERSION_MAJOR 0)
set(LIBHAT_VERSION_MINOR 4)
set(LIBHAT_VERSION_PATCH 0)
set(LIBHAT_VERSION ${LIBHAT_VERSION_MAJOR}.${LIBHAT_VERSION_MINOR}.${LIBHAT_VERSION_PATCH})

project(libhat
    VERSION ${LIBHAT_VERSION}
    DESCRIPTION "A high-performance, modern, C++20 library designed around game hacking"
    HOMEPAGE_URL "https://github.com/BasedInc/libhat"
    LANGUAGES C CXX
)

# Platform and Architecture Detection
include(CMakeDetermineSystem)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LIBHAT_LINUX 1)
    message(STATUS "Detected OS: Linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(LIBHAT_WINDOWS 1)
    message(STATUS "Detected OS: Windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(LIBHAT_MACOS 1)
    message(STATUS "Detected OS: macOS")
else()
    message(WARNING "Unsupported OS: ${CMAKE_SYSTEM_NAME}")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
    set(LIBHAT_ARM 1)
    message(STATUS "Detected Arch: ARM")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|AARCH64")
    set(LIBHAT_AARCH64 1)
    message(STATUS "Detected Arch: AArch64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(LIBHAT_X86_64 1)
    message(STATUS "Detected Arch: x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i.86|x86")
    set(LIBHAT_X86 1)
    message(STATUS "Detected Arch: x86")
else()
    message(WARNING "Unsupported Processor: ${CMAKE_SYSTEM_PROCESSOR}")
endif()
# Combine 32-bit and 64-bit flags for convenience where applicable
if(LIBHAT_X86 OR LIBHAT_X86_64)
    set(LIBHAT_IS_X86 1)
endif()
if(LIBHAT_ARM OR LIBHAT_AARCH64)
    set(LIBHAT_IS_ARM 1)
endif()


include(CheckCXXCompilerFlag)

option(LIBHAT_STATIC_C_LIB "Build a static version of the C library" OFF)
option(LIBHAT_SHARED_C_LIB "Build a shared version of the C library" OFF)
option(LIBHAT_INSTALL_TARGET "Creates install rules for the libhat target" ON)
option(LIBHAT_DISABLE_SSE "Disables SSE scanning" OFF)
option(LIBHAT_DISABLE_AVX512 "Disables AVX512 scanning" OFF)
option(LIBHAT_DISABLE_NEON "Disables NEON scanning (ARM/AArch64)" OFF)
option(LIBHAT_TESTING "Enable tests" OFF)
option(LIBHAT_TESTING_ASAN "Enable address sanitizer when testing" ON)
option(LIBHAT_TESTING_SDE "Run tests using Intel Software Development Emulator" ON)

option(LIBHAT_HINT_X86_64 "Enables support for the x86_64 scan hint, requires a small (less than 1KB) data table" ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Just assume that the compiler accepts the required x86/x64 options if it'll accept -mmmx
    # CMake architecture detection SUCKS!!!
    check_cxx_compiler_flag("-mmmx" LIBHAT_COMPILER_X86_OPTIONS)

    if(LIBHAT_COMPILER_X86_OPTIONS)
        set_source_files_properties(src/arch/x86/SSE.cpp PROPERTIES COMPILE_FLAGS "-msse4.1")
        set_source_files_properties(src/arch/x86/AVX2.cpp PROPERTIES COMPILE_FLAGS "-mavx -mavx2 -mbmi")
        set_source_files_properties(src/arch/x86/AVX512.cpp PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512bw -mbmi")
        set_source_files_properties(src/arch/x86/System.cpp PROPERTIES COMPILE_FLAGS "-mxsave")
    endif()
endif()

if(LIBHAT_TESTING AND LIBHAT_TESTING_ASAN)
    if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
    endif()
    # TODO: Linux
endif()

set(LIBHAT_SRC
    src/Scanner.cpp
    src/System.cpp

    src/os/linux/MemoryProtector.cpp

    src/os/unix/Process.cpp
    src/os/unix/System.cpp

    src/os/win32/MemoryProtector.cpp
    src/os/win32/Process.cpp
    src/os/win32/Scanner.cpp
    src/os/win32/System.cpp

    src/arch/x86/SSE.cpp
    src/arch/x86/AVX2.cpp
    src/arch/x86/AVX512.cpp
    src/arch/x86/System.cpp

    src/arch/arm/System.cpp
)

# Conditionally add ARM NEON scanner source
if(LIBHAT_IS_ARM AND NOT LIBHAT_DISABLE_NEON)
    list(APPEND LIBHAT_SRC src/arch/arm/Scanner.cpp)
    message(STATUS "Enabling NEON optimized scanner")
endif()

add_library(libhat STATIC ${LIBHAT_SRC})
add_library(libhat::libhat ALIAS libhat)

if(UNIX)
    set_target_properties(libhat PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_features(libhat PUBLIC cxx_std_20)

if (MSVC)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(libhat PRIVATE /clang:-Wall /clang:-Wextra /clang:-Wconversion /clang:-Werror)
    else()
        target_compile_options(libhat PRIVATE /W3 /WX)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(libhat PRIVATE -Wall -Wextra -Wconversion -Werror
            # temp fix for Mac OSX CI failing due to incorrect LIBHAT_COMPILER_X86_OPTIONS value
            -Wno-unused-command-line-argument
    )
endif()

target_include_directories(libhat PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(libhat PUBLIC
    # Platform/Arch defines
    "$<$<BOOL:${LIBHAT_LINUX}>:LIBHAT_LINUX>"
    "$<$<BOOL:${LIBHAT_WINDOWS}>:LIBHAT_WINDOWS>"
    "$<$<BOOL:${LIBHAT_MACOS}>:LIBHAT_MACOS>"
    "$<$<BOOL:${LIBHAT_ARM}>:LIBHAT_ARM>"
    "$<$<BOOL:${LIBHAT_AARCH64}>:LIBHAT_AARCH64>"
    "$<$<BOOL:${LIBHAT_X86_64}>:LIBHAT_X86_64>"
    "$<$<BOOL:${LIBHAT_X86}>:LIBHAT_X86>"
    # Feature flags
    "$<$<BOOL:${LIBHAT_DISABLE_SSE}>:LIBHAT_DISABLE_SSE>"
    "$<$<BOOL:${LIBHAT_DISABLE_AVX512}>:LIBHAT_DISABLE_AVX512>"
    "$<$<BOOL:${LIBHAT_HINT_X86_64}>:LIBHAT_HINT_X86_64>"
    "$<$<BOOL:${LIBHAT_DISABLE_NEON}>:LIBHAT_DISABLE_NEON>"
)

# Add NEON compile flags for ARM targets if NEON is enabled
if(LIBHAT_IS_ARM AND NOT LIBHAT_DISABLE_NEON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # For AArch64, NEON is typically enabled by default.
        # The -mfpu=neon flag is often unnecessary or unrecognized for aarch64-linux-gnu-g++.
        # If targeting 32-bit ARM specifically, -mfpu=neon might be needed.
        # We'll rely on the compiler default for AArch64 for now.
        # target_compile_options(libhat PRIVATE -mfpu=neon) # Removed this line
        message(STATUS "NEON enabled for ARM target (relying on compiler defaults for AArch64).")
    elseif(MSVC)
        # MSVC ARM compilation might enable NEON by default or require different flags.
        # Check MSVC documentation for ARM NEON flags if needed.
        message(STATUS "MSVC ARM target detected - NEON might be enabled by default or require specific flags.")
    endif()
endif()

if(LIBHAT_STATIC_C_LIB OR LIBHAT_SHARED_C_LIB)
    set(LIBHAT_C_SOURCES
        src/c/libhat.cpp
    )

    if(LIBHAT_STATIC_C_LIB)
        add_library(libhat_c STATIC ${LIBHAT_C_SOURCES})
        if(UNIX)
            set_target_properties(libhat_c PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif()
    else()
        add_library(libhat_c SHARED ${LIBHAT_C_SOURCES})
        target_compile_definitions(libhat_c
            PRIVATE LIBHAT_BUILD_SHARED_LIB
            INTERFACE LIBHAT_USE_SHARED_LIB
        )
    endif()

    add_library(libhat::libhat_c ALIAS libhat_c)
    target_link_libraries(libhat_c PRIVATE libhat)
    target_include_directories(libhat_c PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
        $<INSTALL_INTERFACE:include>
    )
endif()

if(LIBHAT_TESTING)
    include(CTest)
    enable_testing()
    add_subdirectory(test)
endif()

if(LIBHAT_INSTALL_TARGET)
    install(TARGETS libhat
        EXPORT libhat-targets
        RUNTIME DESTINATION "bin"
        ARCHIVE DESTINATION "lib"
        LIBRARY DESTINATION "lib"
    )
endif()
