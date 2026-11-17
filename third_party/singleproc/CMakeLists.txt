cmake_minimum_required(VERSION 3.16)

project(singleproc
    VERSION 0.3.0
    DESCRIPTION "Linux/POSIX 与 Windows 单实例运行库"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 平台实现一次只编译一个：Linux/POSIX 用 flock + Unix socket，
# Windows 用 LockFileEx + 命名管道。业务逻辑不出现平台宏分支。
if(WIN32)
    set(SINGLEPROC_PLATFORM_SOURCE src/platform_win32.cpp)
else()
    set(SINGLEPROC_PLATFORM_SOURCE src/platform_posix.cpp)
endif()

add_library(singleproc STATIC src/singleproc.cpp ${SINGLEPROC_PLATFORM_SOURCE})
add_library(singleproc::singleproc ALIAS singleproc)
target_compile_features(singleproc PUBLIC cxx_std_11)

target_include_directories(singleproc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(singleproc PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    # 源码含中文注释与中文错误文本，必须显式声明 UTF-8，否则 MSVC 按系统
    # 代码页（如 CP936）解析会报 C4819 并可能让字符串乱码。不加 /WX。
    target_compile_options(singleproc PRIVATE /W4 /utf-8)
endif()

if(WIN32)
    # 用户 SID、SDDL 安全描述符来自 advapi32；静态库把依赖传给下游。
    target_link_libraries(singleproc PUBLIC advapi32)
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

# 0.x 阶段 minor 版本仍可能调整 API，因此只保证同一 minor 内兼容；
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
    if(MSVC)
        target_compile_options(singleproc_example_basic PRIVATE /W4 /utf-8)
    endif()
endif()
