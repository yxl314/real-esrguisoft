if(NOT SINGLEPROC_BINARY_DIR)
    message(FATAL_ERROR "SINGLEPROC_BINARY_DIR is required")
endif()
if(NOT IS_ABSOLUTE "${SINGLEPROC_BINARY_DIR}")
    message(FATAL_ERROR "SINGLEPROC_BINARY_DIR must be an absolute path")
endif()
if(SINGLEPROC_BINARY_DIR STREQUAL "/")
    message(FATAL_ERROR "refusing to use / as the build directory")
endif()

set(prefix "${SINGLEPROC_BINARY_DIR}/install-test/prefix")
set(consumer_build "${SINGLEPROC_BINARY_DIR}/install-test/consumer-build")
set(lock_directory "${SINGLEPROC_BINARY_DIR}/install-test/locks")

file(REMOVE_RECURSE "${SINGLEPROC_BINARY_DIR}/install-test")

# 多配置生成器（Visual Studio 等）需要显式指定配置：安装、构建、运行三处
# 必须用同一个配置，否则会出现 Debug consumer 链接 Release 库的 LNK2038。
set(install_args --install "${SINGLEPROC_BINARY_DIR}" --prefix "${prefix}")
if(SINGLEPROC_MULTI_CONFIG)
    list(APPEND install_args --config "${SINGLEPROC_CONFIG}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${install_args}
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "install failed:\n${install_output}${install_error}")
endif()

set(extra_configure_args)
if(SINGLEPROC_GENERATOR)
    # consumer 必须复用父工程的生成器：在多配置环境下直接构造的 consumer
    # 若落到别的生成器（例如没有 MSVC 开发环境的 Ninja），会找不到 rc/cl。
    list(APPEND extra_configure_args -G "${SINGLEPROC_GENERATOR}")
endif()
if(NOT SINGLEPROC_MULTI_CONFIG AND SINGLEPROC_CONFIG)
    list(APPEND extra_configure_args "-DCMAKE_BUILD_TYPE=${SINGLEPROC_CONFIG}")
endif()
if(SINGLEPROC_CXX_COMPILER)
    list(APPEND extra_configure_args "-DCMAKE_CXX_COMPILER=${SINGLEPROC_CXX_COMPILER}")
endif()
if(SINGLEPROC_CXX_FLAGS)
    list(APPEND extra_configure_args "-DCMAKE_CXX_FLAGS=${SINGLEPROC_CXX_FLAGS}")
endif()
if(SINGLEPROC_EXE_LINKER_FLAGS)
    list(APPEND extra_configure_args "-DCMAKE_EXE_LINKER_FLAGS=${SINGLEPROC_EXE_LINKER_FLAGS}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -S "${SINGLEPROC_SOURCE_DIR}/tests/consumer"
            -B "${consumer_build}"
            "-DCMAKE_PREFIX_PATH=${prefix}"
            ${extra_configure_args}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "consumer configure failed:\n${configure_output}${configure_error}")
endif()

set(build_args --build "${consumer_build}")
if(SINGLEPROC_MULTI_CONFIG)
    list(APPEND build_args --config "${SINGLEPROC_CONFIG}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${build_args}
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "consumer build failed:\n${build_output}${build_error}")
endif()

file(MAKE_DIRECTORY "${lock_directory}")

# 单配置生成器把可执行文件放在构建目录根部；多配置生成器放在 <Config>/ 下。
set(consumer_executable "${consumer_build}/consumer${SINGLEPROC_EXE_SUFFIX}")
if(SINGLEPROC_MULTI_CONFIG)
    set(consumer_executable "${consumer_build}/${SINGLEPROC_CONFIG}/consumer${SINGLEPROC_EXE_SUFFIX}")
endif()

execute_process(
    COMMAND "${consumer_executable}" "${lock_directory}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "consumer run failed (${run_result}):\n${run_output}${run_error}")
endif()
