/// @file
/// @brief Guard 的平台实现内部接口。
///
/// 公共逻辑（key 校验、状态与门铃分发）放在 singleproc.cpp；平台差异
/// 由 singleproc_posix.cpp 与 singleproc_win32.cpp 各自实现本文件声明的
/// 函数。这个头文件不安装，也不出现在公开 API 中。

#ifndef SINGLEPROC_PLATFORM_HPP
#define SINGLEPROC_PLATFORM_HPP

#include <singleproc/singleproc.hpp>

#include <string>

namespace singleproc
{
namespace detail
{

/// @brief 独占锁的获取结果。
enum class lock_result
{
    /// @brief 当前进程取得独占锁。
    primary,

    /// @brief 同一个 key 已有进程持锁。
    already_running,

    /// @brief 锁不可用；out_error 带回平台错误码。
    error
};

/// @brief 平台相关状态；POSIX 保存 fd，Windows 保存内核句柄。
struct platform_state;

/// @brief 创建平台状态。
///
/// @return 新状态；内存不足时返回 nullptr。
platform_state* create_platform();

/// @brief 关闭平台资源并释放状态。
///
/// @param value 由 create_platform() 返回的状态；可以为 nullptr。
void destroy_platform(platform_state* value) noexcept;

/// @brief 构造运行时资源名并尝试取得独占锁。
///
/// 无论是否取得锁，都会准备好门铃资源名，供 already_running 时发送通知。
///
/// @param value 平台状态。
/// @param key 已通过字符校验的应用唯一名。
/// @param runtime_dir 运行时目录；为空时使用平台默认目录。
/// @param out_error 失败时写入平台错误码（POSIX 为 errno，Windows 为等价 errno）。
/// @return primary、already_running 或 error。
lock_result acquire_lock(platform_state* value, const std::string& key, const std::string& runtime_dir, int& out_error);

/// @brief 主实例启动门铃监听；任何失败都只禁用门铃。
///
/// @param value 平台状态。
void start_doorbell(platform_state* value);

/// @brief 二次实例发送一次无载荷通知；失败时静默忽略。
///
/// @param value 平台状态。
void notify_doorbell(platform_state* value);

/// @brief 等待并接收至多一批通知。
///
/// @param value 平台状态。
/// @param timeout_ms 最长等待毫秒数；0 表示不等待，负数表示无限等待。
/// @return 本次是否收到至少一个通知。
bool poll_doorbell(platform_state* value, int timeout_ms);

/// @brief 返回门铃监听 fd；平台没有 fd 抽象时返回 -1。
///
/// @param value 平台状态；可以为 nullptr。
/// @return 监听 fd 或 -1。
int doorbell_fd(const platform_state* value) noexcept;

/// @brief 返回门铃可等待句柄；平台没有该能力时返回 nullptr。
///
/// @param value 平台状态；可以为 nullptr。
/// @return 可等待句柄或 nullptr。
void* doorbell_wait_handle(const platform_state* value) noexcept;

} // namespace detail
} // namespace singleproc

#endif // SINGLEPROC_PLATFORM_HPP
