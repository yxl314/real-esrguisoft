/// @file
/// @brief Guard 的 POSIX 实现：flock 独占锁 + Unix domain socket 门铃。

#include "platform.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <poll.h>
#include <string>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{

constexpr std::size_t max_path_length = 4096;    // Linux PATH_MAX 上限
constexpr int doorbell_connect_timeout_ms = 100; // 本地 socket 的短连接上限
constexpr int doorbell_backlog = 16;             // 通知会合并，不需要很大的队列

/// 返回默认锁目录：优先 XDG_RUNTIME_DIR，其次 /tmp。
std::string default_runtime_dir()
{
    const char* value = std::getenv("XDG_RUNTIME_DIR");
    if (value != nullptr && value[0] != '\0')
    {
        return {value};
    }
    return {"/tmp"};
}

/// 拼接目录和文件名；目录为空时使用默认目录。
std::string join_path(const std::string& directory, const std::string& name)
{
    std::string base = directory;
    if (base.empty())
    {
        base = default_runtime_dir();
    }
    if (base[base.size() - 1] != '/')
    {
        base += '/';
    }
    return base + name;
}

/// 生成运行时文件路径（锁文件与门铃 socket 共用同一套命名规则）。
///
/// 锁与 socket 只有后缀不同。分开写两遍的话，改命名规则（比如加入用户名或
/// 缩短路径以适应 sun_path 的 108 字节上限）时很容易只改一处，两个文件就
/// 落到了不同的命名空间。
std::string make_runtime_path(const std::string& directory, const std::string& key, const char* suffix)
{
    const std::string name =
        "singleproc-" + std::to_string(static_cast<unsigned long>(::getuid())) + "-" + key + suffix;
    return join_path(directory, name);
}

/// 生成锁文件路径。
std::string make_lock_path(const std::string& directory, const std::string& key)
{
    return make_runtime_path(directory, key, ".lock");
}

/// 生成门铃 socket 路径。
std::string make_socket_path(const std::string& directory, const std::string& key)
{
    return make_runtime_path(directory, key, ".sock");
}

/// 把路径转换成 Unix domain socket 地址。
bool make_unix_address(const std::string& path, sockaddr_un& address)
{
    if (path.empty() || path.size() >= sizeof(address.sun_path))
    {
        return false;
    }
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return true;
}

bool set_close_on_exec(int fd)
{
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags < 0)
    {
        return false;
    }
    return ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool set_nonblocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0)
    {
        return false;
    }
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

/// 二次实例：尝试一次短连接，把固定的单字节通知送给主实例。
void notify_doorbell(const std::string& socket_path)
{
    sockaddr_un address;
    if (!make_unix_address(socket_path, address))
    {
        return;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return;
    }
    if (!set_close_on_exec(fd) || !set_nonblocking(fd))
    {
        ::close(fd);
        return;
    }

    const int result = ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result != 0 && errno != EINPROGRESS)
    {
        ::close(fd);
        return;
    }
    if (result != 0)
    {
        pollfd descriptor;
        descriptor.fd = fd;
        descriptor.events = POLLOUT;
        descriptor.revents = 0;
        if (::poll(&descriptor, 1, doorbell_connect_timeout_ms) <= 0)
        {
            ::close(fd);
            return;
        }

        int error = 0;
        socklen_t length = sizeof(error);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0)
        {
            ::close(fd);
            return;
        }
    }

    // 连接本身已经表示“二次启动发生了”；标记字节只用于完成一次发送，
    // 主实例不读取它。失败时静默忽略，通知是 best-effort。
    const char marker = 1;
    int send_flags = 0;
#ifdef MSG_NOSIGNAL
    send_flags |= MSG_NOSIGNAL;
#endif
    (void)::send(fd, &marker, 1, send_flags);
    ::close(fd);
}

/// 主实例：取得文件锁后创建并监听门铃 socket。
/// 返回监听 fd；任何一步失败都返回 -1，由调用方保持门铃禁用。
int start_doorbell(const std::string& socket_path)
{
    sockaddr_un address;
    if (!make_unix_address(socket_path, address))
    {
        return -1;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return -1;
    }
    if (!set_close_on_exec(fd) || !set_nonblocking(fd))
    {
        ::close(fd);
        return -1;
    }

    // 已经持有文件锁，同名 socket 只可能是上次异常终止留下的残留。
    if (::unlink(socket_path.c_str()) != 0 && errno != ENOENT)
    {
        ::close(fd);
        return -1;
    }

    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        ::close(fd);
        return -1;
    }

    if (::chmod(socket_path.c_str(), 0600) != 0)
    {
        ::close(fd);
        (void)::unlink(socket_path.c_str());
        return -1;
    }

    if (::listen(fd, doorbell_backlog) != 0)
    {
        ::close(fd);
        (void)::unlink(socket_path.c_str());
        return -1;
    }

    return fd;
}

} // namespace

namespace singleproc
{
namespace detail
{

/// @brief POSIX 平台状态：锁文件 fd、门铃 socket 路径与监听 fd。
struct platform_state
{
    /// @brief 锁文件 fd；未取得独占锁时为 -1。
    int lock_fd = -1;

    /// @brief 门铃监听 fd；门铃未启用或创建失败时为 -1。
    int doorbell_fd = -1;

    /// @brief 门铃 socket 的路径；socket 地址不可用时为空。
    std::string socket_path;
};

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

    if (value->doorbell_fd >= 0)
    {
        ::close(value->doorbell_fd);
        value->doorbell_fd = -1;
        if (!value->socket_path.empty())
        {
            (void)::unlink(value->socket_path.c_str());
        }
    }

    if (value->lock_fd >= 0)
    {
        ::close(value->lock_fd);
        value->lock_fd = -1;
    }

    delete value;
}

lock_result acquire_lock(platform_state* value, const std::string& key, const std::string& runtime_dir, int& out_error)
{
    const std::string lock_path = make_lock_path(runtime_dir, key);
    if (lock_path.size() >= max_path_length)
    {
        out_error = ENAMETOOLONG;
        return lock_result::error;
    }

    value->socket_path = make_socket_path(runtime_dir, key);
    sockaddr_un socket_address;
    if (!make_unix_address(value->socket_path, socket_address))
    {
        // socket 路径超出平台限制时只禁门铃，单实例锁仍然有效。
        value->socket_path.clear();
    }

    int flags = O_RDWR | O_CREAT;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif

    const int fd = ::open(lock_path.c_str(), flags, 0600);
    if (fd < 0)
    {
        out_error = errno;
        return lock_result::error;
    }

    // O_CLOEXEC 在部分平台可能缺失；fcntl 作为统一兜底。
    if (!set_close_on_exec(fd))
    {
        const int error = errno;
        ::close(fd);
        out_error = error;
        return lock_result::error;
    }

    struct stat file_status;
    if (::fstat(fd, &file_status) != 0)
    {
        const int error = errno;
        ::close(fd);
        out_error = error;
        return lock_result::error;
    }

    if (!S_ISREG(file_status.st_mode))
    {
        ::close(fd);
        out_error = EINVAL;
        return lock_result::error;
    }

    if (file_status.st_uid != ::getuid())
    {
        ::close(fd);
        out_error = EACCES;
        return lock_result::error;
    }

    if (::fchmod(fd, 0600) != 0)
    {
        const int error = errno;
        ::close(fd);
        out_error = error;
        return lock_result::error;
    }

    if (::flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        const int error = errno;
        ::close(fd);
        // Linux 上 EWOULDBLOCK 与 EAGAIN 相等；分开判断会触发 -Wlogical-op。
        if (error == EAGAIN || (EWOULDBLOCK != EAGAIN && error == EWOULDBLOCK))
        {
            return lock_result::already_running;
        }
        out_error = error;
        return lock_result::error;
    }

    value->lock_fd = fd;
    return lock_result::primary;
}

void start_doorbell(platform_state* value)
{
    if (value == nullptr || value->socket_path.empty())
    {
        return;
    }
    value->doorbell_fd = ::start_doorbell(value->socket_path);
}

void notify_doorbell(platform_state* value)
{
    if (value == nullptr || value->socket_path.empty())
    {
        return;
    }
    ::notify_doorbell(value->socket_path);
}

bool poll_doorbell(platform_state* value, int timeout_ms)
{
    if (value == nullptr || value->doorbell_fd < 0)
    {
        return false;
    }

    pollfd descriptor;
    descriptor.fd = value->doorbell_fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    int result = 0;
    do
    {
        result = ::poll(&descriptor, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result <= 0)
    {
        return false;
    }

    bool received = false;
    for (;;)
    {
        const int client = ::accept(value->doorbell_fd, nullptr, nullptr);
        if (client < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        // 连接本身即为通知；没有 payload，接受后立即关闭。
        ::close(client);
        received = true;
    }
    return received;
}

int doorbell_fd(const platform_state* value) noexcept
{
    return value == nullptr ? -1 : value->doorbell_fd;
}

void* doorbell_wait_handle(const platform_state* value) noexcept
{
    // POSIX 平台没有可等待句柄，调用方改用 poll/select 或事件循环。
    (void)value;
    return nullptr;
}

} // namespace detail
} // namespace singleproc
