/// @file
/// @brief 平台端口的可编排测试替身（tests/support 专用，产品目标不引用）。
///
/// 单元测试把 src/singleproc.cpp 与 fake_platform.cpp 一起编译，不链接真实的
/// platform_posix.cpp / platform_win32.cpp。所有平台函数都读写同一份
/// test_support::fake::state()：测试先编排返回值，再观察记录下来的调用。
///
/// 这个替身按 src/platform.hpp 的契约实现：锁结果、默认目录、用户标识、门铃
/// 目标合法性、门铃 start/wait/notify/stop 与原生 fd/handle 都可编排，并能
/// 模拟 start_doorbell 返回 nullptr 与抛 std::bad_alloc 两类失败。

#ifndef SINGLEPROC_TEST_FAKE_PLATFORM_HPP
#define SINGLEPROC_TEST_FAKE_PLATFORM_HPP

#include "platform.hpp"

#include <string>
#include <vector>

namespace test_support
{
namespace fake
{

/// @brief start_doorbell() 的三种行为，用于覆盖成功与两类失败路径。
enum class StartDoorbellMode
{
    /// @brief 返回可用的监听端。
    Success,

    /// @brief 返回 nullptr（平台侧创建失败）。
    Fail,

    /// @brief 抛 std::bad_alloc（取锁后的内存分配失败）。
    ThrowBadAlloc,
};

/// @brief 假平台端口的可编排状态与调用记录。
///
/// 所有字段都是“测试替身的观察点/编排点”，不是产品契约；测试在 init() 里
/// 调用 reset() 获得干净状态。
struct State
{
    /// @brief path_separator() 的返回值。
    char path_separator = '/';

    /// @brief default_runtime_dir() 的返回值；空串表示平台兜底失败。
    std::string default_runtime_dir = "/run/user/1000";

    /// @brief user_identifier() 的返回值。
    std::string user_identifier = "1000";

    /// @brief valid_doorbell_target() 的返回值。
    bool doorbell_target_valid = true;

    /// @brief make_doorbell_target() 生成目标时的前缀。
    std::string doorbell_target_prefix = "fake-doorbell:";

    /// @brief acquire_lock() 返回的结果。
    singleproc::detail::LockResult lock_result = singleproc::detail::LockResult::Primary;

    /// @brief acquire_lock() 成功时返回的锁句柄。
    singleproc::detail::LockHandle lock_handle = 7;

    /// @brief acquire_lock() 失败时的 errno。
    int lock_error_code = 0;

    /// @brief start_doorbell() 的行为。
    StartDoorbellMode start_doorbell_mode = StartDoorbellMode::Success;

    /// @brief wait_doorbell() 的返回值。
    bool wait_result = false;

    /// @brief doorbell_native_fd() 的返回值。
    int native_fd = 42;

    /// @brief doorbell_native_handle() 返回的句柄标记对象地址。
    int native_handle_token = 1;

    // ——— 调用记录 ———

    /// @brief path_separator() 调用次数。
    int path_separator_calls = 0;

    /// @brief default_runtime_dir() 调用次数。
    int default_runtime_dir_calls = 0;

    /// @brief user_identifier() 调用次数。
    int user_identifier_calls = 0;

    /// @brief acquire_lock() 收到的锁文件路径。
    std::vector<std::string> acquire_lock_paths;

    /// @brief release_lock() 收到的锁句柄。
    std::vector<singleproc::detail::LockHandle> released_handles;

    /// @brief make_doorbell_target() 调用次数。
    int make_doorbell_target_calls = 0;

    /// @brief 最近一次 make_doorbell_target() 的运行时目录参数。
    std::string last_doorbell_runtime_dir;

    /// @brief 最近一次 make_doorbell_target() 的运行时文件名参数。
    std::string last_doorbell_runtime_name;

    /// @brief valid_doorbell_target() 收到的目标。
    std::vector<std::string> checked_doorbell_targets;

    /// @brief start_doorbell() 收到的目标。
    std::vector<std::string> start_doorbell_targets;

    /// @brief stop_doorbell() 收到的监听端。
    std::vector<singleproc::detail::Doorbell*> stopped_doorbells;

    /// @brief wait_doorbell() 收到的超时值。
    std::vector<int> wait_doorbell_timeouts;

    /// @brief notify_doorbell() 收到的目标。
    std::vector<std::string> notify_doorbell_targets;

    /// @brief doorbell_native_fd() 调用次数。
    int native_fd_calls = 0;

    /// @brief doorbell_native_handle() 调用次数。
    int native_handle_calls = 0;
};

/// @brief 返回进程内唯一的假端口状态。
///
/// @return 可编排、可观察的状态引用。
State& state();

/// @brief 把所有可编排字段与调用记录恢复默认值。
void reset();

} // namespace fake
} // namespace test_support

#endif // SINGLEPROC_TEST_FAKE_PLATFORM_HPP
