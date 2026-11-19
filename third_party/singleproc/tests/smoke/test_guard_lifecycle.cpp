/// @file
/// @brief 冒烟测试：主实例取得锁 → 二次实例（同进程）收到 AlreadyRunning 并
///        触发门铃 → 主实例分发回调 → 释放后重新取得 Primary。
///
/// 使用真实平台实现（Linux/POSIX 的 flock + socket，Windows 的 LockFileEx +
/// 命名管道），以进程真实退出码 0 判定流程走通；不做分支覆盖。

#include "support/test_harness.hpp"

#include <ctime>
#include <singleproc/singleproc.hpp>
#include <string>

namespace
{

/// @brief 生成本次冒烟运行唯一且合法的 key（进程内计数 + 时间戳）。
std::string
unique_key()
{
  static int counter = 0;
  ++counter;
  return "smoke-lifecycle-"
         + std::to_string(static_cast<long long>(std::time(nullptr))) + "-"
         + std::to_string(counter);
}

} // namespace

/// @brief 冒烟套件：单实例锁与门铃的完整生命周期。
class test_guard_lifecycle
{
public:
  /// @brief 冒烟流程不使用共享状态，保留 init 以满足 runner 契约。
  static void init();

  /// @brief 冒烟流程不使用共享状态，保留 cleanup 以满足 runner 契约。
  static void cleanup();

  /// @brief 完整生命周期：Primary → 二次通知 → 释放 → 重新 Primary。
  static void guard_primary_secondary_release_reacquire_returns_to_primary();
};

void
test_guard_lifecycle::init()
{
}

void
test_guard_lifecycle::cleanup()
{
}

void
test_guard_lifecycle::
    guard_primary_secondary_release_reacquire_returns_to_primary()
{
  const std::string key = unique_key();
  int notifications = 0;

  {
    singleproc::guard primary(key);
    CHECK_EQ(static_cast<int>(primary.state()),
             static_cast<int>(singleproc::guard::status::primary));
    CHECK(primary.is_primary());

    primary.set_doorbell_handler([&notifications] { ++notifications; });

    // 无通知时非阻塞处理立即返回 false。
    CHECK(!primary.process_doorbell(0));

    {
      // 同一进程的第二个实例：AlreadyRunning，并在构造时向主实例发通知。
      singleproc::guard secondary(key);
      CHECK_EQ(static_cast<int>(secondary.state()),
               static_cast<int>(singleproc::guard::status::already_running));
      CHECK(!secondary.is_primary());
    }

    CHECK(primary.process_doorbell(3000));
    CHECK_EQ(notifications, 1);
  }

  // 主实例释放后，同 key 的新实例应重新取得 Primary。
  singleproc::guard reacquired(key);
  CHECK_EQ(static_cast<int>(reacquired.state()),
           static_cast<int>(singleproc::guard::status::primary));
  CHECK(reacquired.is_primary());
}

int
main()
{
  return test_harness::run<test_guard_lifecycle>({
      {
          "guard_primary_secondary_release_reacquire_returns_to_primary",
          &test_guard_lifecycle::
              guard_primary_secondary_release_reacquire_returns_to_primary,
      },
  });
}
