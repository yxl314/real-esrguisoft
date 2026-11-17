// 本文件只在 Windows 构建中编译。整文件加平台守卫，Linux 门禁里的
// clang-tidy / 格式化检查会把它当成空翻译单元，不会因为找不到 windows.h 误报。
#ifdef _WIN32

/// @file
/// @brief Windows 平台自动测试：LockFileEx 锁语义、命名管道门铃与 errno 映射。
///
/// 进程模型与 POSIX 测试不同：没有 fork，用 CreateProcess 重新执行当前测试
/// 程序，子进程按 argv[1] 的模式运行；这是本文件既当测试又当子进程入口的原因。

#include <singleproc/singleproc.hpp>

#include "support/test_support_win32.hpp"

#include <windows.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using singleproc::Guard;

constexpr DWORD io_timeout_ms = 15000;

// 覆盖盲区说明（刻意不覆盖）：
// - EACCES：需要修改目录 DACL（icacls 或 SetNamedSecurityInfo）才能稳定构造，
//   属于环境依赖过高的场景，未纳入自动测试；库内映射路径由人工探针验证过。
// - 门铃目标超长（管道名 > 255）：需要路径极短的 runtime_dir 才能让锁路径
//   合法而管道名超长，跨机器不稳定，未纳入自动测试。

// ——— 子进程模式 ———

int hold_child(const std::string& directory,
               const std::string& key,
               const std::string& ready_marker,
               const std::string& release_marker)
{
    const Guard guard(key, directory);
    if (guard.state() != Guard::State::Primary)
    {
        (void)std::fprintf(
            stderr, "hold child: state %d error %d\n", static_cast<int>(guard.state()), guard.error_code());
        return 2;
    }
    if (!test_support::write_marker(ready_marker))
    {
        return 3;
    }
    if (!test_support::wait_for_file(release_marker, 30000))
    {
        return 4;
    }
    return 0;
}

int check_child(const std::string& directory, const std::string& key)
{
    const Guard guard(key, directory);
    if (guard.state() == Guard::State::AlreadyRunning && guard.error_code() == 0)
    {
        return 0;
    }
    (void)std::fprintf(stderr, "check child: state %d error %d\n", static_cast<int>(guard.state()), guard.error_code());
    return 2;
}

int notify_child(const std::string& directory, const std::string& key)
{
    const Guard guard(key, directory);
    if (guard.state() == Guard::State::AlreadyRunning && guard.error_code() == 0)
    {
        return 0;
    }
    (void)std::fprintf(
        stderr, "notify child: state %d error %d\n", static_cast<int>(guard.state()), guard.error_code());
    return 2;
}

int run_child_mode(int argc, char** argv)
{
    if (argc == 6 && std::strcmp(argv[1], "hold") == 0)
    {
        return hold_child(argv[2], argv[3], argv[4], argv[5]);
    }
    if (argc == 4 && std::strcmp(argv[1], "check") == 0)
    {
        return check_child(argv[2], argv[3]);
    }
    if (argc == 4 && std::strcmp(argv[1], "notify") == 0)
    {
        return notify_child(argv[2], argv[3]);
    }
    (void)std::fprintf(stderr, "unknown child mode\n");
    return 64;
}

// ——— 取消风暴辅助 ———

std::string& cancel_pipe_name()
{
    static std::string value;
    return value;
}

volatile LONG& cancel_stop_flag()
{
    static volatile LONG value = 0;
    return value;
}

DWORD WINAPI cancel_helper(LPVOID argument)
{
    (void)argument;
    const std::wstring wide = test_support::utf8_to_wide(cancel_pipe_name());
    while (::InterlockedCompareExchange(&cancel_stop_flag(), 0, 0) == 0)
    {
        const HANDLE client = ::CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (client != INVALID_HANDLE_VALUE)
        {
            (void)::CloseHandle(client);
        }
    }
    return 0;
}

// ——— 测试 ———

void test_invalid_keys(const std::string& directory)
{
    const Guard empty("", directory);
    CHECK(empty.state() == Guard::State::Error);
    CHECK(empty.error_code() == EINVAL);

    const Guard slash("../not-allowed", directory);
    CHECK(slash.state() == Guard::State::Error);
    CHECK(slash.error_code() == EINVAL);

    const Guard space("not allowed", directory);
    CHECK(space.state() == Guard::State::Error);
    CHECK(space.error_code() == EINVAL);

    std::string nul_key("nul");
    nul_key.push_back('\0');
    const Guard nul(nul_key, directory);
    CHECK(nul.state() == Guard::State::Error);
    CHECK(nul.error_code() == EINVAL);
}

void test_invalid_directories(const std::string& directory)
{
    const std::string missing = test_support::join_path(directory, "missing-directory");
    const Guard missing_guard("missing", missing);
    CHECK(missing_guard.state() == Guard::State::Error);
    CHECK(missing_guard.error_code() == ENOENT);

    const std::string file_path = test_support::join_path(directory, "as-file");
    CHECK(test_support::write_marker(file_path));
    const Guard file_guard("file", file_path);
    CHECK(file_guard.state() == Guard::State::Error);
    CHECK(file_guard.error_code() == ENOTDIR);
    test_support::remove_file(file_path);

    std::string nul_directory = directory;
    nul_directory.push_back('\0');
    const Guard nul_guard("nul-dir", nul_directory);
    CHECK(nul_guard.state() == Guard::State::Error);
    CHECK(nul_guard.error_code() == EINVAL);
}

void test_path_limits()
{
    // 超过 4096：在平台实现之前就被通用路径检查拒绝。
    const Guard huge("long-path", std::string(5000, 'a'));
    CHECK(huge.state() == Guard::State::Error);
    CHECK(huge.error_code() == ENAMETOOLONG);

    // 超过 Windows MAX_PATH 但短于 4096：由 Windows 平台实现映射成 ENAMETOOLONG。
    const std::string medium(300, 'a');
    const Guard medium_guard("long-path", medium);
    CHECK(medium_guard.state() == Guard::State::Error);
    CHECK(medium_guard.error_code() == ENAMETOOLONG);
}

void test_lock_path_is_directory(const std::string& directory)
{
    // 与 POSIX 对等：锁文件位置是目录时两个平台都必须是 EISDIR。
    const std::string key = "lock-path-dir";
    const std::string path = test_support::lock_path(directory, key);
    CHECK(test_support::create_directory(path));

    const Guard guard(key, directory);
    CHECK(guard.state() == Guard::State::Error);
    CHECK(guard.error_code() == EISDIR);
    CHECK(guard.error_code() == 21); // 写死数值：两个平台 errno 常量必须一致

    test_support::remove_directory(path);
}

void test_intermediate_component_is_file(const std::string& directory)
{
    // 与 POSIX 对等：路径中更深的某一级是文件时必须是 ENOTDIR，而不是 ENOENT。
    const std::string file_path = test_support::join_path(directory, "nested-as-file");
    CHECK(test_support::write_marker(file_path));

    const Guard guard("nested", test_support::join_path(file_path, "sub"));
    CHECK(guard.state() == Guard::State::Error);
    CHECK(guard.error_code() == ENOTDIR);
    CHECK(guard.error_code() == 20); // 写死数值：两个平台 errno 常量必须一致

    test_support::remove_file(file_path);
}

void test_primary_and_already_running(const std::string& directory)
{
    const std::string key = "primary-and-running";
    {
        const Guard first(key, directory);
        CHECK(first.state() == Guard::State::Primary);
        CHECK(first.is_primary());
        CHECK(first.error_code() == 0);

        // Windows 没有 fd：固定 -1；可等待事件存在。
        CHECK(first.doorbell_fd() == -1);
        CHECK(first.doorbell_handle() != nullptr);

        const Guard same_process(key, directory);
        CHECK(same_process.state() == Guard::State::AlreadyRunning);
        CHECK(!same_process.is_primary());
        CHECK(same_process.error_code() == 0);

        DWORD exit_code = 99;
        CHECK(test_support::run_child("check", {directory, key}, io_timeout_ms, exit_code));
        CHECK(exit_code == 0);
    }

    const Guard next(key, directory);
    CHECK(next.is_primary());
    test_support::cleanup(directory, key);
}

void test_lock_file_lifecycle(const std::string& directory)
{
    const std::string key = "lock-file-lifecycle";
    const std::string path = test_support::lock_path(directory, key);
    {
        const Guard guard(key, directory);
        CHECK(guard.is_primary());
        CHECK(test_support::file_exists(path));
    }
    // 与 POSIX 语义一致：锁释放后不删除锁文件。
    CHECK(test_support::file_exists(path));
    {
        const Guard guard(key, directory);
        CHECK(guard.is_primary());
    }
    test_support::cleanup(directory, key);
}

void test_cross_process_lock(const std::string& directory)
{
    const std::string key = "cross-process-lock";
    const std::string ready = test_support::join_path(directory, "hold-ready");
    const std::string release = test_support::join_path(directory, "hold-release");

    test_support::ChildProcess child = test_support::spawn_child("hold", {directory, key, ready, release});
    CHECK(child.process != nullptr);
    CHECK(test_support::wait_for_file(ready, io_timeout_ms));

    {
        // 另一个进程持有锁：本进程得到 AlreadyRunning。
        const Guard secondary(key, directory);
        CHECK(secondary.state() == Guard::State::AlreadyRunning);
        CHECK(secondary.error_code() == 0);
        CHECK(secondary.doorbell_handle() == nullptr);
    }

    DWORD exit_code = 99;
    CHECK(test_support::run_child("check", {directory, key}, io_timeout_ms, exit_code));
    CHECK(exit_code == 0);

    CHECK(test_support::write_marker(release));
    CHECK(test_support::wait_child(child, io_timeout_ms, exit_code));
    CHECK(exit_code == 0);

    {
        const Guard guard(key, directory);
        CHECK(guard.is_primary());
    }
    test_support::remove_file(ready);
    test_support::remove_file(release);
    test_support::cleanup(directory, key);
}

void test_crash_releases_lock(const std::string& directory)
{
    const std::string key = "crash-releases-lock";
    const std::string ready = test_support::join_path(directory, "crash-ready");
    const std::string release = test_support::join_path(directory, "crash-release");

    test_support::ChildProcess child = test_support::spawn_child("hold", {directory, key, ready, release});
    CHECK(child.process != nullptr);
    CHECK(test_support::wait_for_file(ready, io_timeout_ms));
    CHECK(test_support::terminate_child(child));

    // 进程被强制终止后，系统应当释放它持有的文件锁。
    bool primary = false;
    for (int attempt = 0; attempt < 100 && !primary; ++attempt)
    {
        const Guard guard(key, directory);
        primary = guard.is_primary();
        if (!primary)
        {
            ::Sleep(20);
        }
    }
    CHECK(primary);
    test_support::remove_file(ready);
    test_support::remove_file(release);
    test_support::cleanup(directory, key);
}

void test_default_directory()
{
    const std::string key = "default-directory";
    const std::string runtime_dir = test_support::default_runtime_dir();
    CHECK(!runtime_dir.empty());
    {
        const Guard guard(key);
        CHECK(guard.is_primary());
        CHECK(test_support::file_exists(test_support::lock_path(runtime_dir, key)));
    }
    test_support::cleanup(runtime_dir, key);
}

void test_doorbell_same_process(const std::string& directory)
{
    const std::string key = "doorbell-same-process";
    int handler_count = 0;
    {
        Guard primary(key, directory);
        CHECK(primary.is_primary());
        CHECK(primary.doorbell_fd() == -1);
        CHECK(primary.doorbell_handle() != nullptr);
        primary.set_doorbell_handler(
            [&handler_count]
            {
                ++handler_count;
            });

        {
            const Guard secondary(key, directory);
            CHECK(secondary.state() == Guard::State::AlreadyRunning);
            CHECK(secondary.error_code() == 0);
            CHECK(secondary.doorbell_fd() == -1);
            CHECK(secondary.doorbell_handle() == nullptr);
        }

        CHECK(primary.process_doorbell(2000));
        CHECK(handler_count == 1);
        CHECK(!primary.process_doorbell(0));

        {
            const Guard secondary(key, directory);
            CHECK(secondary.state() == Guard::State::AlreadyRunning);
        }
        CHECK(primary.process_doorbell(2000));
        CHECK(handler_count == 2);
    }
    test_support::cleanup(directory, key);
}

void test_doorbell_handler_can_be_set_late(const std::string& directory)
{
    const std::string key = "doorbell-late-handler";
    int handler_count = 0;
    {
        Guard primary(key, directory);
        {
            const Guard secondary(key, directory);
            CHECK(secondary.state() == Guard::State::AlreadyRunning);
        }

        // 通知已到达但 handler 为空：不执行回调，通知保留。
        CHECK(!primary.process_doorbell(0));

        primary.set_doorbell_handler(
            [&handler_count]
            {
                ++handler_count;
            });
        CHECK(primary.process_doorbell(0));
        CHECK(handler_count == 1);
    }
    test_support::cleanup(directory, key);
}

void test_doorbell_external_event(const std::string& directory)
{
    const std::string key = "doorbell-external-event";
    int handler_count = 0;
    {
        Guard primary(key, directory);
        auto* ready_event = static_cast<HANDLE>(primary.doorbell_handle());
        CHECK(ready_event != nullptr);
        CHECK(::WaitForSingleObject(ready_event, 0) == WAIT_TIMEOUT);

        primary.set_doorbell_handler(
            [&handler_count]
            {
                ++handler_count;
            });

        {
            const Guard secondary(key, directory);
            CHECK(secondary.state() == Guard::State::AlreadyRunning);
        }

        // 模拟 QWinEventNotifier：句柄有信号后再调用 process_doorbell(0)。
        CHECK(::WaitForSingleObject(ready_event, 2000) == WAIT_OBJECT_0);
        CHECK(primary.process_doorbell(0));
        CHECK(handler_count == 1);
        CHECK(::WaitForSingleObject(ready_event, 0) == WAIT_TIMEOUT);
    }
    test_support::cleanup(directory, key);
}

void test_doorbell_cancel_stress(const std::string& directory)
{
    // 每次构造 Guard 都会投递一个 pending ConnectNamedPipe，随后立刻析构会
    // 走 CancelIoEx 取消路径；同时用辅助线程不停连接/关闭管道制造真实竞态。
    // 修复前这里取消后不等 IO 完成就关闭句柄/释放 OVERLAPPED，属于 UAF 契约
    // 违反；修复后取消路径必须等 IO 真正完成。压测同时验证门铃随后仍可用。
    const std::string key = "doorbell-cancel-stress";
    cancel_pipe_name() = R"(\\.\pipe\singleproc-)" + test_support::user_identifier() + "-" + key + ".doorbell";
    ::InterlockedExchange(&cancel_stop_flag(), 0);
    const HANDLE thread = ::CreateThread(nullptr, 0, cancel_helper, nullptr, 0, nullptr);
    CHECK(thread != nullptr);

    for (int index = 0; index < 200; ++index)
    {
        const Guard guard(key, directory);
        CHECK(guard.is_primary());
    }

    ::InterlockedExchange(&cancel_stop_flag(), 1);
    CHECK(::WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    (void)::CloseHandle(thread);

    int handler_count = 0;
    {
        Guard primary(key, directory);
        CHECK(primary.is_primary());
        primary.set_doorbell_handler(
            [&handler_count]
            {
                ++handler_count;
            });
        DWORD exit_code = 99;
        CHECK(test_support::run_child("notify", {directory, key}, io_timeout_ms, exit_code));
        CHECK(exit_code == 0);
        CHECK(primary.process_doorbell(2000));
        CHECK(handler_count == 1);
    }
    test_support::cleanup(directory, key);
}

void test_doorbell_cross_process(const std::string& directory)
{
    const std::string key = "doorbell-cross-process";
    int handler_count = 0;
    {
        Guard primary(key, directory);
        CHECK(primary.is_primary());
        CHECK(primary.doorbell_handle() != nullptr);
        primary.set_doorbell_handler(
            [&handler_count]
            {
                ++handler_count;
            });

        // 真实两进程：子进程拿到 AlreadyRunning 并触达门铃。
        DWORD exit_code = 99;
        CHECK(test_support::run_child("notify", {directory, key}, io_timeout_ms, exit_code));
        CHECK(exit_code == 0);

        CHECK(primary.process_doorbell(2000));
        CHECK(handler_count == 1);
    }
    test_support::cleanup(directory, key);
}

void test_notifications_coalesce(const std::string& directory)
{
    const std::string key = "doorbell-coalesce";
    int handler_count = 0;
    {
        Guard primary(key, directory);
        primary.set_doorbell_handler(
            [&handler_count]
            {
                ++handler_count;
            });

        {
            const Guard first(key, directory);
            CHECK(first.state() == Guard::State::AlreadyRunning);
        }
        {
            const Guard second(key, directory);
            CHECK(second.state() == Guard::State::AlreadyRunning);
        }

        CHECK(primary.process_doorbell(2000));
        CHECK(handler_count == 1);
    }
    test_support::cleanup(directory, key);
}

void test_handler_exception_is_not_swallowed(const std::string& directory)
{
    const std::string key = "doorbell-exception";
    int handler_count = 0;
    {
        Guard primary(key, directory);
        primary.set_doorbell_handler(
            [&handler_count]
            {
                ++handler_count;
                throw std::runtime_error("doorbell handler");
            });

        {
            const Guard secondary(key, directory);
            CHECK(secondary.state() == Guard::State::AlreadyRunning);
        }

        bool caught = false;
        try
        {
            (void)primary.process_doorbell(2000);
        }
        catch (const std::runtime_error&)
        {
            caught = true;
        }
        CHECK(caught);
        CHECK(handler_count == 1);
        CHECK(!primary.process_doorbell(0));
    }
    test_support::cleanup(directory, key);
}

} // namespace

/// @brief 集成测试套件：真实 Windows 锁、命名管道门铃与子进程流程。
class TestWin32
{
public:
    /// @brief 执行全部流程用例（含子进程模式），返回进程退出码。
    ///
    /// @param argc 命令行参数个数。
    /// @param argv 命令行参数；argc > 1 时进入子进程模式。
    static int run(int argc, char** argv);
};

int TestWin32::run(int argc, char** argv)
{
    if (argc > 1)
    {
        return run_child_mode(argc, argv);
    }

    const std::string directory = test_support::make_temp_dir();
    if (directory.empty())
    {
        (void)std::fprintf(stderr, "cannot create temporary directory\n");
        return 1;
    }

    test_invalid_keys(directory);
    test_invalid_directories(directory);
    test_path_limits();
    test_lock_path_is_directory(directory);
    test_intermediate_component_is_file(directory);
    test_primary_and_already_running(directory);
    test_lock_file_lifecycle(directory);
    test_cross_process_lock(directory);
    test_crash_releases_lock(directory);
    test_default_directory();
    test_doorbell_same_process(directory);
    test_doorbell_handler_can_be_set_late(directory);
    test_doorbell_external_event(directory);
    test_doorbell_cancel_stress(directory);
    test_doorbell_cross_process(directory);
    test_notifications_coalesce(directory);
    test_handler_exception_is_not_swallowed(directory);

    test_support::remove_directory(directory);
    return test_support::finish("singleproc_win32_integration");
}

int main(int argc, char** argv)
{
    return TestWin32::run(argc, argv);
}

#endif // _WIN32
