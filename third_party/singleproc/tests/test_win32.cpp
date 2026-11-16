/// @file
/// @brief Windows 专属进程级测试：双进程唯一性、锁随进程退出释放、跨进程门铃。

#include <singleproc/singleproc.hpp>

#include "test_support.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace
{

constexpr DWORD child_timeout_ms = 10000;

/// 给命令行参数加引号；路径中可能包含空格或中文。
std::wstring quote_argument(const std::string& value)
{
    const std::wstring wide = test_support::to_wide(value);
    std::wstring result = L"\"";
    for (const wchar_t character : wide)
    {
        if (character == L'"')
        {
            result += L'\\';
        }
        result += character;
    }
    result += L'"';
    return result;
}

/// 启动 probe 子进程；成功时返回进程句柄，调用方负责关闭。
///
/// @param probe_path probe 可执行文件路径。
/// @param arguments 传给 probe 的命令行参数。
/// @return 进程句柄；启动失败时返回 nullptr。
HANDLE spawn_probe(const std::string& probe_path, const std::vector<std::string>& arguments)
{
    std::wstring command_line = quote_argument(probe_path);
    for (const std::string& argument : arguments)
    {
        command_line += L' ';
        command_line += quote_argument(argument);
    }

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION information = {};
    const std::wstring executable = test_support::to_wide(probe_path);
    if (!::CreateProcessW(executable.c_str(),
                          &command_line[0],
                          nullptr,
                          nullptr,
                          FALSE,
                          0,
                          nullptr,
                          nullptr,
                          &startup,
                          &information))
    {
        return nullptr;
    }
    (void)::CloseHandle(information.hThread);
    return information.hProcess;
}

/// 等待 probe 退出并返回退出码；超时或失败返回 -1。
int wait_probe(HANDLE process)
{
    if (::WaitForSingleObject(process, child_timeout_ms) != WAIT_OBJECT_0)
    {
        (void)::TerminateProcess(process, 1);
        (void)::WaitForSingleObject(process, child_timeout_ms);
        (void)::CloseHandle(process);
        return -1;
    }

    DWORD exit_code = 0;
    const bool ok = ::GetExitCodeProcess(process, &exit_code) != 0;
    (void)::CloseHandle(process);
    return ok ? static_cast<int>(exit_code) : -1;
}

/// 判断运行时目录内是否已经产生锁文件。
bool directory_has_lock_file(const std::string& directory)
{
    const std::wstring pattern = test_support::to_wide(directory) + L"\\singleproc-*.lock";
    WIN32_FIND_DATAW entry = {};
    HANDLE finder = ::FindFirstFileW(pattern.c_str(), &entry);
    if (finder == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    (void)::FindClose(finder);
    return true;
}

void test_two_processes_unique(const std::string& directory, const std::string& probe_path)
{
    const std::string key = "win32-unique";

    // 子进程先拿到锁并保持存活，父进程必须看到 AlreadyRunning。
    HANDLE child = spawn_probe(probe_path, {key, directory, "hold=30000"});
    CHECK(child != nullptr);
    if (child == nullptr)
    {
        return;
    }

    bool child_holds_lock = false;
    for (int attempt = 0; attempt < 500 && !child_holds_lock; ++attempt)
    {
        if (::WaitForSingleObject(child, 0) == WAIT_OBJECT_0)
        {
            // 子进程提前退出，说明它没有拿到锁。
            break;
        }
        if (directory_has_lock_file(directory))
        {
            const singleproc::Guard probe(key, directory);
            if (probe.state() == singleproc::Guard::State::AlreadyRunning)
            {
                child_holds_lock = true;
                break;
            }
        }
        ::Sleep(10);
    }
    CHECK(child_holds_lock);
    if (!child_holds_lock)
    {
        (void)::TerminateProcess(child, 1);
        (void)::WaitForSingleObject(child, child_timeout_ms);
        (void)::CloseHandle(child);
        return;
    }
    CHECK(directory_has_lock_file(directory));

    // 强杀子进程后，内核关闭锁句柄，锁必须自动释放。
    CHECK(::TerminateProcess(child, 1) != 0);
    CHECK(::WaitForSingleObject(child, child_timeout_ms) == WAIT_OBJECT_0);
    (void)::CloseHandle(child);

    bool acquired = false;
    for (int attempt = 0; attempt < 250 && !acquired; ++attempt)
    {
        const singleproc::Guard guard(key, directory);
        if (guard.is_primary())
        {
            acquired = true;
        }
        else
        {
            ::Sleep(10);
        }
    }
    CHECK(acquired);

    test_support::cleanup(directory, key);
}

void test_doorbell_from_process(const std::string& directory, const std::string& probe_path)
{
    const std::string key = "win32-doorbell";
    int handler_count = 0;
    {
        singleproc::Guard primary(key, directory);
        CHECK(primary.is_primary());
        primary.set_doorbell_handler(
            [&handler_count]()
            {
                ++handler_count;
            });

        HANDLE child = spawn_probe(probe_path, {key, directory});
        CHECK(child != nullptr);
        if (child == nullptr)
        {
            return;
        }
        // probe 对 AlreadyRunning 返回 1。
        CHECK(wait_probe(child) == 1);

        CHECK(primary.process_doorbell(3000));
        CHECK(handler_count == 1);
    }
    test_support::cleanup(directory, key);
}

void test_doorbell_coalesce_from_processes(const std::string& directory, const std::string& probe_path)
{
    const std::string key = "win32-doorbell-coalesce";
    int handler_count = 0;
    {
        singleproc::Guard primary(key, directory);
        CHECK(primary.is_primary());
        primary.set_doorbell_handler(
            [&handler_count]()
            {
                ++handler_count;
            });

        HANDLE first = spawn_probe(probe_path, {key, directory});
        HANDLE second = spawn_probe(probe_path, {key, directory});
        CHECK(first != nullptr);
        CHECK(second != nullptr);
        if (first == nullptr || second == nullptr)
        {
            if (first != nullptr)
            {
                (void)::TerminateProcess(first, 1);
                (void)::CloseHandle(first);
            }
            if (second != nullptr)
            {
                (void)::TerminateProcess(second, 1);
                (void)::CloseHandle(second);
            }
            return;
        }
        CHECK(wait_probe(first) == 1);
        CHECK(wait_probe(second) == 1);

        // 两次通知在同一个事件循环周期内到达，只执行一次回调。
        CHECK(primary.process_doorbell(3000));
        CHECK(handler_count == 1);
    }
    test_support::cleanup(directory, key);
}

void test_utf8_directory(const std::string& directory)
{
    const std::string key = "win32-utf8";
    const std::string chinese_directory = directory + "/中文目录";
    CHECK(::CreateDirectoryW(test_support::to_wide(chinese_directory).c_str(), nullptr) != 0);

    {
        const singleproc::Guard guard(key, chinese_directory);
        CHECK(guard.is_primary());
        CHECK(guard.error_code() == 0);
    }

    test_support::remove_directory(chinese_directory);
    test_support::cleanup(directory, key);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        (void)std::fprintf(stderr, "probe path is required\n");
        return 1;
    }
    const std::string probe_path = argv[1];

    const std::string directory = test_support::make_temp_dir();
    if (directory.empty())
    {
        (void)std::fprintf(stderr, "cannot create temporary directory\n");
        return 1;
    }

    test_two_processes_unique(directory, probe_path);
    test_doorbell_from_process(directory, probe_path);
    test_doorbell_coalesce_from_processes(directory, probe_path);
    test_utf8_directory(directory);

    test_support::remove_directory(directory);
    return test_support::finish("singleproc_win32_tests");
}
