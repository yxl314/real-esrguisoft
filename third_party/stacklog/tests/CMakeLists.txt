# 三级测试：unit（假平台端口）/ integration（真实文件系统复合流程）/ smoke（完整业务线）。

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(STACKLOG_TEST_WARNINGS -Wall -Wextra -Wpedantic)
elseif(MSVC)
    set(STACKLOG_TEST_WARNINGS /W4 /utf-8)
endif()

# ---- 单元测试：核心 + 假端口，不链接真实平台适配器 ----
add_executable(stacklog_unit
    unit/test_stacklog.cpp
    support/fake_platform.cpp
    ${PROJECT_SOURCE_DIR}/src/stacklog.cpp)
target_include_directories(stacklog_unit PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(stacklog_unit PRIVATE Threads::Threads)
target_compile_options(stacklog_unit PRIVATE ${STACKLOG_TEST_WARNINGS})
add_test(NAME stacklog_unit COMMAND stacklog_unit)
set_tests_properties(stacklog_unit PROPERTIES LABELS "unit")

# ---- 集成测试：真实文件系统 + 真实平台适配器，按复合流程组织 ----
set(STACKLOG_INTEGRATION_TESTS
    format_flow
    rotation_flow
    trace_flow
    cross_thread_flow
    threading_flow
    failure_recovery_flow
    reliability_flow
    rotation_stress_flow
    utf8_path_flow)
foreach(flow ${STACKLOG_INTEGRATION_TESTS})
    add_executable(stacklog_integration_${flow} integration/test_${flow}.cpp)
    target_link_libraries(stacklog_integration_${flow} PRIVATE stacklog::stacklog)
    target_compile_options(stacklog_integration_${flow} PRIVATE ${STACKLOG_TEST_WARNINGS})
    add_test(NAME stacklog_integration_${flow} COMMAND stacklog_integration_${flow})
    set_tests_properties(stacklog_integration_${flow} PROPERTIES LABELS "integration")
endforeach()
set_tests_properties(
    stacklog_integration_reliability_flow
    stacklog_integration_rotation_stress_flow
    PROPERTIES TIMEOUT 60)

# ---- 冒烟测试：核心完整业务流程 ----
set(STACKLOG_SMOKE_TESTS
    logging_flow
    recovery_flow)
foreach(flow ${STACKLOG_SMOKE_TESTS})
    add_executable(stacklog_smoke_${flow} smoke/test_${flow}.cpp)
    target_link_libraries(stacklog_smoke_${flow} PRIVATE stacklog::stacklog)
    target_compile_options(stacklog_smoke_${flow} PRIVATE ${STACKLOG_TEST_WARNINGS})
    add_test(NAME stacklog_smoke_${flow} COMMAND stacklog_smoke_${flow})
    set_tests_properties(stacklog_smoke_${flow} PROPERTIES LABELS "smoke")
endforeach()

# 多配置生成器（Visual Studio 等）的 install/build 需要显式配置名，
# 单配置生成器则沿用默认行为；由 install_test.cmake 区分。
if(CMAKE_CONFIGURATION_TYPES)
    set(STACKLOG_MULTI_CONFIG ON)
else()
    set(STACKLOG_MULTI_CONFIG OFF)
endif()

# ---- 安装 + consumer 集成测试 ----
add_test(NAME stacklog_install
    COMMAND "${CMAKE_COMMAND}"
        "-DSTACKLOG_BINARY_DIR=${PROJECT_BINARY_DIR}"
        "-DSTACKLOG_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DSTACKLOG_CMAKE_GENERATOR=${CMAKE_GENERATOR}"
        "-DSTACKLOG_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DSTACKLOG_MULTI_CONFIG=${STACKLOG_MULTI_CONFIG}"
        "-DSTACKLOG_BUILD_CONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/install_test.cmake")
set_tests_properties(stacklog_install PROPERTIES LABELS "integration" TIMEOUT 120)

# ---- qmake + Qt consumer 集成测试 ----
find_program(QMAKE_EXECUTABLE NAMES qmake qmake6)
if(QMAKE_EXECUTABLE)
    add_test(NAME stacklog_qmake
        COMMAND "${CMAKE_COMMAND}"
            "-DQMAKE_EXECUTABLE=${QMAKE_EXECUTABLE}"
            "-DSTACKLOG_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DSTACKLOG_BINARY_DIR=${PROJECT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/qmake_test.cmake")
    set_tests_properties(stacklog_qmake PROPERTIES LABELS "integration" TIMEOUT 120)
    message(STATUS "stacklog qmake integration test enabled: ${QMAKE_EXECUTABLE}")
else()
    message(STATUS "stacklog qmake integration test disabled: qmake not found")
endif()
