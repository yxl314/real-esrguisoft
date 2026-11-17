#include <stacklog/stacklog.hpp>
#include "../support/test_harness.hpp"
#include "../support/test_support.hpp"

namespace
{

stacklog::config make_config(const std::string& dir, const std::string& base_name)
{
    stacklog::config cfg;
    cfg.dir = dir;
    cfg.base_name = base_name;
    cfg.max_file_size = 1024ULL * 1024;
    cfg.max_files = 2;
    cfg.min_level = stacklog::level::info;
    return cfg;
}

bool contains_line(const std::vector<std::string>& lines, const std::string& needle)
{
    return !test_find_line(lines, needle).empty();
}

} // namespace

class TestReliabilityFlow // NOLINT(readability-identifier-naming): 测试策略要求类名 Test<被测单元或流程>
{
public:
    static void reliability_flow();
};

void TestReliabilityFlow::reliability_flow()
{
#ifndef _WIN32
    // Windows 上库以默认共享模式打开日志文件，外部进程无法删除或替换仍在
    // 打开的文件，因此"外部删除后自动重建"这类场景只在 POSIX 下成立。
    {
        // 外部删除后，路径上的文件必须在检查点自动重建。
        const std::string delete_dir = test_temp_dir();
        STACKLOG_TEST_CHECK(!delete_dir.empty());
        stacklog::config cfg = make_config(delete_dir, "delete");
        STACKLOG_TEST_CHECK(stacklog::init(cfg));
        STACKLOG_INFO() << "before-delete";
        const std::string delete_path = delete_dir + "/delete.log";
        STACKLOG_TEST_CHECK(test_remove_file(delete_path));
        for (int index = 0; index < 1100; ++index)
        {
            STACKLOG_INFO() << "after-delete-" << index;
        }
        stacklog::shutdown();
        STACKLOG_TEST_CHECK(test_is_regular_file(delete_path));
        const std::vector<std::string> delete_lines = test_read_lines(delete_path);
        STACKLOG_TEST_CHECK(contains_line(delete_lines, "after-delete-1099"));
        test_cleanup(delete_dir);

        // 外部删除 + 目标路径被目录占用：先降级，清理障碍后必须能重新打开文件。
        const std::string recovery_dir = test_temp_dir();
        STACKLOG_TEST_CHECK(!recovery_dir.empty());
        cfg = make_config(recovery_dir, "recovery");
        STACKLOG_TEST_CHECK(stacklog::init(cfg));
        STACKLOG_INFO() << "before-delete";
        const std::string recovery_path = recovery_dir + "/recovery.log";
        STACKLOG_TEST_CHECK(test_remove_file(recovery_path));
        STACKLOG_TEST_CHECK(test_make_dir(recovery_path));

        for (int index = 0; index < 1100; ++index)
        {
            STACKLOG_INFO() << "while-unwritable-" << index;
        }
        STACKLOG_TEST_CHECK(!test_is_regular_file(recovery_path));

        STACKLOG_TEST_CHECK(test_remove_dir(recovery_path));
        for (int index = 0; index < 40; ++index)
        {
            STACKLOG_INFO() << "recovered-" << index;
        }
        stacklog::shutdown();

        STACKLOG_TEST_CHECK(test_is_regular_file(recovery_path));
        const std::vector<std::string> recovered_lines = test_read_lines(recovery_path);
        STACKLOG_TEST_CHECK(contains_line(recovered_lines, "recovered-39"));
        test_cleanup(recovery_dir);
    }
#endif

    // 归档目标被目录占用：轮转失败时继续写当前文件，清理障碍后能恢复轮转。
    const std::string archive_dir = test_temp_dir();
    STACKLOG_TEST_CHECK(!archive_dir.empty());
    stacklog::config cfg = make_config(archive_dir, "archive");
    cfg.max_file_size = 300;
    cfg.max_files = 1;
    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    const std::string archive_path = archive_dir + "/archive.log";
    const std::string first_archive = archive_path + ".1";
    STACKLOG_TEST_CHECK(test_make_dir(first_archive));
    STACKLOG_TEST_CHECK(test_write_file(first_archive + "/blocker", "x"));

    for (int index = 0; index < 31; ++index)
    {
        STACKLOG_INFO() << "archive-line-" << index;
    }
    const std::vector<std::string> current_lines = test_read_lines(archive_path);
    STACKLOG_TEST_CHECK(!current_lines.empty());
    STACKLOG_TEST_CHECK(contains_line(current_lines, "archive-line-30"));

    STACKLOG_TEST_CHECK(test_remove_file(first_archive + "/blocker"));
    STACKLOG_TEST_CHECK(test_remove_dir(first_archive));
    for (int index = 31; index < 101; ++index)
    {
        STACKLOG_INFO() << "archive-line-" << index;
    }
    stacklog::shutdown();

    STACKLOG_TEST_CHECK(test_is_regular_file(first_archive));
    const std::vector<std::string> base_lines = test_read_lines(archive_path);
    const std::vector<std::string> older_lines = test_read_lines(first_archive);
    STACKLOG_TEST_CHECK(contains_line(base_lines, "archive-line-100") ||
                        contains_line(older_lines, "archive-line-100"));
    test_cleanup(archive_dir);
}

STACKLOG_TEST_MAIN(TestReliabilityFlow, STACKLOG_TEST_CASE(TestReliabilityFlow, reliability_flow));
