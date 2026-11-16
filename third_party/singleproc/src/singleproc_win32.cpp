/// @file
/// @brief Guard 的 Windows 实现：独占共享模式文件句柄 + 命名管道门铃。
///
/// 平台差异：
/// - 锁：CreateFileW(share=0) 取得独占文件句柄。第二个进程拿不到共享权，
///   得到 ERROR_SHARING_VIOLATION，即 AlreadyRunning；进程退出（含被
///   TerminateProcess 强杀）时内核关闭句柄，锁自动释放。
/// - 门铃：主实例创建若干命名管道实例并用重叠 IO 等待连接；二次实例用
///   CreateFileW 连一次、写一个标记字节后关闭。连接本身即通知。
/// - 公开参数 std::string 按 UTF-8 解释，内部转换为 UTF-16 调用 Win32 API，
///   因此中文目录可以正常使用。
/// - 资源命名：锁文件与门铃管道名都带当前 Windows 会话 ID，即“每会话单
///   实例”。同一用户可能同时存在于多个会话（sshd/服务位于 session 0、
///   登录桌面在 session 1，或并发 RDP 会话），历史的全机器单实例会让
///   后台/自动化实例静默拦截用户桌面的启动（门铃窗口还不可见），故按
///   会话隔离；POSIX 语义不变。

#include "platform.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <new>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace
{

/// 门铃使用固定数量的命名管道实例，对应 POSIX 的 accept backlog。
constexpr int doorbell_instance_count = 4;

/// 二次实例连接门铃的最长等待时间；与 POSIX 的短连接上限一致。
constexpr DWORD doorbell_connect_timeout_ms = 100;

/// Windows 单段路径长度上限。
constexpr std::size_t max_component_length = 255;

/// Windows 长路径总长度上限（\\?\ 形式）。
constexpr std::size_t max_path_length = 32767;

/// 命名管道名总长度上限。
constexpr std::size_t max_pipe_name_length = 256;

/// 把 UTF-8 字符串转换为宽字符；非法 UTF-8 时回退到系统 ACP。
///
/// 公开 API 使用 std::string 以保持与 POSIX 一致；Windows 上约定为 UTF-8，
/// 这样中文目录不会按 ACP 错误解码。对旧的窄字符调用方保留 ACP 回退。
std::wstring utf8_to_wide(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }

    const int size = static_cast<int>(value.size());
    UINT code_page = CP_UTF8;
    int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size, nullptr, 0);
    if (length <= 0)
    {
        code_page = CP_ACP;
        length = ::MultiByteToWideChar(CP_ACP, 0, value.data(), size, nullptr, 0);
    }
    if (length <= 0)
    {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(length), L'\0');
    (void)::MultiByteToWideChar(code_page,
                                code_page == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
                                value.data(),
                                size,
                                &result[0],
                                length);
    return result;
}

/// 把 Win32 错误码翻译成 POSIX errno 等价值，便于调用方统一判断。
int errno_from_win32(DWORD code)
{
    switch (code)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
        return ENOENT;
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return EACCES;
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_PARAMETER:
    case ERROR_BAD_PATHNAME:
        return EINVAL;
    case ERROR_DIRECTORY:
        return ENOTDIR;
    case ERROR_FILENAME_EXCED_RANGE:
        return ENAMETOOLONG;
    case ERROR_TOO_MANY_OPEN_FILES:
        return EMFILE;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return ENOMEM;
    default:
        return EIO;
    }
}

/// 判断路径每一段都不超过 Windows 单段长度上限。
bool components_fit(const std::wstring& path)
{
    std::size_t current = 0;
    for (const wchar_t ch : path)
    {
        if (ch == L'\\' || ch == L'/')
        {
            current = 0;
            continue;
        }
        ++current;
        if (current > max_component_length)
        {
            return false;
        }
    }
    return true;
}

/// 把路径规范化为绝对路径；超长时补 \\?\ 前缀。
///
/// @return 绝对路径；无法规范化时返回空字符串。
std::wstring absolute_path(const std::wstring& path)
{
    if (path.empty())
    {
        return {};
    }

    const DWORD required = ::GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (required == 0)
    {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const DWORD written = ::GetFullPathNameW(path.c_str(), required, &result[0], nullptr);
    if (written == 0 || written >= required)
    {
        return {};
    }
    result.resize(static_cast<std::size_t>(written));

    if (result.size() >= MAX_PATH)
    {
        if (result.size() > 2 && result[0] == L'\\' && result[1] == L'\\')
        {
            result = L"\\\\?\\UNC\\" + result.substr(2);
        }
        else
        {
            result = L"\\\\?\\" + result;
        }
    }
    return result;
}

/// 返回受限字符集形式的当前用户名，避免资源名里出现路径分隔符等字符。
std::wstring user_component()
{
    DWORD length = 0;
    (void)::GetUserNameW(nullptr, &length);
    if (length == 0)
    {
        return L"user";
    }

    std::wstring name(static_cast<std::size_t>(length), L'\0');
    if (!::GetUserNameW(&name[0], &length))
    {
        return L"user";
    }
    name.resize(length > 0 ? static_cast<std::size_t>(length - 1) : 0);
    if (name.empty())
    {
        return L"user";
    }

    for (std::size_t index = 0; index < name.size(); ++index)
    {
        const wchar_t ch = name[index];
        const bool alpha = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
        const bool digit = ch >= L'0' && ch <= L'9';
        if (!alpha && !digit && ch != L'.' && ch != L'_' && ch != L'-')
        {
            name[index] = L'_';
        }
    }
    return name;
}

/// 返回当前进程所在的 Windows 会话 ID（形如 s0/s1），用于资源命名。
///
/// 同一用户可能同时存在于多个会话（sshd/服务在 session 0，登录桌面在
/// session 1，或并发 RDP 会话）。把会话号加入锁与门铃资源名，可实现
/// “每会话单实例”，避免后台/自动化实例静默拦截用户桌面的启动。
std::wstring session_component()
{
    DWORD session_id = 0;
    if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &session_id))
    {
        session_id = 0;
    }
    return L"s" + std::to_wstring(static_cast<unsigned long>(session_id));
}

/// 返回默认运行时目录：系统临时目录（通常是 %TEMP%）。
std::wstring default_runtime_dir()
{
    std::wstring result(static_cast<std::size_t>(MAX_PATH) + 1, L'\0');
    DWORD written = ::GetTempPathW(static_cast<DWORD>(result.size()), &result[0]);
    if (written == 0)
    {
        return L".";
    }
    if (written >= result.size())
    {
        result.assign(static_cast<std::size_t>(written), L'\0');
        written = ::GetTempPathW(static_cast<DWORD>(result.size()), &result[0]);
        if (written == 0 || written >= result.size())
        {
            return L".";
        }
    }
    result.resize(static_cast<std::size_t>(written));
    while (!result.empty() && (result.back() == L'\\' || result.back() == L'/'))
    {
        result.pop_back();
    }
    return result.empty() ? std::wstring(L".") : result;
}

/// 拼接目录与文件名。
std::wstring join_path(const std::wstring& directory, const std::wstring& name)
{
    if (directory.empty())
    {
        return name;
    }
    const wchar_t last = directory.back();
    if (last == L'\\' || last == L'/')
    {
        return directory + name;
    }
    return directory + L'\\' + name;
}

/// 生成门铃命名管道名；超过命名管道长度上限时返回空字符串。
std::wstring make_pipe_name(const std::wstring& user,
                           const std::wstring& session,
                           const std::string& key)
{
    std::wstring name = L"\\\\.\\pipe\\singleproc-" + user + L"-" + session
                        + L"-" + utf8_to_wide(key);
    if (name.size() > max_pipe_name_length)
    {
        return {};
    }
    return name;
}

} // namespace

namespace singleproc
{
namespace detail
{

/// @brief 单个命名管道实例的等待状态。
struct pipe_instance
{
    /// @brief 管道实例句柄；创建失败时为 INVALID_HANDLE_VALUE。
    HANDLE pipe = INVALID_HANDLE_VALUE;

    /// @brief 重叠 IO 结构；hEvent 指向共享的门铃事件。
    OVERLAPPED overlapped = {};

    /// @brief 是否已发出 ConnectNamedPipe 且尚未完成。
    bool connecting = false;

    /// @brief 是否已有客户端连接、尚未消费。
    bool connected = false;
};

/// @brief Windows 平台状态：锁句柄、门铃事件、管道实例池与资源名。
struct platform_state
{
    /// @brief 锁文件句柄；未取得独占锁时为 INVALID_HANDLE_VALUE。
    HANDLE lock_handle = INVALID_HANDLE_VALUE;

    /// @brief 门铃共享事件；用 WaitForSingleObject 等待任意实例完成连接。
    HANDLE doorbell_event = nullptr;

    /// @brief 门铃命名管道名；不可用时为空。
    std::wstring pipe_name;

    /// @brief 门铃管道实例池。
    std::array<pipe_instance, doorbell_instance_count> instances;

    /// @brief 门铃是否可用。
    bool doorbell_enabled = false;
};

} // namespace detail
} // namespace singleproc

namespace
{

using singleproc::detail::pipe_instance;
using singleproc::detail::platform_state;

/// 发起一次重叠 ConnectNamedPipe；彻底失败时禁用门铃。
void arm_instance(platform_state* value, pipe_instance& instance)
{
    if (!value->doorbell_enabled || instance.pipe == INVALID_HANDLE_VALUE)
    {
        return;
    }

    instance.overlapped = {};
    instance.overlapped.hEvent = value->doorbell_event;
    instance.connecting = false;
    instance.connected = false;

    const BOOL result = ::ConnectNamedPipe(instance.pipe, &instance.overlapped);
    if (result)
    {
        instance.connected = true;
        return;
    }

    const DWORD error = ::GetLastError();
    if (error == ERROR_PIPE_CONNECTED)
    {
        // 客户端在 ConnectNamedPipe 之前就已经连上。
        instance.connected = true;
        return;
    }
    if (error == ERROR_IO_PENDING)
    {
        instance.connecting = true;
        return;
    }
    value->doorbell_enabled = false;
}

/// 给所有闲置实例重新布防。
void arm_idle(platform_state* value)
{
    for (pipe_instance& instance : value->instances)
    {
        if (instance.connecting || instance.connected)
        {
            continue;
        }
        arm_instance(value, instance);
    }
}

/// 非阻塞回收已完成的连接；返回发现的通知数量。
int drain_instances(platform_state* value)
{
    int received = 0;
    for (pipe_instance& instance : value->instances)
    {
        if (!instance.connecting)
        {
            if (instance.connected)
            {
                ++received;
            }
            continue;
        }

        DWORD transferred = 0;
        if (::GetOverlappedResult(instance.pipe, &instance.overlapped, &transferred, FALSE))
        {
            instance.connecting = false;
            instance.connected = true;
            ++received;
            continue;
        }
        if (::GetLastError() != ERROR_IO_INCOMPLETE)
        {
            // 连接被取消或管道已断开：重置该实例后重新布防。
            instance.connecting = false;
            instance.connected = false;
            (void)::DisconnectNamedPipe(instance.pipe);
            arm_instance(value, instance);
        }
    }
    return received;
}

/// 消费所有已连接实例并重新布防；返回是否消费了至少一个通知。
bool consume_instances(platform_state* value)
{
    bool consumed = false;
    for (pipe_instance& instance : value->instances)
    {
        if (!instance.connected)
        {
            continue;
        }
        consumed = true;
        instance.connected = false;
        (void)::DisconnectNamedPipe(instance.pipe);
    }

    // 先清事件再布防：布防时立即完成的连接会重新置位事件，不会被漏掉。
    (void)::ResetEvent(value->doorbell_event);
    arm_idle(value);
    return consumed;
}

/// 关闭门铃资源；不触碰锁句柄。
void destroy_doorbell(platform_state* value) noexcept
{
    for (pipe_instance& instance : value->instances)
    {
        if (instance.pipe != INVALID_HANDLE_VALUE)
        {
            (void)::CancelIoEx(instance.pipe, nullptr);
            (void)::CloseHandle(instance.pipe);
            instance.pipe = INVALID_HANDLE_VALUE;
        }
        instance.connecting = false;
        instance.connected = false;
    }

    if (value->doorbell_event != nullptr)
    {
        (void)::CloseHandle(value->doorbell_event);
        value->doorbell_event = nullptr;
    }
    value->doorbell_enabled = false;
}

} // namespace

namespace singleproc
{
namespace detail
{

platform_state* create_platform()
{
    return new (std::nothrow) platform_state;
}

void destroy_platform(platform_state* value) noexcept
{
    if (value == nullptr)
    {
        return;
    }

    destroy_doorbell(value);

    if (value->lock_handle != INVALID_HANDLE_VALUE)
    {
        (void)::CloseHandle(value->lock_handle);
        value->lock_handle = INVALID_HANDLE_VALUE;
    }
    delete value;
}

lock_result acquire_lock(platform_state* value, const std::string& key, const std::string& runtime_dir, int& out_error)
{
    const std::wstring directory = runtime_dir.empty() ? default_runtime_dir() : utf8_to_wide(runtime_dir);
    if (directory.empty() || !components_fit(directory))
    {
        out_error = ENAMETOOLONG;
        return lock_result::error;
    }

    const std::wstring user = user_component();
    const std::wstring session = session_component();
    const std::wstring lock_name = L"singleproc-" + user + L"-" + session + L"-"
                                   + utf8_to_wide(key) + L".lock";
    const std::wstring lock_path = absolute_path(join_path(directory, lock_name));
    if (lock_path.empty() || lock_path.size() >= max_path_length)
    {
        out_error = ENAMETOOLONG;
        return lock_result::error;
    }

    // 门铃失败只禁用通知；先准备好管道名，already_running 时可以直接发送。
    value->pipe_name = make_pipe_name(user, session, key);

    // 独占共享模式：同一锁文件已有任何打开句柄时，CreateFileW 返回
    // ERROR_SHARING_VIOLATION。OPEN_ALWAYS 与 POSIX 的 O_CREAT 对齐。
    // OPEN_REPARSE_POINT 用于识别并拒绝符号链接/重解析点（对应 O_NOFOLLOW）。
    const HANDLE handle = ::CreateFileW(lock_path.c_str(),
                                        GENERIC_READ | GENERIC_WRITE,
                                        0,
                                        nullptr,
                                        OPEN_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                                        nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        const DWORD error = ::GetLastError();
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION)
        {
            return lock_result::already_running;
        }
        out_error = errno_from_win32(error);
        return lock_result::error;
    }

    BY_HANDLE_FILE_INFORMATION information = {};
    if (!::GetFileInformationByHandle(handle, &information))
    {
        const DWORD error = ::GetLastError();
        (void)::CloseHandle(handle);
        out_error = errno_from_win32(error);
        return lock_result::error;
    }

    if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        (void)::CloseHandle(handle);
        out_error = ELOOP;
        return lock_result::error;
    }

    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        (void)::CloseHandle(handle);
        out_error = EINVAL;
        return lock_result::error;
    }

    value->lock_handle = handle;
    return lock_result::primary;
}

void start_doorbell(platform_state* value)
{
    if (value == nullptr || value->pipe_name.empty())
    {
        return;
    }

    value->doorbell_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (value->doorbell_event == nullptr)
    {
        return;
    }

    DWORD pipe_mode = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT;
#ifdef PIPE_REJECT_REMOTE_CLIENTS
    pipe_mode |= PIPE_REJECT_REMOTE_CLIENTS;
#endif

    for (pipe_instance& instance : value->instances)
    {
        instance.pipe = ::CreateNamedPipeW(value->pipe_name.c_str(),
                                           PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                           pipe_mode,
                                           PIPE_UNLIMITED_INSTANCES,
                                           64,
                                           64,
                                           0,
                                           nullptr);
        if (instance.pipe == INVALID_HANDLE_VALUE)
        {
            destroy_doorbell(value);
            return;
        }
    }

    value->doorbell_enabled = true;
    arm_idle(value);
}

void notify_doorbell(platform_state* value)
{
    if (value == nullptr || value->pipe_name.empty())
    {
        return;
    }

    HANDLE pipe = ::CreateFileW(value->pipe_name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE && ::GetLastError() == ERROR_PIPE_BUSY)
    {
        // 所有实例都在处理上一次通知时短暂等待一次；仍失败则放弃。
        if (::WaitNamedPipeW(value->pipe_name.c_str(), doorbell_connect_timeout_ms))
        {
            pipe = ::CreateFileW(value->pipe_name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        }
    }
    if (pipe == INVALID_HANDLE_VALUE)
    {
        return;
    }

    // 连接本身已经表示“二次启动发生了”；标记字节只用于完成一次发送，
    // 主实例不读取它。失败时静默忽略，通知是 best-effort。
    const char marker = 1;
    DWORD written = 0;
    (void)::WriteFile(pipe, &marker, 1, &written, nullptr);
    (void)::CloseHandle(pipe);
}

bool poll_doorbell(platform_state* value, int timeout_ms)
{
    if (value == nullptr || !value->doorbell_enabled)
    {
        return false;
    }

    arm_idle(value);
    if (drain_instances(value) > 0)
    {
        return consume_instances(value);
    }
    if (timeout_ms == 0)
    {
        return false;
    }

    const DWORD wait_ms = timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms);
    const DWORD wait_result = ::WaitForSingleObject(value->doorbell_event, wait_ms);
    if (wait_result != WAIT_OBJECT_0)
    {
        return false;
    }

    (void)::ResetEvent(value->doorbell_event);
    if (drain_instances(value) <= 0)
    {
        // 事件在回收与等待之间被消费掉了；本次按无通知处理。
        return false;
    }
    return consume_instances(value);
}

int doorbell_fd(const platform_state* value) noexcept
{
    // Windows 没有 poll/select 可用的 fd 抽象。
    (void)value;
    return -1;
}

void* doorbell_wait_handle(const platform_state* value) noexcept
{
    if (value == nullptr || !value->doorbell_enabled)
    {
        return nullptr;
    }
    return value->doorbell_event;
}

} // namespace detail
} // namespace singleproc
