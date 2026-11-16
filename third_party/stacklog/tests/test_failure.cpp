#include <stacklog/stacklog.hpp>
#include "test_support.hpp"

#include <fstream>

#ifndef _WIN32
#include <unistd.h>
#endif

int main()
{
    const std::string dir = test_temp_dir();
    STACKLOG_TEST_CHECK(!dir.empty());

    stacklog::config cfg;
    cfg.dir = dir;
    cfg.base_name = "failure";
    cfg.max_file_size = 1024;
    cfg.max_files = 2;

    // 非法字段必须拒绝初始化。
    stacklog::config invalid = cfg;
    invalid.dir = "";
    STACKLOG_TEST_CHECK(!stacklog::init(invalid));

    invalid = cfg;
    invalid.base_name = "";
    STACKLOG_TEST_CHECK(!stacklog::init(invalid));

    invalid = cfg;
    invalid.max_file_size = 0;
    STACKLOG_TEST_CHECK(!stacklog::init(invalid));

    invalid = cfg;
    invalid.max_files = 0;
    STACKLOG_TEST_CHECK(!stacklog::init(invalid));

    // 初始化失败后的日志调用不能崩溃。
    invalid = cfg;
    invalid.dir = "";
    invalid.min_level = stacklog::level::debug;
    STACKLOG_TEST_CHECK(!stacklog::init(invalid));
    stacklog::set_level(stacklog::level::info);

    // 目录位置被普通文件占用时，初始化失败。
    const std::string occupied = dir + "/not-a-directory";
    {
        std::ofstream file(occupied.c_str());
        file << "x";
    }
    cfg.dir = occupied;
    STACKLOG_TEST_CHECK(!stacklog::init(cfg));

    // 日志文件位置被目录占用时，初始化失败。
    cfg.dir = dir;
    cfg.base_name = "occupied";
    const std::string log_path = dir + "/occupied.log";
#ifdef _WIN32
    STACKLOG_TEST_CHECK(_mkdir(log_path.c_str()) == 0);
#else
    STACKLOG_TEST_CHECK(::mkdir(log_path.c_str(), 0755) == 0);
#endif
    STACKLOG_TEST_CHECK(!stacklog::init(cfg));

    // 未初始化时写日志只能降级 stderr，但不能崩溃。
    stacklog::shutdown();
    stacklog::shutdown();
    STACKLOG_INFO() << "fallback-after-shutdown";

    // 一次失败不能影响后续正常初始化。
    cfg.dir = dir;
    cfg.base_name = "recovered";
    cfg.max_files = 2;
    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    STACKLOG_INFO() << "recovered-after-failure";
    stacklog::shutdown();
    const std::vector<std::string> recovered = test_read_lines(dir + "/recovered.log");
    STACKLOG_TEST_CHECK(recovered.size() == 1);
    STACKLOG_TEST_CHECK(recovered[0].find("recovered-after-failure") != std::string::npos);

#ifndef _WIN32
    // 写入遇到 ENOSPC 后 ready=false，但流仍然打开；路径恢复后重试必须能
    // 先 close 再重开。旧实现直接对已打开流 open，重试永远不会成功。
    stacklog::shutdown();
    cfg.dir = dir;
    cfg.base_name = "retry";
    cfg.max_file_size = 1024ULL * 1024ULL;
    cfg.max_files = 2;
    const std::string retry_path = dir + "/retry.log";
    (void)::unlink(retry_path.c_str());
    if (::access("/dev/full", W_OK) == 0 && ::symlink("/dev/full", retry_path.c_str()) == 0)
    {
        // 先让日志文件指向 /dev/full，再 init；这样写入时才能稳定触发 ENOSPC。
        STACKLOG_TEST_CHECK(stacklog::init(cfg));
        for (int i = 0; i < 40; ++i)
        {
            STACKLOG_INFO() << "forced-write-failure";
        }
        (void)::unlink(retry_path.c_str());
        for (int i = 0; i < 40; ++i)
        {
            STACKLOG_INFO() << "recovered-write";
        }
        stacklog::shutdown();
        const std::vector<std::string> retried = test_read_lines(retry_path);
        STACKLOG_TEST_CHECK(!retried.empty());
        STACKLOG_TEST_CHECK(retried.back().find("recovered-write") != std::string::npos);
    }
#endif

    test_cleanup(dir);
    return 0;
}
