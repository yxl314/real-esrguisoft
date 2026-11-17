# qmake + Qt 工作线程示例的自动验收。
if(NOT DEFINED QMAKE_EXECUTABLE OR NOT DEFINED STACKLOG_SOURCE_DIR OR NOT DEFINED STACKLOG_BINARY_DIR)
    message(FATAL_ERROR "缺少 QMAKE_EXECUTABLE、STACKLOG_SOURCE_DIR 或 STACKLOG_BINARY_DIR")
endif()

set(build_dir "${STACKLOG_BINARY_DIR}/qmake-test")
file(REMOVE_RECURSE "${build_dir}")
file(MAKE_DIRECTORY "${build_dir}")

# qmake 所在目录同时提供示例运行所需的 Qt/编译器运行库。所有子进程都显式把该
# 目录放到 PATH 最前面：调用者 shell 里 mingw64/ucrt64 混用时，不能让另一套
# MSVCRT/UCRT 运行库抢在正确版本之前被加载（否则运行期报 0xC0000139）。
get_filename_component(qmake_dir "${QMAKE_EXECUTABLE}" DIRECTORY)
if(WIN32)
    set(path_separator ";")
    get_filename_component(msys_prefix "${qmake_dir}/../.." ABSOLUTE)
    set(tool_path "${qmake_dir};${msys_prefix}/usr/bin")
else()
    set(path_separator ":")
    set(tool_path "${qmake_dir}")
endif()
set(test_env "PATH=${tool_path}${path_separator}$ENV{PATH}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "${test_env}"
            "${QMAKE_EXECUTABLE}" "${STACKLOG_SOURCE_DIR}/examples/qt_worker_demo/qt_worker_demo.pro"
    WORKING_DIRECTORY "${build_dir}"
    RESULT_VARIABLE qmake_result
    OUTPUT_VARIABLE qmake_output
    ERROR_VARIABLE qmake_error)
if(NOT qmake_result EQUAL 0)
    message(FATAL_ERROR "qmake 失败:\n${qmake_output}\n${qmake_error}")
endif()

# make 必须与 qmake 属于同一套 MSYS2 环境：优先取 qmake 旁边的 mingw32-make，
# 其次同一前缀的 usr/bin/make，最后才回退 PATH 搜索。这样调用者 PATH 里
# 另一套环境的 make 不会用不兼容的编译器/运行库去编译 qmake 生成的 Makefile。
set(MAKE_PROGRAM "")
if(WIN32)
    foreach(candidate
        "${qmake_dir}/mingw32-make.exe"
        "${qmake_dir}/mingw32-make"
        "${qmake_dir}/nmake.exe"
        "${qmake_dir}/nmake"
        "${msys_prefix}/usr/bin/make.exe")
        if(EXISTS "${candidate}")
            set(MAKE_PROGRAM "${candidate}")
            break()
        endif()
    endforeach()
endif()
if(NOT MAKE_PROGRAM)
    # 不能直接 find_program(MAKE_PROGRAM ...)：变量已定义为空串时 find_program
    # 会认为“已设置”而不再搜索，所以用独立变量接结果。
    if(WIN32)
        find_program(STACKLOG_MAKE_PROGRAM NAMES mingw32-make nmake make gmake)
    else()
        find_program(STACKLOG_MAKE_PROGRAM NAMES make gmake)
    endif()
    if(STACKLOG_MAKE_PROGRAM)
        set(MAKE_PROGRAM "${STACKLOG_MAKE_PROGRAM}")
    endif()
endif()
if(NOT MAKE_PROGRAM)
    message(FATAL_ERROR "找不到 make 工具")
endif()

if(MAKE_PROGRAM MATCHES "nmake")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "${test_env}"
        "${MAKE_PROGRAM}" WORKING_DIRECTORY "${build_dir}"
        RESULT_VARIABLE make_result OUTPUT_VARIABLE make_output ERROR_VARIABLE make_error)
else()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "${test_env}"
        "${MAKE_PROGRAM}" -j4 WORKING_DIRECTORY "${build_dir}"
        RESULT_VARIABLE make_result OUTPUT_VARIABLE make_output ERROR_VARIABLE make_error)
endif()
if(NOT make_result EQUAL 0)
    message(FATAL_ERROR "qmake 构建失败:\n${make_output}\n${make_error}")
endif()

# 单配置在构建根目录，Windows 的 qmake 默认把产物放在 release/ 子目录。
set(demo_exe "")
file(GLOB demo_exe_candidates
    "${build_dir}/qt_worker_demo"
    "${build_dir}/qt_worker_demo.exe"
    "${build_dir}/release/qt_worker_demo.exe"
    "${build_dir}/debug/qt_worker_demo.exe")
if(demo_exe_candidates)
    list(GET demo_exe_candidates 0 demo_exe)
endif()
if(demo_exe STREQUAL "")
    message(FATAL_ERROR "找不到 qmake 示例产物: ${build_dir}")
endif()

# 示例日志位置来自 QStandardPaths::AppLocalDataLocation：
# POSIX 可用 XDG_DATA_HOME 重定向，Windows 上只能用系统 AppData/Local。
if(WIN32)
    if(NOT DEFINED ENV{LOCALAPPDATA} OR "$ENV{LOCALAPPDATA}" STREQUAL "")
        message(FATAL_ERROR "Windows 上缺少 LOCALAPPDATA，无法定位 Qt 示例日志目录")
    endif()
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" app_local_data)
    set(log_root "${app_local_data}/stacklog/stacklog-qt-worker-demo")
else()
    set(log_root "${build_dir}/xdg-data/stacklog/stacklog-qt-worker-demo")
endif()
# 上一次运行留下的日志会污染 trace 断言，先清理。
file(REMOVE_RECURSE "${log_root}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "${test_env}" "XDG_DATA_HOME=${build_dir}/xdg-data" "${demo_exe}"
    WORKING_DIRECTORY "${build_dir}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "qmake 示例运行失败:\n${run_output}\n${run_error}")
endif()

file(GLOB demo_logs "${log_root}/qt_worker_demo.log*")
if(NOT demo_logs)
    message(FATAL_ERROR "qmake 示例没有生成日志文件: ${log_root}")
endif()
if(NOT EXISTS "${log_root}/qt_worker_demo.log.1" OR
   NOT EXISTS "${log_root}/qt_worker_demo.log.2")
    message(FATAL_ERROR "qmake 示例没有按预期触发文件轮转")
endif()

# 读取所有归档文件，校验主线程、工作线程和子作用域的 trace 关系。
set(log_text "")
foreach(log_file ${demo_logs})
    file(READ "${log_file}" log_content)
    string(APPEND log_text "${log_content}\n")
endforeach()
# Windows 的示例日志写在真实用户目录，读完立即清理，避免污染环境。
if(WIN32)
    file(REMOVE_RECURSE "${log_root}")
    # Qt 会按 组织名/应用名 建两层目录；应用目录删掉后，组织目录为空时一并清理。
    get_filename_component(org_dir "${log_root}" DIRECTORY)
    file(GLOB org_entries "${org_dir}/*")
    if(NOT org_entries)
        file(REMOVE_RECURSE "${org_dir}")
    endif()
endif()
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
