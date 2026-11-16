#include <singleproc/singleproc.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif

namespace
{

/// 解析可选的 hold=<毫秒> 参数。
///
/// hold 让取得锁的测试进程保持存活，供跨进程检查锁的独占与释放。
///
/// @param value 命令行参数；为空表示不保持。
/// @return 保持毫秒数；参数非法时返回 -1。
int parse_hold_ms(const char* value)
{
    if (value == nullptr)
    {
        return 0;
    }

    const char prefix[] = "hold=";
    const std::size_t prefix_length = std::strlen(prefix);
    if (std::strncmp(value, prefix, prefix_length) != 0)
    {
        return -1;
    }

    const long long milliseconds = std::strtoll(value + prefix_length, nullptr, 10);
    if (milliseconds < 0 || milliseconds > 600000)
    {
        return -1;
    }
    return static_cast<int>(milliseconds);
}

/// 保持当前进程存活指定毫秒。
void sleep_ms(int milliseconds)
{
#ifdef _WIN32
    ::Sleep(static_cast<DWORD>(milliseconds));
#else
    struct timespec duration;
    duration.tv_sec = milliseconds / 1000;
    duration.tv_nsec = static_cast<long>(milliseconds % 1000) * 1000000L;
    (void)::nanosleep(&duration, nullptr);
#endif
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4)
    {
        return 3;
    }

    const int hold_ms = argc == 4 ? parse_hold_ms(argv[3]) : 0;
    if (hold_ms < 0)
    {
        return 3;
    }

    const singleproc::Guard guard(argv[1], argv[2]);
    if (guard.state() == singleproc::Guard::State::Primary && hold_ms > 0)
    {
        sleep_ms(hold_ms);
    }

    switch (guard.state())
    {
    case singleproc::Guard::State::Primary:
        return 0;
    case singleproc::Guard::State::AlreadyRunning:
        return 1;
    case singleproc::Guard::State::Error:
        return 2;
    }
    return 2;
}
