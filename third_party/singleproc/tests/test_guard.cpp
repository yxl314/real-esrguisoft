#include <singleproc/singleproc.hpp>

#include "test_support.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

void test_primary_and_already_running(const std::string& directory)
{
    const std::string key = "primary-and-running";
    {
        singleproc::Guard first(key, directory);
        CHECK(first.state() == singleproc::Guard::State::Primary);
        CHECK(first.is_primary());
        CHECK(first.error_code() == 0);

        // 同一进程再建一个同 key 守卫也必须得到 AlreadyRunning；
        // 两个平台都由内核对象保证（POSIX flock、Windows 独占句柄）。
        const singleproc::Guard same_process(key, directory);
        CHECK(same_process.state() == singleproc::Guard::State::AlreadyRunning);
        CHECK(!same_process.is_primary());
        CHECK(same_process.error_code() == 0);
    }

    // 守卫析构后锁释放，可以重新取得。
    const singleproc::Guard next(key, directory);
    CHECK(next.is_primary());
    test_support::cleanup(directory, key);
}

void test_invalid_keys(const std::string& directory)
{
    const singleproc::Guard empty("", directory);
    CHECK(empty.state() == singleproc::Guard::State::Error);
    CHECK(empty.error_code() == EINVAL);

    const singleproc::Guard slash("../not-allowed", directory);
    CHECK(slash.state() == singleproc::Guard::State::Error);
    CHECK(slash.error_code() == EINVAL);

    const singleproc::Guard space("not allowed", directory);
    CHECK(space.state() == singleproc::Guard::State::Error);
    CHECK(space.error_code() == EINVAL);

    std::string nul_key("nul");
    nul_key.push_back('\0');
    const singleproc::Guard nul(nul_key, directory);
    CHECK(nul.state() == singleproc::Guard::State::Error);
    CHECK(nul.error_code() == EINVAL);
}

void test_missing_directory(const std::string& directory)
{
    const std::string missing = directory + "/missing-directory";
    const singleproc::Guard missing_guard("missing", missing);
    CHECK(missing_guard.state() == singleproc::Guard::State::Error);
    CHECK(missing_guard.error_code() == ENOENT);

    std::string nul_directory = directory;
    nul_directory.push_back('\0');
    const singleproc::Guard nul_guard("nul-dir", nul_directory);
    CHECK(nul_guard.state() == singleproc::Guard::State::Error);
    CHECK(nul_guard.error_code() == EINVAL);
}

void test_path_too_long()
{
    const std::string long_directory(5000, 'a');
    const singleproc::Guard guard("long-path", long_directory);
    CHECK(guard.state() == singleproc::Guard::State::Error);
    CHECK(guard.error_code() == ENAMETOOLONG);
}

#ifndef _WIN32

void test_child_process_already_running(const std::string& directory)
{
    const std::string key = "child-process";
    const singleproc::Guard primary(key, directory);
    CHECK(primary.is_primary());

    const pid_t child = ::fork();
    if (child == 0)
    {
        singleproc::Guard guard(key, directory);
        ::_exit(guard.state() == singleproc::Guard::State::AlreadyRunning ? 0 : 1);
    }

    int status = 0;
    CHECK(child > 0);
    CHECK(::waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    test_support::cleanup(directory, key);
}

void test_file_instead_of_directory(const std::string& directory)
{
    const std::string file_path = directory + "/as-file";
    const int file_fd = ::open(file_path.c_str(), O_CREAT | O_WRONLY, 0600);
    CHECK(file_fd >= 0);
    if (file_fd >= 0)
    {
        (void)::close(file_fd);
    }

    const singleproc::Guard file_guard("file", file_path);
    CHECK(file_guard.state() == singleproc::Guard::State::Error);
    CHECK(file_guard.error_code() == ENOTDIR);
    test_support::remove_file(file_path);
}

void test_lock_file_permissions(const std::string& directory)
{
    const std::string key = "permissions";
    const std::string path = test_support::lock_path(directory, key);

    const int file_fd = ::open(path.c_str(), O_CREAT | O_WRONLY, 0600);
    CHECK(file_fd >= 0);
    if (file_fd >= 0)
    {
        CHECK(::fchmod(file_fd, 0666) == 0);
        (void)::close(file_fd);
    }

    {
        const singleproc::Guard guard(key, directory);
        CHECK(guard.is_primary());

        struct stat file_status = {};
        const bool stat_ok = ::stat(path.c_str(), &file_status) == 0;
        CHECK(stat_ok);
        if (stat_ok)
        {
            CHECK((file_status.st_mode & 0777) == 0600);
        }
    }

    test_support::cleanup(directory, key);
}

void test_symlink_lock_file(const std::string& directory)
{
    const std::string key = "symlink";
    const std::string target = directory + "/symlink-target";
    const std::string path = test_support::lock_path(directory, key);

    const int target_fd = ::open(target.c_str(), O_CREAT | O_WRONLY, 0600);
    CHECK(target_fd >= 0);
    if (target_fd >= 0)
    {
        (void)::close(target_fd);
    }
    CHECK(::symlink(target.c_str(), path.c_str()) == 0);

    const singleproc::Guard guard(key, directory);
    CHECK(guard.state() == singleproc::Guard::State::Error);
    CHECK(guard.error_code() == ELOOP);

    test_support::remove_file(path);
    test_support::remove_file(target);
}

void test_non_regular_lock_file(const std::string& directory)
{
    const std::string key = "fifo-lock";
    const std::string path = test_support::lock_path(directory, key);

    CHECK(::mkfifo(path.c_str(), 0600) == 0);
    const singleproc::Guard guard(key, directory);
    CHECK(guard.state() == singleproc::Guard::State::Error);
    CHECK(guard.error_code() == EINVAL);
    test_support::remove_file(path);
}

void test_unwritable_directory(const std::string& directory)
{
    if (::geteuid() == 0)
    {
        return;
    }

    const std::string read_only = directory + "/read-only";
    CHECK(::mkdir(read_only.c_str(), 0500) == 0);

    const singleproc::Guard guard("read-only", read_only);
    CHECK(guard.state() == singleproc::Guard::State::Error);
    CHECK(guard.error_code() == EACCES);

    CHECK(::chmod(read_only.c_str(), 0700) == 0);
    test_support::remove_directory(read_only);
}

void test_default_directory(const std::string& directory)
{
    const char* old_value = std::getenv("XDG_RUNTIME_DIR");
    const bool had_old_value = old_value != nullptr;
    const std::string saved_value = had_old_value ? std::string(old_value) : std::string();
    struct stat file_status = {};

    const std::string key = "xdg-" + std::to_string(static_cast<long long>(::getpid()));
    CHECK(::setenv("XDG_RUNTIME_DIR", directory.c_str(), 1) == 0);
    {
        const singleproc::Guard guard(key);
        CHECK(guard.is_primary());

        CHECK(::stat(test_support::lock_path(directory, key).c_str(), &file_status) == 0);
    }
    test_support::cleanup(directory, key);

    CHECK(::unsetenv("XDG_RUNTIME_DIR") == 0);
    const std::string fallback_key = "fallback-" + std::to_string(static_cast<long long>(::getpid()));
    {
        const singleproc::Guard guard(fallback_key);
        CHECK(guard.is_primary());
        CHECK(::stat(test_support::lock_path("/tmp", fallback_key).c_str(), &file_status) == 0);
    }
    test_support::cleanup("/tmp", fallback_key);

    if (had_old_value)
    {
        (void)::setenv("XDG_RUNTIME_DIR", saved_value.c_str(), 1);
    }
    else
    {
        (void)::unsetenv("XDG_RUNTIME_DIR");
    }
}

#endif // !_WIN32

} // namespace

int main()
{
    const std::string directory = test_support::make_temp_dir();
    if (directory.empty())
    {
        (void)std::fprintf(stderr, "cannot create temporary directory\n");
        return 1;
    }

    test_primary_and_already_running(directory);
    test_invalid_keys(directory);
    test_missing_directory(directory);
    test_path_too_long();

#ifndef _WIN32
    test_child_process_already_running(directory);
    test_file_instead_of_directory(directory);
    test_lock_file_permissions(directory);
    test_symlink_lock_file(directory);
    test_non_regular_lock_file(directory);
    test_unwritable_directory(directory);
    test_default_directory(directory);
#endif

    test_support::remove_directory(directory);
    return test_support::finish("singleproc_guard_tests");
}
