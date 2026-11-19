/// @file
/// @brief 冒烟测试：真实两进程业务线——主实例持锁，第二个进程得到
///        AlreadyRunning 并触发门铃；主实例释放后，第二个进程能成为 Primary。
///
/// 使用真实平台实现与真实子进程（本测试程序按 argv[1] 进入子进程模式），
/// 以子进程退出码 0 与主实例收到通知判定流程走通。

#include "support/test_harness.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <singleproc/singleproc.hpp>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

/// @brief 当前测试程序自身的路径（main 的 argv[0]），用于启动子进程。
std::string&
self_path()
{
  static std::string value;
  return value;
}

/// @brief 生成本次冒烟运行唯一且合法的 key（进程内计数 + 时间戳）。
std::string
unique_key()
{
  static int counter = 0;
  ++counter;
  return "smoke-second-"
         + std::to_string(static_cast<long long>(std::time(nullptr))) + "-"
         + std::to_string(counter);
}

/// @brief 以当前可执行文件启动一个子进程，返回子进程退出码。
///
/// 使用平台原生的无 shell 启动方式（Windows `_spawnl`，POSIX `fork` +
/// `execv`）， 避免命令字符串被 shell 二次解析，也避免把参数拼进命令行。
///
/// @param mode 子进程模式：secondary 或 primary。
/// @param key 单实例 key。
/// @return 子进程退出码；无法启动时返回 -1，非正常退出返回 -2。
int
run_self(const std::string& mode, const std::string& key)
{
  const std::string& self = self_path();
#ifdef _WIN32
  const intptr_t status = ::_spawnl(
      _P_WAIT, self.c_str(), self.c_str(), mode.c_str(), key.c_str(), nullptr);
  if (status == -1)
    {
      return -1;
    }
  return static_cast<int>(status);
#else
  const pid_t child = ::fork();
  if (child < 0)
    {
      return -1;
    }
  if (child == 0)
    {
      ::execl(self.c_str(),
              self.c_str(),
              mode.c_str(),
              key.c_str(),
              static_cast<char*>(nullptr));
      ::_exit(127);
    }
  int status = 0;
  if (::waitpid(child, &status, 0) < 0)
    {
      return -1;
    }
  if (WIFEXITED(status))
    {
      return WEXITSTATUS(status);
    }
  return -2;
#endif
}

/// @brief 子进程模式：按模式创建 guard 并校验期望状态。
///
/// @param mode secondary 期望 AlreadyRunning；primary 期望 Primary。
/// @param key 单实例 key。
/// @return 行为符合期望时返回 0，否则返回非 0。
int
run_child_mode(const std::string& mode, const std::string& key)
{
  const singleproc::guard guard(key);
  if (mode == "secondary")
    {
      if (guard.state() != singleproc::guard::status::already_running)
        {
          (void)std::fprintf(stderr,
                             "secondary child: unexpected state %d\n",
                             static_cast<int>(guard.state()));
          return 2;
        }
      return 0;
    }
  if (mode == "primary")
    {
      if (guard.state() != singleproc::guard::status::primary)
        {
          (void)std::fprintf(stderr,
                             "primary child: unexpected state %d\n",
                             static_cast<int>(guard.state()));
          return 3;
        }
      return 0;
    }
  return 4;
}

} // namespace

/// @brief 冒烟套件：二次启动与锁释放的完整业务流程。
class test_second_instance
{
public:
  /// @brief 冒烟流程不使用共享状态，保留 init 以满足 runner 契约。
  static void init();

  /// @brief 冒烟流程不使用共享状态，保留 cleanup 以满足 runner 契约。
  static void cleanup();

  /// @brief 子进程得到 AlreadyRunning 并通知主实例；释放后子进程成为 Primary。
  static void
  guard_second_process_notifies_then_becomes_primary_after_release();
};

void
test_second_instance::init()
{
}

void
test_second_instance::cleanup()
{
}

void
test_second_instance::
    guard_second_process_notifies_then_becomes_primary_after_release()
{
  const std::string key = unique_key();
  int notifications = 0;

  {
    singleproc::guard primary(key);
    CHECK_EQ(static_cast<int>(primary.state()),
             static_cast<int>(singleproc::guard::status::primary));
    primary.set_doorbell_handler([&notifications] { ++notifications; });

    // 第二个真实进程：取得 AlreadyRunning，并在构造时向主实例发门铃通知。
    CHECK_EQ(run_self("secondary", key), 0);
    CHECK(primary.process_doorbell(3000));
    CHECK_EQ(notifications, 1);
  }

  // 主实例退出后，新进程应能取得 Primary。
  CHECK_EQ(run_self("primary", key), 0);
}

int
main(int argc, char** argv)
{
  if (argc > 0)
    {
      self_path() = argv[0];
    }
  if (argc == 3
      && (std::strcmp(argv[1], "secondary") == 0
          || std::strcmp(argv[1], "primary") == 0))
    {
      return run_child_mode(argv[1], argv[2]);
    }

  return test_harness::run<test_second_instance>({
      {
          "guard_second_process_notifies_then_becomes_primary_after_release",
          &test_second_instance::
              guard_second_process_notifies_then_becomes_primary_after_release,
      },
  });
}
