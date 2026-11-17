# 安装、find_package 和独立 consumer 的自动验收。
if(NOT DEFINED STACKLOG_BINARY_DIR OR NOT DEFINED STACKLOG_SOURCE_DIR)
    message(FATAL_ERROR "缺少 STACKLOG_BINARY_DIR 或 STACKLOG_SOURCE_DIR")
endif()

set(install_prefix "${STACKLOG_BINARY_DIR}/install-test/prefix")
set(consumer_dir "${STACKLOG_BINARY_DIR}/install-test/consumer")
set(consumer_build "${consumer_dir}/build")
set(run_dir "${consumer_dir}/run")

file(REMOVE_RECURSE "${STACKLOG_BINARY_DIR}/install-test")
file(MAKE_DIRECTORY "${consumer_dir}" "${run_dir}")

# 多配置生成器（Visual Studio）的 install/build 必须显式指定配置。add_test 用
# $<CONFIG> 把 ctest -C 选中的配置名传进来；单配置生成器不传 --config，否则导出
# 的 imported target 会带上配置名，与无配置名的单配置构建对不上。
set(build_config "Debug")
if(DEFINED STACKLOG_BUILD_CONFIG AND NOT STACKLOG_BUILD_CONFIG STREQUAL "")
    set(build_config "${STACKLOG_BUILD_CONFIG}")
elseif(DEFINED ENV{CTEST_CONFIGURATION_TYPE} AND NOT "$ENV{CTEST_CONFIGURATION_TYPE}" STREQUAL "")
    set(build_config "$ENV{CTEST_CONFIGURATION_TYPE}")
endif()
set(config_args "")
if(STACKLOG_MULTI_CONFIG)
    set(config_args "--config" "${build_config}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${STACKLOG_BINARY_DIR}" --prefix "${install_prefix}" ${config_args}
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "安装失败:\n${install_output}\n${install_error}")
endif()

file(WRITE "${consumer_dir}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.16)\n"
"project(stacklog_install_consumer LANGUAGES CXX)\n"
"find_package(stacklog 0.2 REQUIRED)\n"
"add_executable(stacklog_consumer main.cpp)\n"
"target_link_libraries(stacklog_consumer PRIVATE stacklog::stacklog)\n")

file(WRITE "${consumer_dir}/main.cpp"
"#include <stacklog/stacklog.hpp>\n"
"#include <string>\n"
"\n"
"int main()\n"
"{\n"
"    stacklog::config cfg;\n"
"    cfg.dir = \"logs\";\n"
"    cfg.base_name = \"install_consumer\";\n"
"    cfg.max_file_size = 1024;\n"
"    cfg.max_files = 2;\n"
"    cfg.min_level = stacklog::level::info;\n"
"    if (!stacklog::init(cfg)) return 1;\n"
"    STACKLOG_INFO() << \"install consumer version=\" << STACKLOG_VERSION_STRING;\n"
"    stacklog::shutdown();\n"
"    return 0;\n"
"}\n")

set(configure_command "${CMAKE_COMMAND}" -S "${consumer_dir}" -B "${consumer_build}"
    "-DCMAKE_PREFIX_PATH=${install_prefix}" "-DCMAKE_BUILD_TYPE=${build_config}")
if(DEFINED STACKLOG_CMAKE_GENERATOR AND NOT STACKLOG_CMAKE_GENERATOR STREQUAL "")
    list(APPEND configure_command -G "${STACKLOG_CMAKE_GENERATOR}")
endif()
# Windows 上用 Ninja 等单配置生成器时，cmake 在本脚本里找不到默认编译器，需要
# 显式传入；Visual Studio 生成器自己选择工具链，传编译器路径反而会造成歧义。
if(DEFINED STACKLOG_CXX_COMPILER AND NOT STACKLOG_CXX_COMPILER STREQUAL "" AND
   NOT STACKLOG_CMAKE_GENERATOR MATCHES "Visual Studio")
    list(APPEND configure_command "-DCMAKE_CXX_COMPILER=${STACKLOG_CXX_COMPILER}")
endif()

execute_process(COMMAND ${configure_command}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "consumer configure 失败:\n${configure_output}\n${configure_error}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" ${config_args} --parallel 4
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "consumer build 失败:\n${build_output}\n${build_error}")
endif()

# 单配置生成器（Makefiles/Ninja）直接输出在构建根目录，多配置生成器在
# Debug/Release 子目录；按候选路径探测，避免依赖生成器类型。
set(consumer_exe "")
foreach(candidate
    "${consumer_build}/${build_config}/stacklog_consumer.exe"
    "${consumer_build}/stacklog_consumer"
    "${consumer_build}/stacklog_consumer.exe"
    "${consumer_build}/Debug/stacklog_consumer.exe"
    "${consumer_build}/Release/stacklog_consumer.exe")
    if(EXISTS "${candidate}")
        set(consumer_exe "${candidate}")
        break()
    endif()
endforeach()
if(consumer_exe STREQUAL "")
    message(FATAL_ERROR "找不到 consumer 可执行文件: ${consumer_build}")
endif()

execute_process(COMMAND "${consumer_exe}" WORKING_DIRECTORY "${run_dir}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "consumer 运行失败:\n${run_output}\n${run_error}")
endif()

if(NOT EXISTS "${run_dir}/logs/install_consumer.log")
    message(FATAL_ERROR "consumer 没有生成日志文件")
endif()

message(STATUS "install/find_package consumer test passed")
