add_executable(singleproc_tests test_guard.cpp)
add_executable(singleproc_doorbell_tests test_doorbell.cpp)
add_executable(singleproc_probe probe.cpp)

set(SINGLEPROC_TEST_TARGETS singleproc_tests singleproc_doorbell_tests singleproc_probe)

# POSIX 的进程测试依赖 fork/SIGKILL/pipe；Windows 换成基于 CreateProcess 的用例。
if(WIN32)
    add_executable(singleproc_win32_tests test_win32.cpp)
    add_dependencies(singleproc_win32_tests singleproc_probe)
    list(APPEND SINGLEPROC_TEST_TARGETS singleproc_win32_tests)
else()
    add_executable(singleproc_process_tests test_process.cpp)
    add_dependencies(singleproc_process_tests singleproc_probe)
    list(APPEND SINGLEPROC_TEST_TARGETS singleproc_process_tests)
endif()

foreach(target ${SINGLEPROC_TEST_TARGETS})
    target_link_libraries(${target} PRIVATE singleproc::singleproc)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

add_test(NAME singleproc_tests COMMAND singleproc_tests)
add_test(NAME singleproc_doorbell_tests COMMAND singleproc_doorbell_tests)
if(WIN32)
    add_test(NAME singleproc_win32_tests COMMAND singleproc_win32_tests $<TARGET_FILE:singleproc_probe>)
else()
    add_test(NAME singleproc_process_tests COMMAND singleproc_process_tests $<TARGET_FILE:singleproc_probe>)
endif()

add_test(NAME singleproc_install_consumer
    COMMAND ${CMAKE_COMMAND}
            -DSINGLEPROC_BINARY_DIR=${PROJECT_BINARY_DIR}
            -DSINGLEPROC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            "-DSINGLEPROC_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DSINGLEPROC_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
            "-DSINGLEPROC_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
            "-DSINGLEPROC_GENERATOR=${CMAKE_GENERATOR}"
            "-DSINGLEPROC_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DSINGLEPROC_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/install_test.cmake)

set_tests_properties(singleproc_tests singleproc_doorbell_tests PROPERTIES TIMEOUT 30)
if(WIN32)
    set_tests_properties(singleproc_win32_tests PROPERTIES TIMEOUT 60)
else()
    set_tests_properties(singleproc_process_tests PROPERTIES TIMEOUT 60)
endif()
set_tests_properties(singleproc_install_consumer PROPERTIES TIMEOUT 180)
