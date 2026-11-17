#include <stacklog/stacklog.hpp>
#include "../support/test_harness.hpp"
#include "../support/test_support.hpp"

#include <thread>

namespace
{

void write_lines(int thread_index)
{
    for (int index = 0; index < 100; ++index)
    {
        STACKLOG_INFO() << "thread=" << thread_index << " line=" << index;
    }
}

} // namespace

class TestThreadingFlow // NOLINT(readability-identifier-naming): 测试策略要求类名 Test<被测单元或流程>
{
public:
    static void threading_flow();
};

void TestThreadingFlow::threading_flow()
{
    const std::string dir = test_temp_dir();
    STACKLOG_TEST_CHECK(!dir.empty());

    stacklog::config cfg;
    cfg.dir = dir;
    cfg.base_name = "thread";
    cfg.max_file_size = 1024ULL * 1024;
    cfg.max_files = 3;
    cfg.min_level = stacklog::level::info;

    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int index = 0; index < 4; ++index)
    {
        threads.emplace_back(write_lines, index);
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    stacklog::shutdown();

    const std::vector<std::string> lines = test_read_lines(dir + "/thread.log");
    STACKLOG_TEST_CHECK(lines.size() == 400);
    for (const auto& line : lines)
    {
        const std::vector<std::string> parts = test_log_fields(line);
        STACKLOG_TEST_CHECK(parts.size() == 8);
        STACKLOG_TEST_CHECK(parts[1] == "INFO");
    }

    test_cleanup(dir);
}

STACKLOG_TEST_MAIN(TestThreadingFlow, STACKLOG_TEST_CASE(TestThreadingFlow, threading_flow));
