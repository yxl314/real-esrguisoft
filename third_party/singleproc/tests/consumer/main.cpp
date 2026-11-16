#include <singleproc/singleproc.hpp>

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
#include <unistd.h>
#endif

namespace
{

/// 返回当前进程号；用于让每次 consumer 运行使用独立的 key。
long long current_process_id()
{
#ifdef _WIN32
    return static_cast<long long>(::GetCurrentProcessId());
#else
    return static_cast<long long>(::getpid());
#endif
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 3;
    }

    const std::string key = "consumer-" + std::to_string(current_process_id());
    const singleproc::Guard guard(key, argv[1]);
    if (guard.state() == singleproc::Guard::State::Error)
    {
        return 2;
    }
    return guard.is_primary() ? 0 : 1;
}
