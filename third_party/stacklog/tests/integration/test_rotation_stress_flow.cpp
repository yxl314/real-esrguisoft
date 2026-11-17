#include <stacklog/stacklog.hpp>
#include "../support/test_harness.hpp"
#include "../support/test_support.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <thread>
#include <vector>

class TestRotationStressFlow // NOLINT(readability-identifier-naming): 测试策略要求类名 Test<被测单元或流程>
{
public:
    static void rotation_stress_flow();
};

void TestRotationStressFlow::rotation_stress_flow()
{
    const std::string dir = test_temp_dir();
    STACKLOG_TEST_CHECK(!dir.empty());

    stacklog::config cfg;
    cfg.dir = dir;
    cfg.base_name = "stress";
    cfg.max_file_size = 4096;
    cfg.max_files = 200;
    cfg.min_level = stacklog::level::info;
    STACKLOG_TEST_CHECK(stacklog::init(cfg));

    const int thread_count = 4;
    const int per_thread = 500;
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(thread_count));
    for (int thread_index = 0; thread_index < thread_count; ++thread_index)
    {
        workers.emplace_back(
            [thread_index, per_thread]
            {
                for (int index = 0; index < per_thread; ++index)
                {
                    STACKLOG_INFO() << "unique=" << thread_index << ":" << index;
                }
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    stacklog::shutdown();

    std::set<std::string> unique_lines;
    int total = 0;
    for (unsigned archive = 0; archive <= cfg.max_files; ++archive)
    {
        std::string path = dir + "/stress.log";
        if (archive > 0)
        {
            path += "." + std::to_string(archive);
        }
        const std::vector<std::string> lines = test_read_lines(path);
        for (const auto& line : lines)
        {
            ++total;
            const std::vector<std::string> fields = test_log_fields(line);
            STACKLOG_TEST_CHECK(fields.size() == 8);
            const std::string::size_type pos = line.find("unique=");
            STACKLOG_TEST_CHECK(pos != std::string::npos);
            unique_lines.insert(line.substr(pos));
        }
    }

    STACKLOG_TEST_CHECK(total == thread_count * per_thread);
    STACKLOG_TEST_CHECK(unique_lines.size() == static_cast<std::size_t>(thread_count * per_thread));
    test_cleanup(dir);
}

STACKLOG_TEST_MAIN(TestRotationStressFlow, STACKLOG_TEST_CASE(TestRotationStressFlow, rotation_stress_flow));
