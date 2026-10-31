cmake_minimum_required(VERSION 3.16)

project(TinyX86
    VERSION 1.3.0
    DESCRIPTION "Cross-platform x86-32 CPU & x87 FPU Emulator, PE32 Loader, and Interactive Debugger"
    LANGUAGES C CXX
)

include(CTest)
include(GNUInstallDirs)

# ============================================================================
# 构建选项 (Build Options)
# ============================================================================
option(TINYX86_BUILD_TESTS "Build TinyX86 integration and unit tests" ON)
option(TINYX86_USE_SUBMODULES "Prefer git submodules in deps/ when available on Win32" OFF)
option(TINYX86_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 输出目录统一规范
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(TINYX86_INCLUDE_DIR        "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(TINYX86_COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/compat")
set(TINYX86_CORE_DIR           "${CMAKE_CURRENT_SOURCE_DIR}/core")

# ============================================================================
# 跨平台编译器与链接器配置 (Windows MSVC / MinGW / Linux GCC / macOS Clang)
# ============================================================================
add_library(tinyx86_compiler_flags INTERFACE)

if(MSVC)
    target_compile_options(tinyx86_compiler_flags INTERFACE
        /W3
        /utf-8
        /permissive-
    )
    target_compile_definitions(tinyx86_compiler_flags INTERFACE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(TINYX86_WARNINGS_AS_ERRORS)
        target_compile_options(tinyx86_compiler_flags INTERFACE /WX)
    endif()
else()
    target_compile_options(tinyx86_compiler_flags INTERFACE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )
    if(TINYX86_WARNINGS_AS_ERRORS)
        target_compile_options(tinyx86_compiler_flags INTERFACE -Werror)
    endif()
endif()

# ============================================================================
# 依赖模块检测：默认使用内置跨平台实现以保证全平台 ABI 一致；
# 如需在 Win32 下使用 deps/ 子模块，可通过 -DTINYX86_USE_SUBMODULES=ON 开启
# ============================================================================
set(USE_SUBMODULE_DISASM FALSE)
set(USE_SUBMODULE_PETOOLS FALSE)

if(TINYX86_USE_SUBMODULES AND WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/Core-x86-Disassembler/CMakeLists.txt")
        set(USE_SUBMODULE_DISASM TRUE)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/PETools/CMakeLists.txt")
        set(USE_SUBMODULE_PETOOLS TRUE)
    endif()
endif()

if(USE_SUBMODULE_DISASM)
    message(STATUS "[TinyX86] Using submodule: deps/Core-x86-Disassembler")
    add_subdirectory(deps/Core-x86-Disassembler EXCLUDE_FROM_ALL)
else()
    message(STATUS "[TinyX86] Using built-in cross-platform Core-x86 disassembler (core/disasm.c)")
    add_library(core_x86 STATIC
        "${TINYX86_CORE_DIR}/disasm.c"
        "${TINYX86_COMPAT_INCLUDE_DIR}/disasm.h"
    )
    target_include_directories(core_x86 PUBLIC "${TINYX86_COMPAT_INCLUDE_DIR}")
    target_link_libraries(core_x86 PRIVATE tinyx86_compiler_flags)
endif()

if(USE_SUBMODULE_PETOOLS)
    message(STATUS "[TinyX86] Using submodule: deps/PETools")
    add_subdirectory(deps/PETools EXCLUDE_FROM_ALL)
else()
    message(STATUS "[TinyX86] Using built-in cross-platform PE32 parser (core/PETools.c)")
    add_library(petools STATIC
        "${TINYX86_CORE_DIR}/PETools.c"
        "${TINYX86_COMPAT_INCLUDE_DIR}/PETools.h"
    )
    target_include_directories(petools PUBLIC "${TINYX86_COMPAT_INCLUDE_DIR}")
    target_link_libraries(petools PRIVATE tinyx86_compiler_flags)
endif()

# ============================================================================
# 核心目标 1: tinyx86_cpu (CPU + x87 FPU 模拟引擎静态库)
# ============================================================================
add_library(tinyx86_cpu STATIC
    "${TINYX86_CORE_DIR}/TinyX86.c"
    "${TINYX86_INCLUDE_DIR}/TinyX86.h"
)

target_include_directories(tinyx86_cpu PUBLIC "${TINYX86_INCLUDE_DIR}")
target_link_libraries(tinyx86_cpu
    PUBLIC core_x86
    PRIVATE tinyx86_compiler_flags
)

if(UNIX)
    target_link_libraries(tinyx86_cpu PUBLIC m)
endif()

# ============================================================================
# 核心目标 2: TinyX86 主程序 (Shell + PELoader + Interactive Debugger)
# ============================================================================
add_executable(TinyX86
    "${TINYX86_CORE_DIR}/main.c"
    "${TINYX86_CORE_DIR}/PELoader.c"
    "${TINYX86_INCLUDE_DIR}/PELoader.h"
    "${TINYX86_CORE_DIR}/debugger.c"
    "${TINYX86_INCLUDE_DIR}/debugger.h"
)

target_include_directories(TinyX86 PRIVATE "${TINYX86_INCLUDE_DIR}")
target_link_libraries(TinyX86 PRIVATE
    tinyx86_cpu
    petools
    tinyx86_compiler_flags
)

if(WIN32)
    target_link_libraries(TinyX86 PRIVATE psapi)
endif()

# ============================================================================
# 核心目标 3: 跨平台集成测试 (CTest)
# ============================================================================
if(BUILD_TESTING AND TINYX86_BUILD_TESTS)
    add_executable(cpu_integration_tests
        tests/cpu_integration_tests.c
    )
    target_include_directories(cpu_integration_tests PRIVATE "${TINYX86_INCLUDE_DIR}")
    target_link_libraries(cpu_integration_tests PRIVATE
        tinyx86_cpu
        tinyx86_compiler_flags
    )
    add_test(
        NAME cpu_integration_tests
        COMMAND cpu_integration_tests
    )
endif()

# ============================================================================
# 安装规则 (Install Rules)
# ============================================================================
install(TARGETS TinyX86 tinyx86_cpu core_x86 petools
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(DIRECTORY "${TINYX86_INCLUDE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tinyx86"
)

# ============================================================================
# 构建摘要输出 (Configuration Summary)
# ============================================================================
message(STATUS "=======================================================")
message(STATUS "  TinyX86 ${PROJECT_VERSION} Configuration Summary")
message(STATUS "  Host OS          : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler         : ${CMAKE_C_COMPILER_ID} (${CMAKE_C_COMPILER_VERSION})")
message(STATUS "  Pointer Size     : ${CMAKE_SIZEOF_VOID_P} bytes")
message(STATUS "  Build Tests      : ${TINYX86_BUILD_TESTS}")
message(STATUS "=======================================================")
