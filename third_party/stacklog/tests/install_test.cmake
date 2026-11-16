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

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${STACKLOG_BINARY_DIR}" --prefix "${install_prefix}"
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
    "-DCMAKE_PREFIX_PATH=${install_prefix}" "-DCMAKE_BUILD_TYPE=Debug")
if(DEFINED STACKLOG_CMAKE_GENERATOR AND NOT STACKLOG_CMAKE_GENERATOR STREQUAL "")
    list(APPEND configure_command -G "${STACKLOG_CMAKE_GENERATOR}")
endif()

execute_process(COMMAND ${configure_command}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "consumer configure 失败:\n${configure_output}\n${configure_error}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" --parallel 4
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "consumer build 失败:\n${build_output}\n${build_error}")
endif()

set(consumer_exe "${consumer_build}/stacklog_consumer")
if(WIN32)
    set(consumer_exe "${consumer_build}/Debug/stacklog_consumer.exe")
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
