#include <singleproc/singleproc.hpp>

#include "platform.hpp"

#include <algorithm>
#include <cerrno>
#include <string>
#include <utility>

namespace
{

/// key 与目录的公共校验放在公共实现里，保证非法输入在两个平台上都得到 EINVAL。

/// 判断 key 是否能安全地作为文件名的一部分。
bool valid_key(const std::string& key)
{
    if (key.empty())
    {
        return false;
    }
    return std::all_of(key.begin(),
                       key.end(),
                       [](char value)
                       {
                           const bool alpha = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
                           const bool digit = value >= '0' && value <= '9';
                           return alpha || digit || value == '.' || value == '_' || value == '-';
                       });
}

/// 目录字符串中不能嵌入 NUL，否则路径会被系统调用静默截断。
bool valid_directory(const std::string& directory)
{
    return directory.find('\0') == std::string::npos;
}

} // namespace

namespace singleproc
{

Guard::Guard(const std::string& key, const std::string& runtime_dir)
{
    platform_ = detail::create_platform();
    if (!platform_)
    {
        error_code_ = ENOMEM;
        return;
    }

    if (!valid_key(key) || !valid_directory(runtime_dir))
    {
        error_code_ = EINVAL;
        return;
    }

    int error = 0;
    const detail::lock_result result = detail::acquire_lock(platform_, key, runtime_dir, error);
    if (result == detail::lock_result::error)
    {
        error_code_ = error;
        return;
    }

    if (result == detail::lock_result::already_running)
    {
        state_ = State::AlreadyRunning;
        error_code_ = 0;
        detail::notify_doorbell(platform_);
        return;
    }

    state_ = State::Primary;
    error_code_ = 0;
    detail::start_doorbell(platform_);
}

Guard::~Guard()
{
    detail::destroy_platform(platform_);
}

Guard::State Guard::state() const noexcept
{
    return state_;
}

bool Guard::is_primary() const noexcept
{
    return state_ == State::Primary;
}

int Guard::error_code() const noexcept
{
    return error_code_;
}

void Guard::set_doorbell_handler(DoorbellHandler handler)
{
    doorbell_handler_ = std::move(handler);
}

bool Guard::process_doorbell(int timeout_ms)
{
    if (!platform_)
    {
        return false;
    }

    if (!doorbell_pending_)
    {
        if (!detail::poll_doorbell(platform_, timeout_ms))
        {
            return false;
        }
        doorbell_pending_ = true;
    }

    // 复制一份再调用：handler 内替换自己时不会销毁正在执行的函数对象。
    const DoorbellHandler handler = doorbell_handler_;
    if (!handler)
    {
        return false;
    }

    doorbell_pending_ = false;
    handler();
    return true;
}

int Guard::doorbell_fd() const noexcept
{
    return detail::doorbell_fd(platform_);
}

#ifdef _WIN32
void* Guard::doorbell_wait_handle() const noexcept
{
    return detail::doorbell_wait_handle(platform_);
}
#endif

} // namespace singleproc
