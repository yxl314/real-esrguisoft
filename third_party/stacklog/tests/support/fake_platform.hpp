/// @file
/// @brief 平台端口的内存假实现；供单元测试编排成功/失败并观察文件内容。

#ifndef STACKLOG_TEST_FAKE_PLATFORM_HPP
#define STACKLOG_TEST_FAKE_PLATFORM_HPP

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fake_platform
{

/// @brief 假文件系统中的文件状态。
struct FakeFile // NOLINT(readability-identifier-naming): 测试策略要求替身命名 Fake<接口>
{
    bool exists = false;
    bool directory = false;
    std::uint64_t size = 0;
    std::string content;
};

/// @brief 端口调用记录。
struct FakeCall // NOLINT(readability-identifier-naming): 测试策略要求替身命名 Fake<接口>
{
    std::string name;
    std::string first;
    std::string second;
};

/// @brief 可编排的假平台；所有端口函数都读写单例。
class FakePlatform // NOLINT(readability-identifier-naming): 测试策略要求替身命名 Fake<接口>
{
public:
    /// @brief 进程内单例。
    static FakePlatform& instance();

    /// @brief 清空文件系统、调用记录与全部编排开关。
    void reset();

    // ---- 编排开关 ----
    /// @brief valid_utf8_path 的返回值。
    bool utf8_valid = true;
    /// @brief make_dirs 的返回值。
    bool make_dirs_ok = true;
    /// @brief 前 N 次 open_append 失败（0 表示不注入失败）。
    int open_fail_count = 0;
    /// @brief 前 N 次 write_line 失败（0 表示不注入失败）。
    int write_fail_count = 0;
    /// @brief 前 N 次 write_line 抛异常（0 表示不注入异常）。
    int throw_write_count = 0;
    /// @brief 前 N 次 remove_file 失败（0 表示不注入失败）。
    int remove_fail_count = 0;
    /// @brief 前 N 次 rename_file 失败（0 表示不注入失败）。
    int rename_fail_count = 0;
    /// @brief timestamp 的固定返回值。
    std::string timestamp_value = "2026-01-02 03:04:05.006";

    // ---- 可观察状态 ----
    /// @brief 假文件系统（键为路径）。
    std::map<std::string, FakeFile> files;
    /// @brief 端口调用记录。
    std::vector<FakeCall> calls;
    /// @brief open_append 调用次数。
    int open_calls = 0;
    /// @brief write_line 调用次数。
    int write_calls = 0;

    // ---- 观察辅助 ----
    /// @brief 路径是否存在于假文件系统。
    bool has_file(const std::string& path) const;
    /// @brief 读取假文件内容；不存在返回空串。
    std::string file_content(const std::string& path) const;
    /// @brief 某端口函数被调用的次数。
    int count_calls(const std::string& name) const;
    /// @brief 预置一个文件（模拟重启时已有日志）。
    void set_file(const std::string& path, std::uint64_t size, const std::string& content = std::string());
    /// @brief 清空全部调用的记录（保留编排状态与文件）。
    void clear_calls();
};

} // namespace fake_platform

#endif // STACKLOG_TEST_FAKE_PLATFORM_HPP
