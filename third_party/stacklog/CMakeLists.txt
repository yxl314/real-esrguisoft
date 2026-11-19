cmake_minimum_required(VERSION 3.16)

project(stacklog
    VERSION 0.2.0
    DESCRIPTION "面向 C++ / Qt 项目的轻量日志库"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 平台端口：核心 stacklog.cpp 不含平台宏，适配器按平台二选一编译。
if(WIN32)
    set(STACKLOG_PLATFORM_SOURCE src/platform_win32.cpp)
else()
    set(STACKLOG_PLATFORM_SOURCE src/platform_posix.cpp)
endif()

add_library(stacklog STATIC src/stacklog.cpp ${STACKLOG_PLATFORM_SOURCE})
add_library(stacklog::stacklog ALIAS stacklog)

# 静态库也要位置无关：本库持有 thread_local 状态，被链进共享对象（例如
# Python 扩展 .so）时，非 PIC 的目标文件会产生 local-exec 的 TLS 重定位，
# 链接器会直接拒绝：
#   relocation R_X86_64_TPOFF32 ... can not be used when making a shared object
set_target_properties(stacklog PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(stacklog PUBLIC cxx_std_11)

target_include_directories(stacklog PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(stacklog PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)
target_link_libraries(stacklog PUBLIC Threads::Threads)

# 作为子项目（add_subdirectory）使用时只提供构建目标，不生成安装与
# CMake 包配置文件，避免打包工具的暂存前缀影响配置阶段。
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
install(TARGETS stacklog
    EXPORT stacklogTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

install(EXPORT stacklogTargets
    FILE stacklogTargets.cmake
    NAMESPACE stacklog::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stacklog)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/stacklogConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/stacklogConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stacklog)

# 0.x 阶段 minor 版本仍可能调整 API，因此只保证 0.2.x 之间兼容。
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/stacklogConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/stacklogConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/stacklogConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stacklog)

endif()

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(STACKLOG_DEFAULT_TESTS ON)
    set(STACKLOG_DEFAULT_EXAMPLES ON)
else()
    set(STACKLOG_DEFAULT_TESTS OFF)
    set(STACKLOG_DEFAULT_EXAMPLES OFF)
endif()

option(STACKLOG_BUILD_TESTS "构建测试" ${STACKLOG_DEFAULT_TESTS})
option(STACKLOG_BUILD_EXAMPLES "构建示例" ${STACKLOG_DEFAULT_EXAMPLES})
option(STACKLOG_BUILD_BENCHMARKS "构建性能基准" OFF)

if(STACKLOG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(STACKLOG_BUILD_EXAMPLES)
    add_subdirectory(examples/cpp_demo)
endif()

if(STACKLOG_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
