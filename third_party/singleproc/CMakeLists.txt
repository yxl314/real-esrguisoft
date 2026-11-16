cmake_minimum_required(VERSION 3.16)

project(singleproc
    VERSION 0.2.0
    DESCRIPTION "POSIX / Windows 单实例运行库"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 平台相关实现单独成文件，公共逻辑保持一致。
if(WIN32)
    set(SINGLEPROC_PLATFORM_SOURCE src/singleproc_win32.cpp)
else()
    set(SINGLEPROC_PLATFORM_SOURCE src/singleproc_posix.cpp)
endif()

add_library(singleproc STATIC
    src/singleproc.cpp
    ${SINGLEPROC_PLATFORM_SOURCE})
add_library(singleproc::singleproc ALIAS singleproc)
target_compile_features(singleproc PUBLIC cxx_std_11)

target_include_directories(singleproc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(singleproc PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    # 使用 Win32 文件与管道 API；kernel32 由默认链接库提供。
    target_compile_definitions(singleproc PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

install(TARGETS singleproc
    EXPORT singleprocTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

install(EXPORT singleprocTargets
    FILE singleprocTargets.cmake
    NAMESPACE singleproc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/singleproc)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/singleprocConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/singleprocConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/singleproc)

# 0.x 阶段 minor 版本仍可能调整 API，因此只保证 0.2.x 之间兼容；
# 1.0 后再切回 SameMajorVersion。
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/singleprocConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/singleprocConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/singleprocConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/singleproc)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(SINGLEPROC_DEFAULT_TESTS ON)
    set(SINGLEPROC_DEFAULT_EXAMPLES ON)
else()
    set(SINGLEPROC_DEFAULT_TESTS OFF)
    set(SINGLEPROC_DEFAULT_EXAMPLES OFF)
endif()

option(SINGLEPROC_BUILD_TESTS "构建测试" ${SINGLEPROC_DEFAULT_TESTS})
option(SINGLEPROC_BUILD_EXAMPLES "构建示例" ${SINGLEPROC_DEFAULT_EXAMPLES})

if(SINGLEPROC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SINGLEPROC_BUILD_EXAMPLES)
    add_executable(singleproc_example_basic examples/basic.cpp)
    target_link_libraries(singleproc_example_basic PRIVATE singleproc::singleproc)
endif()
