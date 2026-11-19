// 本文件只在 POSIX 构建中编译。整文件加平台守卫，Windows 门禁里的 clang-tidy
// 会把它当成空翻译单元，不会因为找不到 POSIX 头文件误报。
#ifndef _WIN32

#include "support/test_support.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <singleproc/singleproc.hpp>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{

void
test_trailing_backslash_runtime_dir(const std::string& directory)
{
  // POSIX 上反斜杠是普通文件名字符。旧实现只认 '/'，runtime_dir 以 '\\' 结尾时
  // 锁路径是 "<dir>/trail\/<name>"；新实现如果错误地把 '\\' 当分隔符，会变成
  // "<dir>/trail\<name>"，与旧版本锁到不同文件（混合版本可双开）。
  // 这里写死期望路径，不调用库里的命名函数，避免自我循环。
  const std::string key = "trailing-backslash";
  const std::string subdir = directory + "/trail\\";
  CHECK(::mkdir(subdir.c_str(), 0700) == 0);

  const std::string uid
      = std::to_string(static_cast<unsigned long>(::getuid()));
  const std::string expected
      = subdir + "/singleproc-" + uid + "-" + key + ".lock";
  const std::string wrong = subdir + "singleproc-" + uid + "-" + key + ".lock";

  {
    const singleproc::guard guard(key, subdir);
    CHECK(guard.is_primary());
  }

  struct stat expected_status = {};
  struct stat wrong_status = {};
  CHECK(::stat(expected.c_str(), &expected_status) == 0);
  CHECK(::stat(wrong.c_str(), &wrong_status) != 0);

  test_support::remove_file(expected);
  test_support::remove_file(wrong);
  CHECK(::rmdir(subdir.c_str()) == 0);
}

void
test_lock_path_is_directory(const std::string& directory)
{
  // 锁文件位置是目录：POSIX 由 open() 给出 EISDIR，Windows 平台实现映射为同值。
  const std::string key = "lock-path-dir";
  const std::string path = test_support::lock_path(directory, key);
  CHECK(::mkdir(path.c_str(), 0700) == 0);

  const singleproc::guard guard(key, directory);
  CHECK(guard.state() == singleproc::guard::status::error);
  CHECK(guard.error_code() == EISDIR);
  CHECK(guard.error_code() == 21); // 写死数值：两个平台 errno 常量必须一致

  CHECK(::rmdir(path.c_str()) == 0);
}

void
test_intermediate_component_is_file(const std::string& directory)
{
  // 路径中更深的某一级是文件：POSIX 由内核给 ENOTDIR，Windows 需要补祖先检查。
  const std::string file_path = directory + "/nested-as-file";
  const int file_fd = ::open(file_path.c_str(), O_CREAT | O_WRONLY, 0600);
  CHECK(file_fd >= 0);
  if (file_fd >= 0)
    {
      (void)::close(file_fd);
    }

  const singleproc::guard guard("nested", file_path + "/sub");
  CHECK(guard.state() == singleproc::guard::status::error);
  CHECK(guard.error_code() == ENOTDIR);
  CHECK(guard.error_code() == 20); // 写死数值：两个平台 errno 常量必须一致

  test_support::remove_file(file_path);
}

void
test_primary_and_already_running(const std::string& directory)
{
  const std::string key = "primary-and-running";
  {
    singleproc::guard first(key, directory);
    CHECK(first.state() == singleproc::guard::status::primary);
    CHECK(first.is_primary());
    CHECK(first.error_code() == 0);

    const pid_t child = ::fork();
    if (child == 0)
      {
        singleproc::guard guard(key, directory);
        ::_exit(guard.state() == singleproc::guard::status::already_running
                    ? 0
                    : 1);
      }

    int status = 0;
    CHECK(child > 0);
    CHECK(::waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);

    singleproc::guard same_process(key, directory);
    CHECK(same_process.state() == singleproc::guard::status::already_running);
    CHECK(!same_process.is_primary());
    CHECK(same_process.error_code() == 0);
  }

  singleproc::guard next(key, directory);
  CHECK(next.is_primary());
}

void
test_invalid_keys(const std::string& directory)
{
  const singleproc::guard empty("", directory);
  CHECK(empty.state() == singleproc::guard::status::error);
  CHECK(empty.error_code() == EINVAL);

  const singleproc::guard slash("../not-allowed", directory);
  CHECK(slash.state() == singleproc::guard::status::error);
  CHECK(slash.error_code() == EINVAL);

  const singleproc::guard space("not allowed", directory);
  CHECK(space.state() == singleproc::guard::status::error);
  CHECK(space.error_code() == EINVAL);

  std::string nul_key("nul");
  nul_key.push_back('\0');
  const singleproc::guard nul(nul_key, directory);
  CHECK(nul.state() == singleproc::guard::status::error);
  CHECK(nul.error_code() == EINVAL);
}

void
test_invalid_directories(const std::string& directory)
{
  const std::string missing = directory + "/missing-directory";
  const singleproc::guard missing_guard("missing", missing);
  CHECK(missing_guard.state() == singleproc::guard::status::error);
  CHECK(missing_guard.error_code() == ENOENT);

  const std::string file_path = directory + "/as-file";
  const int file_fd = ::open(file_path.c_str(), O_CREAT | O_WRONLY, 0600);
  CHECK(file_fd >= 0);
  if (file_fd >= 0)
    {
      (void)::close(file_fd);
    }

  const singleproc::guard file_guard("file", file_path);
  CHECK(file_guard.state() == singleproc::guard::status::error);
  CHECK(file_guard.error_code() == ENOTDIR);
  test_support::remove_file(file_path);

  std::string nul_directory = directory;
  nul_directory.push_back('\0');
  const singleproc::guard nul_guard("nul-dir", nul_directory);
  CHECK(nul_guard.state() == singleproc::guard::status::error);
  CHECK(nul_guard.error_code() == EINVAL);
}

void
test_path_too_long()
{
  const std::string long_directory(5000, 'a');
  const singleproc::guard guard("long-path", long_directory);
  CHECK(guard.state() == singleproc::guard::status::error);
  CHECK(guard.error_code() == ENAMETOOLONG);
}

void
test_lock_file_permissions(const std::string& directory)
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
    const singleproc::guard guard(key, directory);
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

void
test_symlink_lock_file(const std::string& directory)
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

  const singleproc::guard guard(key, directory);
  CHECK(guard.state() == singleproc::guard::status::error);
  CHECK(guard.error_code() == ELOOP);

  test_support::remove_file(path);
  test_support::remove_file(target);
}

void
test_non_regular_lock_file(const std::string& directory)
{
  const std::string key = "fifo-lock";
  const std::string path = test_support::lock_path(directory, key);

  CHECK(::mkfifo(path.c_str(), 0600) == 0);
  const singleproc::guard guard(key, directory);
  CHECK(guard.state() == singleproc::guard::status::error);
  CHECK(guard.error_code() == EINVAL);
  test_support::remove_file(path);
}

void
test_unwritable_directory(const std::string& directory)
{
  if (::geteuid() == 0)
    {
      return;
    }

  const std::string read_only = directory + "/read-only";
  CHECK(::mkdir(read_only.c_str(), 0500) == 0);

  const singleproc::guard guard("read-only", read_only);
  CHECK(guard.state() == singleproc::guard::status::error);
  CHECK(guard.error_code() == EACCES);

  CHECK(::chmod(read_only.c_str(), 0700) == 0);
  test_support::remove_directory(read_only);
}

void
test_default_directory(const std::string& directory)
{
  const char* old_value = std::getenv("XDG_RUNTIME_DIR");
  const bool had_old_value = old_value != nullptr;
  const std::string saved_value
      = had_old_value ? std::string(old_value) : std::string();
  struct stat file_status = {};

  const std::string key
      = "xdg-" + std::to_string(static_cast<long long>(::getpid()));
  CHECK(::setenv("XDG_RUNTIME_DIR", directory.c_str(), 1) == 0);
  {
    const singleproc::guard guard(key);
    CHECK(guard.is_primary());

    CHECK(::stat(test_support::lock_path(directory, key).c_str(), &file_status)
          == 0);
  }
  test_support::cleanup(directory, key);

  CHECK(::unsetenv("XDG_RUNTIME_DIR") == 0);
  const std::string fallback_key
      = "fallback-" + std::to_string(static_cast<long long>(::getpid()));
  {
    const singleproc::guard guard(fallback_key);
    CHECK(guard.is_primary());
    CHECK(::stat(test_support::lock_path("/tmp", fallback_key).c_str(),
                 &file_status)
          == 0);
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

} // namespace

/// @brief 集成测试套件：guard 在真实 POSIX 文件系统上的复合流程。
class test_guard
{
public:
  /// @brief 执行全部流程用例，返回进程退出码。
  static int run();
};

int
test_guard::run()
{
  const std::string directory = test_support::make_temp_dir();
  if (directory.empty())
    {
      (void)std::fprintf(stderr, "cannot create temporary directory\n");
      return 1;
    }

  test_primary_and_already_running(directory);
  test_trailing_backslash_runtime_dir(directory);
  test_lock_path_is_directory(directory);
  test_intermediate_component_is_file(directory);
  test_invalid_keys(directory);
  test_invalid_directories(directory);
  test_path_too_long();
  test_lock_file_permissions(directory);
  test_symlink_lock_file(directory);
  test_non_regular_lock_file(directory);
  test_unwritable_directory(directory);
  test_default_directory(directory);

  test_support::cleanup(directory, "primary-and-running");
  test_support::remove_directory(directory);

  return test_support::finish("singleproc_guard_integration");
}

int
main()
{
  return test_guard::run();
}

#endif // !defined(_WIN32)
