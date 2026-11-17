/// @file
/// @brief Guard 公开接口的单元测试：平台端口全部替换为 tests/support 的假端口。
///
/// 本目标只编译 src/singleproc.cpp + fake_platform.cpp，不链接真实的
/// platform_posix.cpp / platform_win32.cpp；平台行为由 fake 编排。

#include "support/fake_platform.hpp"
#include "support/test_harness.hpp"

#include <singleproc/singleproc.hpp>

#include <cerrno>
#include <new>
#include <stdexcept>
#include <string>

namespace
{

/// @brief 平台端口替身的状态快捷访问。
test_support::fake::State& fake()
{
    return test_support::fake::state();
}

} // namespace

/// @brief 单元测试套件：对应源文件 src/singleproc.cpp。
class TestSingleproc
{
public:
    /// @brief 每个用例开始前重置假端口，避免用例间共享状态。
    static void init();

    /// @brief 每个用例结束后再次重置，清理记录。
    static void cleanup();

    // ctor/状态机

    /// @brief 合法 key：取得 Primary，并清理锁与门铃。
    static void guard_valid_key_returns_primary();
    /// @brief 空 key：EINVAL，且不触碰平台层。
    static void guard_empty_key_returns_einval();
    /// @brief key 含非法字符：EINVAL。
    static void guard_invalid_key_characters_returns_einval();
    /// @brief runtime_dir 含 NUL：EINVAL。
    static void guard_nul_runtime_dir_returns_einval();
    /// @brief 默认目录为空：ENOENT。
    static void guard_empty_default_runtime_dir_returns_enoent();
    /// @brief 锁路径过长：ENAMETOOLONG。
    static void guard_lock_path_too_long_returns_enametoolong();
    /// @brief 平台加锁失败：Error + 平台 errno。
    static void guard_lock_error_returns_platform_errno();
    /// @brief 锁已被占用：AlreadyRunning，并向主实例门铃发通知。
    static void guard_already_running_notifies_doorbell();
    /// @brief 门铃目标不可用：锁已占用时也不发通知。
    static void guard_already_running_without_doorbell_skips_notify();
    /// @brief 门铃目标不可用时 Primary 只保留锁，不创建门铃。
    static void guard_primary_without_doorbell_keeps_lock_only();
    /// @brief 门铃创建失败不影响 Primary 状态。
    static void guard_primary_doorbell_start_fails_keeps_primary();
    /// @brief 门铃创建抛 bad_alloc：先释放锁再向外传播。
    static void guard_primary_doorbell_start_throws_releases_lock();
    /// @brief 析构：停止门铃并释放锁。
    static void guard_destructor_stops_doorbell_and_releases_lock();

    // 门铃访问器

    /// @brief 有门铃时 doorbell_fd() 透传平台 fd。
    static void doorbell_fd_with_doorbell_returns_native_fd();
    /// @brief 有门铃时 doorbell_handle() 透传平台句柄。
    static void doorbell_handle_with_doorbell_returns_native_handle();
    /// @brief 无门铃时 doorbell_fd() 为 -1。
    static void doorbell_fd_without_doorbell_returns_minus_one();
    /// @brief 无门铃时 doorbell_handle() 为 nullptr。
    static void doorbell_handle_without_doorbell_returns_nullptr();

    // 门铃分发

    /// @brief 无门铃时 process_doorbell() 直接返回 false。
    static void process_doorbell_without_doorbell_returns_false();
    /// @brief 无通知时 process_doorbell() 返回 false，并把超时传给平台。
    static void process_doorbell_no_notification_returns_false();
    /// @brief 有通知时执行 handler 并返回 true。
    static void process_doorbell_with_notification_calls_handler();
    /// @brief handler 为空时保留待处理通知，设置后再分发。
    static void process_doorbell_without_handler_keeps_pending();
    /// @brief 清空 handler 后通知保留，重新设置后分发。
    static void set_doorbell_handler_clear_then_set_keeps_pending();
    /// @brief handler 抛异常时向外传播，通知不重放。
    static void process_doorbell_handler_throws_does_not_replay();
};

void TestSingleproc::init()
{
    test_support::fake::reset();
}

void TestSingleproc::cleanup()
{
    test_support::fake::reset();
}

void TestSingleproc::guard_valid_key_returns_primary()
{
    {
        singleproc::Guard guard("App.v1_x-2", "/run/dir");

        CHECK_EQ(static_cast<int>(guard.state()), static_cast<int>(singleproc::Guard::State::Primary));
        CHECK(guard.is_primary());
        CHECK_EQ(guard.error_code(), 0);
        CHECK_EQ(guard.doorbell_fd(), fake().native_fd);

        CHECK_EQ(fake().acquire_lock_paths.size(), 1U);
        CHECK_EQ(fake().acquire_lock_paths.at(0), std::string("/run/dir/singleproc-1000-App.v1_x-2.lock"));
        CHECK_EQ(fake().start_doorbell_targets.size(), 1U);
        CHECK_EQ(fake().start_doorbell_targets.at(0), std::string("fake-doorbell:singleproc-1000-App.v1_x-2"));
        CHECK_EQ(fake().make_doorbell_target_calls, 1);
        CHECK_EQ(fake().last_doorbell_runtime_dir, std::string("/run/dir"));
        CHECK_EQ(fake().last_doorbell_runtime_name, std::string("singleproc-1000-App.v1_x-2"));
    }

    CHECK_EQ(fake().stopped_doorbells.size(), 1U);
    CHECK_EQ(fake().released_handles.size(), 1U);
    CHECK_EQ(fake().released_handles.at(0), fake().lock_handle);
}

void TestSingleproc::guard_empty_key_returns_einval()
{
    singleproc::Guard guard(std::string(), "/run/dir");

    CHECK_EQ(static_cast<int>(guard.state()), static_cast<int>(singleproc::Guard::State::Error));
    CHECK(!guard.is_primary());
    CHECK_EQ(guard.error_code(), EINVAL);
    CHECK(fake().acquire_lock_paths.empty());
}

void TestSingleproc::guard_invalid_key_characters_returns_einval()
{
    singleproc::Guard guard("app name", "/run/dir");

    CHECK_EQ(static_cast<int>(guard.state()), static_cast<int>(singleproc::Guard::State::Error));
    CHECK_EQ(guard.error_code(), EINVAL);
    CHECK(fake().acquire_lock_paths.empty());
}

void TestSingleproc::guard_nul_runtime_dir_returns_einval()
{
    singleproc::Guard guard("app", std::string("bad\0dir", 7));

    CHECK_EQ(static_cast<int>(guard.state()), static_cast<int>(singleproc::Guard::State::Error));
    CHECK_EQ(guard.error_code(), EINVAL);
    CHECK(fake().acquire_lock_paths.empty());
}

void TestSingleproc::guard_empty_default_runtime_dir_returns_enoent()
{
    fake().default_runtime_dir.clear();

    singleproc::Guard guard("app");

    CHECK_EQ(static_cast<int>(guard.state()), static_cast<int>(singleproc::Guard::State::Error));
    CHECK_EQ(guard.error_code(), ENOENT);
    CHECK_EQ(fake().default_runtime_dir_calls, 1);
    CHECK(fake().acquire_lock_paths.empty());
}

void TestSingleproc::guard_lock_path_too_long_returns_enametoolong()
{
    fake().default_runtime_dir = std::string(5000, 'a');

    singleproc::Guard guard("app");

    CHECK_EQ(static_cast<int>(guard.state()), static_cast<int>(singleproc::Guard::State::Error));
    CHECK_EQ(guard.error_code(), ENAMETOOLONG);
    CHECK(fake().acquire_lock_paths.empty());
}

void TestSingleproc::guard_lock_error_returns_platform_errno()
{
    fake().lock_result = singleproc::detail::LockResult::Error;
    fake().lock_error_code = EACCES;

    singleproc::Guard guard("app", "/run/dir");

    CHECK_EQ(static_cast<int>(guard.state()), static_cast<int>(singleproc::Guard::State::Error));
    CHECK_EQ(guard.error_code(), EACCES);
    CHECK_EQ(fake().acquire_lock_paths.size(), 1U);
    CHECK(fake().released_handles.empty());
}

void TestSingleproc::guard_already_running_notifies_doorbell()
{
    fake().lock_result = singleproc::detail::LockResult::AlreadyRunning;

    singleproc::Guard guard("app", "/run/dir");

    CHECK_EQ(static_cast<int>(guard.state()), static_cast<int>(singleproc::Guard::State::AlreadyRunning));
    CHECK(!guard.is_primary());
    CHECK_EQ(guard.error_code(), 0);
    CHECK_EQ(fake().notify_doorbell_targets.size(), 1U);
    CHECK_EQ(fake().notify_doorbell_targets.at(0), std::string("fake-doorbell:singleproc-1000-app"));
    CHECK(fake().start_doorbell_targets.empty());
}

void TestSingleproc::guard_already_running_without_doorbell_skips_notify()
{
    fake().lock_result = singleproc::detail::LockResult::AlreadyRunning;
    fake().doorbell_target_valid = false;

    singleproc::Guard guard("app", "/run/dir");

    CHECK_EQ(static_cast<int>(guard.state()), static_cast<int>(singleproc::Guard::State::AlreadyRunning));
    CHECK(fake().notify_doorbell_targets.empty());
    CHECK_EQ(fake().make_doorbell_target_calls, 1);
    CHECK_EQ(fake().checked_doorbell_targets.size(), 1U);
}

void TestSingleproc::guard_primary_without_doorbell_keeps_lock_only()
{
    fake().doorbell_target_valid = false;

    singleproc::Guard guard("app", "/run/dir");

    CHECK_EQ(static_cast<int>(guard.state()), static_cast<int>(singleproc::Guard::State::Primary));
    CHECK(fake().start_doorbell_targets.empty());
    CHECK_EQ(guard.doorbell_fd(), -1);
    CHECK(guard.doorbell_handle() == nullptr);
}

void TestSingleproc::guard_primary_doorbell_start_fails_keeps_primary()
{
    fake().start_doorbell_mode = test_support::fake::StartDoorbellMode::Fail;

    {
        singleproc::Guard guard("app", "/run/dir");

        CHECK_EQ(static_cast<int>(guard.state()), static_cast<int>(singleproc::Guard::State::Primary));
        CHECK(guard.is_primary());
        CHECK_EQ(guard.doorbell_fd(), -1);
        CHECK(guard.doorbell_handle() == nullptr);
        CHECK_EQ(fake().start_doorbell_targets.size(), 1U);
    }

    CHECK(fake().stopped_doorbells.empty());
    CHECK_EQ(fake().released_handles.size(), 1U);
}

void TestSingleproc::guard_primary_doorbell_start_throws_releases_lock()
{
    fake().start_doorbell_mode = test_support::fake::StartDoorbellMode::ThrowBadAlloc;

    CHECK_THROWS(singleproc::Guard("app", "/run/dir"), std::bad_alloc);

    CHECK_EQ(fake().released_handles.size(), 1U);
    CHECK_EQ(fake().released_handles.at(0), fake().lock_handle);
    CHECK(fake().stopped_doorbells.empty());
}

void TestSingleproc::guard_destructor_stops_doorbell_and_releases_lock()
{
    {
        singleproc::Guard guard("app", "/run/dir");
        CHECK_EQ(fake().stopped_doorbells.size(), 0U);
        CHECK_EQ(fake().released_handles.size(), 0U);
    }

    CHECK_EQ(fake().stopped_doorbells.size(), 1U);
    CHECK_EQ(fake().released_handles.size(), 1U);
}

void TestSingleproc::doorbell_fd_with_doorbell_returns_native_fd()
{
    fake().native_fd = 1234;

    singleproc::Guard guard("app", "/run/dir");

    CHECK_EQ(guard.doorbell_fd(), 1234);
    CHECK_EQ(fake().native_fd_calls, 1);
}

void TestSingleproc::doorbell_handle_with_doorbell_returns_native_handle()
{
    singleproc::Guard guard("app", "/run/dir");

    CHECK(guard.doorbell_handle() == static_cast<void*>(&fake().native_handle_token));
    CHECK_EQ(fake().native_handle_calls, 1);
}

void TestSingleproc::doorbell_fd_without_doorbell_returns_minus_one()
{
    fake().doorbell_target_valid = false;

    singleproc::Guard guard("app", "/run/dir");

    CHECK_EQ(guard.doorbell_fd(), -1);
    CHECK_EQ(fake().native_fd_calls, 0);
}

void TestSingleproc::doorbell_handle_without_doorbell_returns_nullptr()
{
    fake().doorbell_target_valid = false;

    singleproc::Guard guard("app", "/run/dir");

    CHECK(guard.doorbell_handle() == nullptr);
    CHECK_EQ(fake().native_handle_calls, 0);
}

void TestSingleproc::process_doorbell_without_doorbell_returns_false()
{
    fake().doorbell_target_valid = false;

    singleproc::Guard guard("app", "/run/dir");

    CHECK(!guard.process_doorbell(0));
    CHECK(fake().wait_doorbell_timeouts.empty());
}

void TestSingleproc::process_doorbell_no_notification_returns_false()
{
    singleproc::Guard guard("app", "/run/dir");

    CHECK(!guard.process_doorbell(5));
    CHECK_EQ(fake().wait_doorbell_timeouts.size(), 1U);
    CHECK_EQ(fake().wait_doorbell_timeouts.at(0), 5);
}

void TestSingleproc::process_doorbell_with_notification_calls_handler()
{
    int handler_calls = 0;
    singleproc::Guard guard("app", "/run/dir");
    fake().wait_result = true;
    guard.set_doorbell_handler(
        [&handler_calls]
        {
            ++handler_calls;
        });

    CHECK(guard.process_doorbell(0));
    CHECK_EQ(handler_calls, 1);
    CHECK_EQ(fake().wait_doorbell_timeouts.size(), 1U);

    fake().wait_result = false;
    CHECK(!guard.process_doorbell(0));
    CHECK_EQ(handler_calls, 1);
}

void TestSingleproc::process_doorbell_without_handler_keeps_pending()
{
    int handler_calls = 0;
    singleproc::Guard guard("app", "/run/dir");
    fake().wait_result = true;

    CHECK(!guard.process_doorbell(0));
    CHECK_EQ(fake().wait_doorbell_timeouts.size(), 1U);

    fake().wait_result = false;
    guard.set_doorbell_handler(
        [&handler_calls]
        {
            ++handler_calls;
        });

    CHECK(guard.process_doorbell(0));
    CHECK_EQ(handler_calls, 1);
    CHECK_EQ(fake().wait_doorbell_timeouts.size(), 1U);
}

void TestSingleproc::set_doorbell_handler_clear_then_set_keeps_pending()
{
    int handler_calls = 0;
    singleproc::Guard guard("app", "/run/dir");
    fake().wait_result = true;
    guard.set_doorbell_handler(
        [&handler_calls]
        {
            ++handler_calls;
        });
    guard.set_doorbell_handler({});

    CHECK(!guard.process_doorbell(0));
    CHECK_EQ(fake().wait_doorbell_timeouts.size(), 1U);

    fake().wait_result = false;
    guard.set_doorbell_handler(
        [&handler_calls]
        {
            ++handler_calls;
        });

    CHECK(guard.process_doorbell(0));
    CHECK_EQ(handler_calls, 1);
}

void TestSingleproc::process_doorbell_handler_throws_does_not_replay()
{
    singleproc::Guard guard("app", "/run/dir");
    fake().wait_result = true;
    guard.set_doorbell_handler(
        []
        {
            throw std::runtime_error("handler failed");
        });

    CHECK_THROWS(guard.process_doorbell(0), std::runtime_error);

    fake().wait_result = false;
    CHECK(!guard.process_doorbell(0));
    CHECK_EQ(fake().wait_doorbell_timeouts.size(), 2U);
}

int main()
{
    return test_harness::run<TestSingleproc>({
        {"guard_valid_key_returns_primary", &TestSingleproc::guard_valid_key_returns_primary},
        {"guard_empty_key_returns_einval", &TestSingleproc::guard_empty_key_returns_einval},
        {"guard_invalid_key_characters_returns_einval", &TestSingleproc::guard_invalid_key_characters_returns_einval},
        {"guard_nul_runtime_dir_returns_einval", &TestSingleproc::guard_nul_runtime_dir_returns_einval},
        {
            "guard_empty_default_runtime_dir_returns_enoent",
            &TestSingleproc::guard_empty_default_runtime_dir_returns_enoent,
        },
        {
            "guard_lock_path_too_long_returns_enametoolong",
            &TestSingleproc::guard_lock_path_too_long_returns_enametoolong,
        },
        {"guard_lock_error_returns_platform_errno", &TestSingleproc::guard_lock_error_returns_platform_errno},
        {"guard_already_running_notifies_doorbell", &TestSingleproc::guard_already_running_notifies_doorbell},
        {
            "guard_already_running_without_doorbell_skips_notify",
            &TestSingleproc::guard_already_running_without_doorbell_skips_notify,
        },
        {
            "guard_primary_without_doorbell_keeps_lock_only",
            &TestSingleproc::guard_primary_without_doorbell_keeps_lock_only,
        },
        {
            "guard_primary_doorbell_start_fails_keeps_primary",
            &TestSingleproc::guard_primary_doorbell_start_fails_keeps_primary,
        },
        {
            "guard_primary_doorbell_start_throws_releases_lock",
            &TestSingleproc::guard_primary_doorbell_start_throws_releases_lock,
        },
        {
            "guard_destructor_stops_doorbell_and_releases_lock",
            &TestSingleproc::guard_destructor_stops_doorbell_and_releases_lock,
        },
        {"doorbell_fd_with_doorbell_returns_native_fd", &TestSingleproc::doorbell_fd_with_doorbell_returns_native_fd},
        {
            "doorbell_handle_with_doorbell_returns_native_handle",
            &TestSingleproc::doorbell_handle_with_doorbell_returns_native_handle,
        },
        {
            "doorbell_fd_without_doorbell_returns_minus_one",
            &TestSingleproc::doorbell_fd_without_doorbell_returns_minus_one,
        },
        {
            "doorbell_handle_without_doorbell_returns_nullptr",
            &TestSingleproc::doorbell_handle_without_doorbell_returns_nullptr,
        },
        {
            "process_doorbell_without_doorbell_returns_false",
            &TestSingleproc::process_doorbell_without_doorbell_returns_false,
        },
        {
            "process_doorbell_no_notification_returns_false",
            &TestSingleproc::process_doorbell_no_notification_returns_false,
        },
        {
            "process_doorbell_with_notification_calls_handler",
            &TestSingleproc::process_doorbell_with_notification_calls_handler,
        },
        {
            "process_doorbell_without_handler_keeps_pending",
            &TestSingleproc::process_doorbell_without_handler_keeps_pending,
        },
        {
            "set_doorbell_handler_clear_then_set_keeps_pending",
            &TestSingleproc::set_doorbell_handler_clear_then_set_keeps_pending,
        },
        {
            "process_doorbell_handler_throws_does_not_replay",
            &TestSingleproc::process_doorbell_handler_throws_does_not_replay,
        },
    });
}
