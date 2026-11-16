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

# 多配置生成器需要 --config；单配置的 Ninja/Makefiles 传空值会报错。
set(install_config_args)
if(SINGLEPROC_BUILD_TYPE)
    list(APPEND install_config_args --config "${SINGLEPROC_BUILD_TYPE}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${SINGLEPROC_BINARY_DIR}" ${install_config_args} --prefix "${prefix}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "install failed:\n${install_output}${install_error}")
endif()

set(extra_configure_args)
if(SINGLEPROC_CXX_COMPILER)
    list(APPEND extra_configure_args "-DCMAKE_CXX_COMPILER=${SINGLEPROC_CXX_COMPILER}")
endif()
if(SINGLEPROC_CXX_FLAGS)
    list(APPEND extra_configure_args "-DCMAKE_CXX_FLAGS=${SINGLEPROC_CXX_FLAGS}")
endif()
if(SINGLEPROC_EXE_LINKER_FLAGS)
    list(APPEND extra_configure_args "-DCMAKE_EXE_LINKER_FLAGS=${SINGLEPROC_EXE_LINKER_FLAGS}")
endif()
if(SINGLEPROC_GENERATOR)
    list(APPEND extra_configure_args "-G" "${SINGLEPROC_GENERATOR}")
endif()
if(SINGLEPROC_MAKE_PROGRAM)
    list(APPEND extra_configure_args "-DCMAKE_MAKE_PROGRAM=${SINGLEPROC_MAKE_PROGRAM}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -S "${SINGLEPROC_SOURCE_DIR}/tests/consumer"
            -B "${consumer_build}"
            "-DCMAKE_PREFIX_PATH=${prefix}"
            "-DCMAKE_BUILD_TYPE=Debug"
            ${extra_configure_args}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "consumer configure failed:\n${configure_output}${configure_error}")
endif()

set(consumer_config_args)
if(SINGLEPROC_BUILD_TYPE AND NOT SINGLEPROC_GENERATOR MATCHES "Ninja|Makefiles")
    list(APPEND consumer_config_args --config Debug)
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" ${consumer_config_args}
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "consumer build failed:\n${build_output}${build_error}")
endif()

file(MAKE_DIRECTORY "${lock_directory}")

# Windows 下可执行文件带 .exe 后缀。
set(consumer_executable "${consumer_build}/consumer")
if(WIN32)
    set(consumer_executable "${consumer_executable}.exe")
endif()

execute_process(
    COMMAND "${consumer_executable}" "${lock_directory}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "consumer run failed (${run_result}):\n${run_output}${run_error}")
endif()
