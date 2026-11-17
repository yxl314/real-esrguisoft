/// @file
/// @brief 冒烟：init → 各级别日志 → 嵌套 trace → 跨线程接续 → 轮转 → shutdown → 校验。

#include "../support/test_harness.hpp"
#include "../support/test_support.hpp"

#include <stacklog/stacklog.hpp>

#include <string>
#include <thread>
#include <vector>

namespace
{

/// @brief 读取目录下当前日志与全部归档，返回所有非空行。
std::vector<std::string> read_all_lines(const std::string& base, unsigned max_files)
{
    std::vector<std::string> lines;
    std::vector<std::string> paths = {base};
    for (unsigned index = 1; index <= max_files; ++index)
    {
        paths.push_back(base + "." + std::to_string(index));
    }
    for (const std::string& path : paths)
    {
        if (!test_is_regular_file(path))
        {
            continue;
        }
        const std::vector<std::string> current = test_read_lines(path);
        lines.insert(lines.end(), current.begin(), current.end());
    }
    return lines;
}

} // namespace

class TestLoggingFlow // NOLINT(readability-identifier-naming): 测试策略要求类名 Test<被测单元或流程>
{
public:
    static void logging_flow();
};

void TestLoggingFlow::logging_flow()
{
    const std::string root = test_temp_dir();
    STACKLOG_TEST_CHECK(!root.empty());

    const std::string dir = root + "/logs";
    const std::string base = dir + "/smoke";

    stacklog::config cfg;
    cfg.dir = dir;
    cfg.base_name = "smoke";
    cfg.max_file_size = 512;
    cfg.max_files = 3;
    cfg.min_level = stacklog::level::debug;

    STACKLOG_TEST_CHECK(stacklog::init(cfg));

    STACKLOG_DEBUG() << "level-debug";
    STACKLOG_INFO() << "level-info";
    STACKLOG_WARN() << "level-warn";
    STACKLOG_ERROR() << "level-error";

    stacklog::trace_frame main_frame;
    {
        STACKLOG_TRACE("smoke-main");
        main_frame = stacklog::capture_trace();
        STACKLOG_TEST_CHECK(main_frame.valid());
        STACKLOG_INFO() << "main-thread-log";
    }

    std::thread worker(
        [main_frame]
        {
            STACKLOG_RESTORE_TRACE(main_frame, "smoke-worker");
            STACKLOG_INFO() << "cross-thread-log";
        });
    worker.join();

    // 轮转前先校验级别与 trace：小文件上限会在压力阶段把早期行删掉。
    const std::vector<std::string> early_lines = read_all_lines(base + ".log", cfg.max_files);
    STACKLOG_TEST_CHECK(!early_lines.empty());

    bool has_debug = false;
    bool has_info = false;
    bool has_warn = false;
    bool has_error = false;
    std::vector<std::string> main_trace_line;
    std::vector<std::string> worker_trace_line;
    for (const std::string& line : early_lines)
    {
        const std::vector<std::string> fields = test_log_fields(line);
        STACKLOG_TEST_CHECK(fields.size() == 8);
        has_debug = has_debug || !test_find_line({line}, "level-debug").empty();
        has_info = has_info || !test_find_line({line}, "level-info").empty();
        has_warn = has_warn || !test_find_line({line}, "level-warn").empty();
        has_error = has_error || !test_find_line({line}, "level-error").empty();
        if (main_trace_line.empty())
        {
            main_trace_line = test_find_line({line}, "main-thread-log");
        }
        if (worker_trace_line.empty())
        {
            worker_trace_line = test_find_line({line}, "cross-thread-log");
        }
    }
    STACKLOG_TEST_CHECK(has_debug);
    STACKLOG_TEST_CHECK(has_info);
    STACKLOG_TEST_CHECK(has_warn);
    STACKLOG_TEST_CHECK(has_error);
    STACKLOG_TEST_CHECK(main_trace_line.size() == 8);
    STACKLOG_TEST_CHECK(worker_trace_line.size() == 8);

    // 校验跨线程接续：同一个 trace_id，worker 的 parent 是主线程节点。
    STACKLOG_TEST_CHECK(main_trace_line[2] == worker_trace_line[2]);
    STACKLOG_TEST_CHECK(worker_trace_line[4] == main_trace_line[3]);
    STACKLOG_TEST_CHECK(worker_trace_line[5] == "smoke-worker");

    // 小文件上限触发多次轮转，覆盖归档写入与最旧归档清理。
    for (int index = 0; index < 80; ++index)
    {
        STACKLOG_INFO() << "bulk-" << index;
    }

    stacklog::shutdown();

    STACKLOG_TEST_CHECK(test_is_regular_file(base + ".log"));
    STACKLOG_TEST_CHECK(test_is_regular_file(base + ".log.1"));
    STACKLOG_TEST_CHECK(!test_is_regular_file(base + ".log.5"));

    const std::vector<std::string> final_lines = read_all_lines(base + ".log", cfg.max_files);
    bool has_last_bulk = false;
    for (const std::string& line : final_lines)
    {
        has_last_bulk = has_last_bulk || !test_find_line({line}, "bulk-79").empty();
    }
    STACKLOG_TEST_CHECK(has_last_bulk);

    test_cleanup(root);
}

STACKLOG_TEST_MAIN(TestLoggingFlow, STACKLOG_TEST_CASE(TestLoggingFlow, logging_flow));
