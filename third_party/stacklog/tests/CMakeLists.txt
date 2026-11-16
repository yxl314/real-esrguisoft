add_executable(stacklog_test_format test_format.cpp)
add_executable(stacklog_test_rotation test_rotation.cpp)
add_executable(stacklog_test_trace test_trace.cpp)
add_executable(stacklog_test_thread test_thread.cpp)
add_executable(stacklog_test_failure test_failure.cpp)
add_executable(stacklog_test_cross_thread test_cross_thread.cpp)
add_executable(stacklog_test_reliability test_reliability.cpp)
add_executable(stacklog_test_rotation_stress test_rotation_stress.cpp)

foreach(target
    stacklog_test_format
    stacklog_test_rotation
    stacklog_test_trace
    stacklog_test_thread
    stacklog_test_failure
    stacklog_test_cross_thread
    stacklog_test_reliability
    stacklog_test_rotation_stress)
    target_link_libraries(${target} PRIVATE stacklog::stacklog)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

add_test(NAME stacklog_format COMMAND stacklog_test_format)
add_test(NAME stacklog_rotation COMMAND stacklog_test_rotation)
add_test(NAME stacklog_trace COMMAND stacklog_test_trace)
add_test(NAME stacklog_thread COMMAND stacklog_test_thread)
add_test(NAME stacklog_failure COMMAND stacklog_test_failure)
add_test(NAME stacklog_cross_thread COMMAND stacklog_test_cross_thread)
add_test(NAME stacklog_reliability COMMAND stacklog_test_reliability)
add_test(NAME stacklog_rotation_stress COMMAND stacklog_test_rotation_stress)

set_tests_properties(stacklog_reliability stacklog_rotation_stress PROPERTIES TIMEOUT 60)

add_test(NAME stacklog_install
    COMMAND "${CMAKE_COMMAND}"
        "-DSTACKLOG_BINARY_DIR=${PROJECT_BINARY_DIR}"
        "-DSTACKLOG_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DSTACKLOG_CMAKE_GENERATOR=${CMAKE_GENERATOR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/install_test.cmake")
set_tests_properties(stacklog_install PROPERTIES LABELS "integration" TIMEOUT 120)

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
