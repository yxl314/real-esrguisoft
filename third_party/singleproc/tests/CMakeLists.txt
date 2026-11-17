# 测试分级与命名遵循 ai_guidelines/测试策略.md：
# - unit/：src/singleproc.cpp + tests/support/fake_platform.cpp（平台端口替身），
#          不链接真实 platform_posix.cpp / platform_win32.cpp；
# - integration/：真实平台的文件系统、多进程、socket/命名管道与安装流程；
# - smoke/：核心完整业务流程，按进程真实退出码判定。
# 三级测试用 CTest LABELS 区分：ctest -L unit / -L integration / -L smoke。

# 测试目标统一使用与产品目标一致的严格告警集。
function(singleproc_test_target target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    endif()
endfunction()

# ——— 单元测试：假平台端口 ———
# 把 src/singleproc.cpp 与 tests/support/fake_platform.cpp 一起编译，覆盖全部
# 公开接口与公开函数分支；不链接 singleproc 库的真实平台实现。
add_executable(singleproc_unit_tests
    unit/test_singleproc.cpp
    support/fake_platform.cpp
    ${PROJECT_SOURCE_DIR}/src/singleproc.cpp)
target_include_directories(singleproc_unit_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR})
singleproc_test_target(singleproc_unit_tests)
add_test(NAME singleproc_unit_tests COMMAND singleproc_unit_tests)
set_tests_properties(singleproc_unit_tests PROPERTIES LABELS "unit" TIMEOUT 30)

# ——— 集成测试：真实平台 ———
if(WIN32)
    add_executable(singleproc_win32_integration integration/test_win32.cpp)
    target_link_libraries(singleproc_win32_integration PRIVATE singleproc::singleproc)
    target_include_directories(singleproc_win32_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    singleproc_test_target(singleproc_win32_integration)
    add_test(NAME singleproc_win32_integration COMMAND singleproc_win32_integration)
    set_tests_properties(singleproc_win32_integration PROPERTIES LABELS "integration" TIMEOUT 120)

    # 确定性复现“客户端在 ConnectNamedPipe 之前连上并立刻关闭”的竞态：
    # 测试内联一份平台实现并用钩子拉长窗口，只编译不链接库（避免符号重复）。
    add_executable(singleproc_win32_connect_race_integration integration/test_win32_connect_race.cpp)
    target_include_directories(singleproc_win32_connect_race_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(singleproc_win32_connect_race_integration PRIVATE advapi32)
    singleproc_test_target(singleproc_win32_connect_race_integration)
    add_test(NAME singleproc_win32_connect_race_integration COMMAND singleproc_win32_connect_race_integration)
    set_tests_properties(singleproc_win32_connect_race_integration PROPERTIES LABELS "integration" TIMEOUT 60)
else()
    # 探针只被 POSIX 进程集成测试使用。
    add_executable(singleproc_probe support/probe.cpp)
    target_link_libraries(singleproc_probe PRIVATE singleproc::singleproc)
    singleproc_test_target(singleproc_probe)

    add_executable(singleproc_guard_integration integration/test_guard.cpp)
    add_executable(singleproc_doorbell_integration integration/test_doorbell.cpp)
    add_executable(singleproc_process_integration integration/test_process.cpp)
    add_dependencies(singleproc_process_integration singleproc_probe)

    foreach(target singleproc_guard_integration singleproc_doorbell_integration singleproc_process_integration)
        target_link_libraries(${target} PRIVATE singleproc::singleproc)
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        singleproc_test_target(${target})
    endforeach()

    add_test(NAME singleproc_guard_integration COMMAND singleproc_guard_integration)
    add_test(NAME singleproc_doorbell_integration COMMAND singleproc_doorbell_integration)
    add_test(NAME singleproc_process_integration
        COMMAND singleproc_process_integration $<TARGET_FILE:singleproc_probe>)
    set_tests_properties(singleproc_guard_integration singleproc_doorbell_integration
        PROPERTIES LABELS "integration" TIMEOUT 30)
    set_tests_properties(singleproc_process_integration PROPERTIES LABELS "integration" TIMEOUT 60)
endif()

# ——— 冒烟测试：核心完整业务流程（两个平台都跑）———
add_executable(singleproc_guard_lifecycle_smoke smoke/test_guard_lifecycle.cpp)
add_executable(singleproc_second_instance_smoke smoke/test_second_instance.cpp)
foreach(target singleproc_guard_lifecycle_smoke singleproc_second_instance_smoke)
    target_link_libraries(${target} PRIVATE singleproc::singleproc)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    singleproc_test_target(${target})
endforeach()
add_test(NAME singleproc_guard_lifecycle_smoke COMMAND singleproc_guard_lifecycle_smoke)
add_test(NAME singleproc_second_instance_smoke COMMAND singleproc_second_instance_smoke)
set_tests_properties(singleproc_guard_lifecycle_smoke singleproc_second_instance_smoke
    PROPERTIES LABELS "smoke" TIMEOUT 60)

# ——— 安装与下游消费：真实 install + find_package 流程 ———
# 多配置生成器（VS 等）需要把 $<CONFIG> 一路传进 install/test 脚本；
# 这个标记在 configure 阶段算好，避免生成器表达式里出现分号。
if(CMAKE_CONFIGURATION_TYPES)
    set(SINGLEPROC_MULTI_CONFIG 1)
else()
    set(SINGLEPROC_MULTI_CONFIG 0)
endif()

add_test(NAME singleproc_install_consumer
    COMMAND ${CMAKE_COMMAND}
            "-DSINGLEPROC_BINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DSINGLEPROC_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DSINGLEPROC_GENERATOR=${CMAKE_GENERATOR}"
            "-DSINGLEPROC_CONFIG=$<CONFIG>"
            "-DSINGLEPROC_MULTI_CONFIG=${SINGLEPROC_MULTI_CONFIG}"
            "-DSINGLEPROC_EXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            "-DSINGLEPROC_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DSINGLEPROC_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
            "-DSINGLEPROC_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/install_test.cmake)
set_tests_properties(singleproc_install_consumer PROPERTIES LABELS "integration" TIMEOUT 120)
