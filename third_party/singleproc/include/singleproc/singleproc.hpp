/// @file
/// @brief 单实例运行守卫 Guard 与二次启动门铃的回调类型。
///
/// 同一个用户、同一个 key 在同一时刻只允许一个进程持有独占锁；主实例
/// 可以另外启用门铃，接收二次实例发出的启动通知。
///
/// Windows 上按 Windows 会话隔离：同一用户的不同会话（如 ssh/服务所在
/// session 0 与登录桌面 session 1、并发 RDP 会话）可各自运行一个实例，
/// 互不阻塞；POSIX 上仍为同一用户范围内唯一。
///
/// 支持平台：
/// - Linux / POSIX：独占锁用 `flock`，门铃用 Unix domain socket；
/// - Windows：独占锁用独占共享模式的文件句柄，门铃用命名管道。
/// 公开 API 在两个平台上完全一致，平台差异只在错误码取值和
/// `doorbell_fd()` 的可用性上体现。

#ifndef SINGLEPROC_SINGLEPROC_HPP
#define SINGLEPROC_SINGLEPROC_HPP

#include <functional>
#include <string>

namespace singleproc
{

/// @brief 二次启动通知的回调类型。
///
/// 回调在调用 process_doorbell() 的线程中同步执行，必须保持简短。
using DoorbellHandler = std::function<void()>;

namespace detail
{

/// @brief 平台相关状态的占位声明；完整定义在各自的平台实现文件中。
struct platform_state;

} // namespace detail

/// @brief 单实例运行守卫。
///
/// 构造时尝试取得进程级独占锁；一个进程持有锁期间，同 key 的其它进程
/// 会得到 AlreadyRunning。锁由内核对象承担，进程退出或异常终止后由内核释放。
///
/// 守卫不删除锁文件，因为删除锁文件会与其它进程产生 inode 竞争；
/// 锁文件长期存在不代表有实例在运行。
///
/// 主实例还可以启用二次启动门铃：二次实例在得到 AlreadyRunning 后向
/// 主实例发送一次无载荷通知，主实例通过 process_doorbell() 分发回调。
/// 门铃失败不会改变 state() 或 error_code()。
///
/// @note Guard 的方法不是线程安全的；门铃回调也不在库内部线程中执行。
class Guard
{
public:
    /// @brief 启动判定的结果。
    enum class State
    {
        /// @brief 当前进程取得独占权。
        Primary,

        /// @brief 同一个 key 已有进程在运行。
        AlreadyRunning,

        /// @brief key、目录或锁文件不可用。
        Error
    };

    /// @brief 尝试取得 key 对应的独占锁。
    ///
    /// 失败通过 state() 和 error_code() 报告：key 非法、目录含 NUL，或锁
    /// 文件不是普通文件时得到 EINVAL；锁文件不符合当前用户安全约束时得到
    /// EACCES；路径过长时得到 ENAMETOOLONG；其余系统调用失败带回对应的
    /// 错误码（POSIX 为 errno，Windows 为等价 errno 取值）。
    /// 同一进程对同一个 key 再建一个 Guard 也会得到 AlreadyRunning。
    ///
    /// 门铃资源路径超出平台限制，或 socket 创建/命名管道创建失败时只禁用
    /// 门铃，独占锁仍然有效。
    ///
    /// @param key 应用唯一名，只允许字母、数字、'.'、'_'、'-'。
    /// @param runtime_dir 锁文件和门铃资源的目录；POSIX 下为空时使用
    ///     XDG_RUNTIME_DIR，再退回 /tmp；Windows 下为空时使用 %TEMP%。
    ///     Windows 把该参数按 UTF-8 解释，路径可以使用中文。
    explicit Guard(const std::string& key, const std::string& runtime_dir = std::string());

    /// @brief 关闭门铃监听、删除 socket 文件并释放独占锁。
    ~Guard();

    /// @brief 禁用拷贝和移动：锁与门铃的生命周期只跟随这个对象。
    ///
    /// 解锁和门铃清理都绑定在对象生命周期上，复制或移动会让所有权
    /// 变得含糊。
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    Guard(Guard&&) = delete;
    Guard& operator=(Guard&&) = delete;

    /// @brief 返回启动判定的结果。
    ///
    /// @return Primary、AlreadyRunning 或 Error。
    State state() const noexcept;

    /// @brief 当前进程是否取得独占权。
    ///
    /// Error 也返回 false；需要区分“已有实例”和“锁失败”时请查 state()。
    ///
    /// @return Primary 时为 true，其余情况为 false。
    bool is_primary() const noexcept;

    /// @brief 返回 Error 时的错误码；正常或 AlreadyRunning 时为 0。
    ///
    /// POSIX 平台返回 errno；Windows 平台返回由 Win32 错误码翻译出的
    /// 等价 errno 取值，便于调用方用同一套判断处理两个平台。
    ///
    /// @return Error 状态下的错误码；其余状态为 0。
    int error_code() const noexcept;

    /// @brief 注册二次启动通知的回调。
    ///
    /// 重复调用时后设置的回调生效。回调为空时保留待处理通知，等待之后的
    /// process_doorbell() 再分发。
    ///
    /// @param handler 通知到达后执行的函数；传空表示暂停分发。
    void set_doorbell_handler(DoorbellHandler handler);

    /// @brief 处理待处理的二次启动通知。
    ///
    /// timeout_ms 为 0 时非阻塞；小于 0 时等待到有通知为止，被信号中断时
    /// 继续等待（POSIX）。
    /// 门铃不可用时直接返回 false；收到通知时若 handler 为空，也不执行回调，
    /// 通知保留到调用方设置 handler 之后再分发。
    /// 多个连接会被合并，一次调用最多执行一次回调。
    /// handler 抛出异常时通知已消费，不会重放。
    /// 门铃与文件锁互不影响：门铃失败不会把 Primary 变成 Error。
    ///
    /// @param timeout_ms 最长等待毫秒数；0 表示不等待，负数表示无限等待。
    /// @return true 表示至少执行了一次回调。
    bool process_doorbell(int timeout_ms = 0);

    /// @brief 返回门铃监听 fd；门铃不可用时返回 -1。
    ///
    /// 这是借用 fd：归 Guard 所有，有效至 Guard 析构；调用方不得 close
    /// 或修改它。可以交给 poll、select 或 Qt QSocketNotifier 监听；
    /// fd 可读时再调用 process_doorbell(0)，不要直接读取该 fd。
    ///
    /// @note Windows 平台没有 fd 抽象，始终返回 -1；Windows 下请用
    ///     doorbell_wait_handle() 或定时调用 process_doorbell(0)。
    ///
    /// @return 门铃监听 fd；门铃未启用、创建失败或平台不支持时为 -1。
    int doorbell_fd() const noexcept;

#ifdef _WIN32
    /// @brief Windows 专用：返回门铃命名管道的可等待句柄。
    ///
    /// 句柄可以用 WaitForSingleObject 或 Qt 的 Windows 事件转发机制等待；
    /// 就绪后调用 process_doorbell(0) 分发通知。这是借用句柄：归 Guard
    /// 所有，调用方不得 CloseHandle 或修改它。
    ///
    /// @return 可等待的事件句柄；门铃未启用时返回 nullptr。
    void* doorbell_wait_handle() const noexcept;
#endif

private:
    /// @brief 平台相关状态；由平台实现创建和销毁，这里只持有指针。
    detail::platform_state* platform_ = nullptr;

    /// @brief 启动判定的结果。
    State state_ = State::Error;

    /// @brief Error 状态下的错误码；其余状态为 0。
    int error_code_ = 0;

    /// @brief 已注册的二次启动回调。
    DoorbellHandler doorbell_handler_;

    /// @brief 已接入连接但尚未分发的通知。
    bool doorbell_pending_ = false;
};

} // namespace singleproc

#endif // SINGLEPROC_SINGLEPROC_HPP
