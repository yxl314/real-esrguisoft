// 本文件只在 Windows 构建中编译；整文件加平台守卫，Linux 门禁的 standardize
// 会把它当作空翻译单元，避免找不到 windows.h 的误报。
#ifdef _WIN32

/// @file
/// @brief Windows 门铃确定性回归测试：客户端在 ConnectNamedPipe
/// 之前连上并关闭。
///
/// 真实二次实例的动作是“CreateFileW 连接之后立刻 CloseHandle”。如果它恰好
/// 落在主实例 CreateNamedPipeW 与 ConnectNamedPipe 之间的窗口里，Windows 会
/// 让 ConnectNamedPipe 返回 FALSE + ERROR_NO_DATA（客户端已关闭）。平台实现
/// 必须把这种情况当成“连接发生过”的通知并重新挂起监听，否则门铃会被永久
/// 禁用。测试用宏钩子把 ConnectNamedPipe 延迟 300ms 来稳定复现这个窗口：
/// 主线程执行 start_doorbell（保持为投递重叠 IO 的线程），辅助线程在窗口内
/// 执行“连接 + 立刻关闭”。

#include "support/test_support_win32.hpp"

#include <string>

// 先声明钩子，再把平台实现里的 ConnectNamedPipe 调用重定向到钩子。
BOOL WINAPI HookedConnectNamedPipe(HANDLE pipe, LPOVERLAPPED overlapped);
// NOLINTNEXTLINE(readability-identifier-naming): 故意用 Win32 API
// 原名做宏，才能拦截平台实现里的 ::ConnectNamedPipe 调用
#define ConnectNamedPipe HookedConnectNamedPipe
#include "../../src/platform_win32.cpp" // NOLINT(bugprone-suspicious-include): 测试内联平台实现以插入 ConnectNamedPipe 钩子
#undef ConnectNamedPipe

BOOL WINAPI
HookedConnectNamedPipe(HANDLE pipe, LPOVERLAPPED overlapped)
{
  ::Sleep(300); // 把“CreateNamedPipe 之后、ConnectNamedPipe 之前”的窗口拉长
  return ::ConnectNamedPipe(pipe, overlapped);
}

namespace
{

std::string&
target()
{
  static std::string value;
  return value;
}

volatile LONG&
client_connected()
{
  static volatile LONG value = 0;
  return value;
}

DWORD WINAPI
connect_and_close(LPVOID argument)
{
  (void)argument;
  ::Sleep(100); // 等主线程进入钩子的 sleep，管道名已存在
  const std::wstring wide = test_support::utf8_to_wide(target());
  const HANDLE client = ::CreateFileW(
      wide.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (client != INVALID_HANDLE_VALUE)
    {
      (void)::CloseHandle(client); // 连上后立刻关闭：真实二次实例的低层动作
      ::InterlockedExchange(&client_connected(), 1);
    }
  return 0;
}

} // namespace

/// @brief 集成测试套件：命名管道“连接后立刻关闭”的确定性竞态流程。
class TestWin32ConnectRace
{
public:
  /// @brief 执行竞态回归用例，返回进程退出码。
  static int run();
};

int
TestWin32ConnectRace::run()
{
  const std::string directory = test_support::make_temp_dir();
  if (directory.empty())
    {
      (void)std::fprintf(stderr, "cannot create temporary directory\n");
      return 1;
    }

  const std::string key = "connect-race";
  target() = singleproc::detail::make_doorbell_target(
      directory,
      "singleproc-" + singleproc::detail::user_identifier() + "-" + key);
  const std::wstring wide = test_support::utf8_to_wide(target());
  CHECK(!wide.empty());

  // 辅助线程在窗口内连接+关闭；主线程保持为投递重叠 IO 的线程。
  const HANDLE helper
      = ::CreateThread(nullptr, 0, connect_and_close, nullptr, 0, nullptr);
  CHECK(helper != nullptr);
  singleproc::detail::Doorbell* doorbell
      = singleproc::detail::start_doorbell(target());
  CHECK(::WaitForSingleObject(helper, 10000) == WAIT_OBJECT_0);
  (void)::CloseHandle(helper);
  CHECK(::InterlockedCompareExchange(&client_connected(), 0, 0) == 1);

  // 修复前：ERROR_NO_DATA 被当成失败，start_doorbell 返回 nullptr。
  CHECK(doorbell != nullptr);
  if (doorbell != nullptr)
    {
      // “连上又关闭”本身已经是一次通知，不能被丢掉。
      CHECK(singleproc::detail::wait_doorbell(doorbell, 2000));

      // 门铃必须仍然可用：再来一次正常的“连接 + 立刻关闭”仍能收到通知。
      const HANDLE second = ::CreateFileW(
          wide.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
      CHECK(second != INVALID_HANDLE_VALUE);
      if (second != INVALID_HANDLE_VALUE)
        {
          (void)::CloseHandle(second);
        }
      CHECK(singleproc::detail::wait_doorbell(doorbell, 2000));

      singleproc::detail::stop_doorbell(doorbell);
    }

  test_support::remove_directory(directory);
  return test_support::finish("singleproc_win32_connect_race_integration");
}

int
main()
{
  return TestWin32ConnectRace::run();
}

#endif // _WIN32
