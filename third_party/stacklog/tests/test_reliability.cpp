#include <stacklog/stacklog.hpp>
#include "test_support.hpp"

#include <fstream>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

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

int main()
{
    stacklog::config cfg;
#ifndef _WIN32
    // 外部删除/占用日志路径的恢复场景依赖 POSIX 语义：Windows 不允许删除或
    // 替换仍被日志库打开的文件（共享模式不含 FILE_SHARE_DELETE），无法构造
    // 这两个场景；Windows 上用下面的“归档目标被目录占用”覆盖轮转故障恢复。
    {
    // 外部删除后，路径上的文件必须在检查点自动重建。
    const std::string delete_dir = test_temp_dir();
    STACKLOG_TEST_CHECK(!delete_dir.empty());
    cfg = make_config(delete_dir, "delete");
    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    STACKLOG_INFO() << "before-delete";
    const std::string delete_path = delete_dir + "/delete.log";
    STACKLOG_TEST_CHECK(std::remove(delete_path.c_str()) == 0);
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
    STACKLOG_TEST_CHECK(std::remove(recovery_path.c_str()) == 0);
#ifdef _WIN32
    STACKLOG_TEST_CHECK(_mkdir(recovery_path.c_str()) == 0);
#else
    STACKLOG_TEST_CHECK(::mkdir(recovery_path.c_str(), 0755) == 0);
#endif

    for (int index = 0; index < 1100; ++index)
    {
        STACKLOG_INFO() << "while-unwritable-" << index;
    }
    STACKLOG_TEST_CHECK(!test_is_regular_file(recovery_path));

    STACKLOG_TEST_CHECK(::rmdir(recovery_path.c_str()) == 0);
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
    cfg = make_config(archive_dir, "archive");
    cfg.max_file_size = 300;
    cfg.max_files = 1;
    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    const std::string archive_path = archive_dir + "/archive.log";
    const std::string first_archive = archive_path + ".1";
#ifdef _WIN32
    STACKLOG_TEST_CHECK(_mkdir(first_archive.c_str()) == 0);
#else
    STACKLOG_TEST_CHECK(::mkdir(first_archive.c_str(), 0755) == 0);
#endif
    {
        std::ofstream blocker((first_archive + "/blocker").c_str());
        blocker << "x";
    }

    for (int index = 0; index < 31; ++index)
    {
        STACKLOG_INFO() << "archive-line-" << index;
    }
    const std::vector<std::string> current_lines = test_read_lines(archive_path);
    STACKLOG_TEST_CHECK(!current_lines.empty());
    STACKLOG_TEST_CHECK(contains_line(current_lines, "archive-line-30"));

    STACKLOG_TEST_CHECK(std::remove((first_archive + "/blocker").c_str()) == 0);
    STACKLOG_TEST_CHECK(::rmdir(first_archive.c_str()) == 0);
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

    return 0;
}
