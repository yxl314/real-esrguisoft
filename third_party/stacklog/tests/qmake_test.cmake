# qmake + Qt 工作线程示例的自动验收。
if(NOT DEFINED QMAKE_EXECUTABLE OR NOT DEFINED STACKLOG_SOURCE_DIR OR NOT DEFINED STACKLOG_BINARY_DIR)
    message(FATAL_ERROR "缺少 QMAKE_EXECUTABLE、STACKLOG_SOURCE_DIR 或 STACKLOG_BINARY_DIR")
endif()

set(build_dir "${STACKLOG_BINARY_DIR}/qmake-test")
file(REMOVE_RECURSE "${build_dir}")
file(MAKE_DIRECTORY "${build_dir}")

execute_process(
    COMMAND "${QMAKE_EXECUTABLE}" "${STACKLOG_SOURCE_DIR}/examples/qt_worker_demo/qt_worker_demo.pro"
    WORKING_DIRECTORY "${build_dir}"
    RESULT_VARIABLE qmake_result
    OUTPUT_VARIABLE qmake_output
    ERROR_VARIABLE qmake_error)
if(NOT qmake_result EQUAL 0)
    message(FATAL_ERROR "qmake 失败:\n${qmake_output}\n${qmake_error}")
endif()

find_program(MAKE_PROGRAM NAMES make gmake mingw32-make nmake)
if(NOT MAKE_PROGRAM)
    message(FATAL_ERROR "找不到 make 工具")
endif()

if(MAKE_PROGRAM MATCHES "nmake")
    execute_process(COMMAND "${MAKE_PROGRAM}" WORKING_DIRECTORY "${build_dir}"
        RESULT_VARIABLE make_result OUTPUT_VARIABLE make_output ERROR_VARIABLE make_error)
else()
    execute_process(COMMAND "${MAKE_PROGRAM}" -j4 WORKING_DIRECTORY "${build_dir}"
        RESULT_VARIABLE make_result OUTPUT_VARIABLE make_output ERROR_VARIABLE make_error)
endif()
if(NOT make_result EQUAL 0)
    message(FATAL_ERROR "qmake 构建失败:\n${make_output}\n${make_error}")
endif()

set(demo_exe "${build_dir}/qt_worker_demo")
if(WIN32)
    set(demo_exe "${build_dir}/release/qt_worker_demo.exe")
endif()
if(NOT EXISTS "${demo_exe}")
    message(FATAL_ERROR "找不到 qmake 示例产物: ${demo_exe}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_DATA_HOME=${build_dir}/xdg-data" "${demo_exe}"
    WORKING_DIRECTORY "${build_dir}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "qmake 示例运行失败:\n${run_output}\n${run_error}")
endif()

file(GLOB demo_logs "${build_dir}/xdg-data/stacklog/stacklog-qt-worker-demo/qt_worker_demo.log*")
if(NOT demo_logs)
    message(FATAL_ERROR "qmake 示例没有生成日志文件")
endif()
if(NOT EXISTS "${build_dir}/xdg-data/stacklog/stacklog-qt-worker-demo/qt_worker_demo.log.1" OR
   NOT EXISTS "${build_dir}/xdg-data/stacklog/stacklog-qt-worker-demo/qt_worker_demo.log.2")
    message(FATAL_ERROR "qmake 示例没有按预期触发文件轮转")
endif()

# 读取所有归档文件，校验主线程、工作线程和子作用域的 trace 关系。
set(log_text "")
foreach(log_file ${demo_logs})
    file(READ "${log_file}" log_content)
    string(APPEND log_text "${log_content}\n")
endforeach()
string(REPLACE "\r" "" log_text "${log_text}")
string(REPLACE "\n" ";" log_lines "${log_text}")

set(main_trace "")
set(main_node "")
set(worker_trace "")
set(worker_node "")
set(worker_parent "")
set(worker_sub_trace "")
set(worker_sub_node "")
set(worker_sub_parent "")

foreach(line ${log_lines})
    if(line STREQUAL "")
        continue()
    endif()
    string(REPLACE "|" ";" fields "${line}")
    list(LENGTH fields field_count)
    if(NOT field_count EQUAL 8)
        message(FATAL_ERROR "日志字段数量不是 8: ${line}")
    endif()
    list(GET fields 2 trace_id)
    list(GET fields 3 node_id)
    list(GET fields 4 parent_id)
    list(GET fields 7 message)
    if(message STREQUAL "main thread started")
        set(main_trace "${trace_id}")
        set(main_node "${node_id}")
    elseif(message STREQUAL "worker thread started")
        set(worker_trace "${trace_id}")
        set(worker_node "${node_id}")
        set(worker_parent "${parent_id}")
    elseif(message STREQUAL "worker sub scope")
        set(worker_sub_trace "${trace_id}")
        set(worker_sub_node "${node_id}")
        set(worker_sub_parent "${parent_id}")
    endif()
endforeach()

if(main_trace STREQUAL "" OR main_node STREQUAL "" OR
   worker_trace STREQUAL "" OR worker_node STREQUAL "" OR
   worker_sub_trace STREQUAL "" OR worker_sub_node STREQUAL "")
    message(FATAL_ERROR "qmake 示例日志缺少 trace 验收所需的行")
endif()
if(NOT worker_trace STREQUAL main_trace OR NOT worker_sub_trace STREQUAL main_trace)
    message(FATAL_ERROR "Qt 工作线程没有接续主线程的 trace")
endif()
if(NOT worker_parent STREQUAL main_node)
    message(FATAL_ERROR "worker-job 的 parent 不是主线程节点")
endif()
if(NOT worker_sub_parent STREQUAL worker_node)
    message(FATAL_ERROR "worker-sub 的 parent 不是 worker-job 节点")
endif()
if(main_node STREQUAL worker_node OR worker_node STREQUAL worker_sub_node OR
   main_node STREQUAL worker_sub_node)
    message(FATAL_ERROR "Qt 跨线程节点 ID 不唯一")
endif()

message(STATUS "qmake worker demo test passed")
