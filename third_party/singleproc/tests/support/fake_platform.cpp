/// @file
/// @brief 平台端口（src/platform.hpp）的可编排假实现，只用于单元测试。

#include "support/fake_platform.hpp"

#include <new>

namespace test_support
{
namespace fake
{

State& state()
{
    static State instance;
    return instance;
}

void reset()
{
    state() = State();
}

} // namespace fake
} // namespace test_support

namespace singleproc
{
namespace detail
{

/// @brief 假门铃监听端：只需有稳定地址，测试用指针区分 start/stop。
struct Doorbell
{
    /// @brief 占位标记，避免空结构体带来的编译器差异。
    int marker = 0;
};

namespace
{

/// @brief start_doorbell() 成功时返回的监听端，生命周期覆盖整个测试进程。
Doorbell fake_doorbell_instance;

} // namespace

char path_separator()
{
    test_support::fake::State& fake = test_support::fake::state();
    ++fake.path_separator_calls;
    return fake.path_separator;
}

std::string default_runtime_dir()
{
    test_support::fake::State& fake = test_support::fake::state();
    ++fake.default_runtime_dir_calls;
    return fake.default_runtime_dir;
}

std::string user_identifier()
{
    test_support::fake::State& fake = test_support::fake::state();
    ++fake.user_identifier_calls;
    return fake.user_identifier;
}

LockAttempt acquire_lock(const std::string& path)
{
    test_support::fake::State& fake = test_support::fake::state();
    fake.acquire_lock_paths.push_back(path);

    LockAttempt attempt;
    attempt.result = fake.lock_result;
    attempt.handle = fake.lock_result == LockResult::Primary ? fake.lock_handle : invalid_lock_handle;
    attempt.error_code = fake.lock_result == LockResult::Error ? fake.lock_error_code : 0;
    return attempt;
}

void release_lock(LockHandle handle)
{
    test_support::fake::state().released_handles.push_back(handle);
}

std::string make_doorbell_target(const std::string& runtime_dir, const std::string& runtime_name)
{
    test_support::fake::State& fake = test_support::fake::state();
    ++fake.make_doorbell_target_calls;
    fake.last_doorbell_runtime_dir = runtime_dir;
    fake.last_doorbell_runtime_name = runtime_name;
    return fake.doorbell_target_prefix + runtime_name;
}

bool valid_doorbell_target(const std::string& target)
{
    test_support::fake::State& fake = test_support::fake::state();
    fake.checked_doorbell_targets.push_back(target);
    return fake.doorbell_target_valid;
}

Doorbell* start_doorbell(const std::string& target)
{
    test_support::fake::State& fake = test_support::fake::state();
    fake.start_doorbell_targets.push_back(target);
    switch (fake.start_doorbell_mode)
    {
    case test_support::fake::StartDoorbellMode::Success:
        return &fake_doorbell_instance;
    case test_support::fake::StartDoorbellMode::Fail:
        return nullptr;
    case test_support::fake::StartDoorbellMode::ThrowBadAlloc:
        throw std::bad_alloc();
    }
    return nullptr;
}

void stop_doorbell(Doorbell* doorbell)
{
    test_support::fake::state().stopped_doorbells.push_back(doorbell);
}

bool wait_doorbell(Doorbell* doorbell, int timeout_ms)
{
    (void)doorbell;
    test_support::fake::State& fake = test_support::fake::state();
    fake.wait_doorbell_timeouts.push_back(timeout_ms);
    return fake.wait_result;
}

void notify_doorbell(const std::string& target)
{
    test_support::fake::state().notify_doorbell_targets.push_back(target);
}

int doorbell_native_fd(const Doorbell* doorbell)
{
    (void)doorbell;
    ++test_support::fake::state().native_fd_calls;
    return test_support::fake::state().native_fd;
}

void* doorbell_native_handle(const Doorbell* doorbell)
{
    (void)doorbell;
    ++test_support::fake::state().native_handle_calls;
    return static_cast<void*>(&test_support::fake::state().native_handle_token);
}

} // namespace detail
} // namespace singleproc
