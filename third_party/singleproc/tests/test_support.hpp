#ifndef SINGLEPROC_TEST_SUPPORT_HPP
#define SINGLEPROC_TEST_SUPPORT_HPP

#include <cstdio>
#include <cstdlib>
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

namespace test_support
{

inline int& failure_count()
{
    static int count = 0;
    return count;
}

inline void check(bool condition, const char* expression, const char* file, int line)
{
    if (!condition)
    {
        (void)std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
        ++failure_count();
    }
}

#ifdef _WIN32

inline std::wstring to_wide(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }
    const int size = static_cast<int>(value.size());
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, value.data(), size, nullptr, 0);
    if (length <= 0)
    {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    (void)::MultiByteToWideChar(CP_UTF8, 0, value.data(), size, &result[0], length);
    return result;
}

inline std::string to_utf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }
    const int size = static_cast<int>(value.size());
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, value.data(), size, nullptr, 0, nullptr, nullptr);
    if (length <= 0)
    {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    (void)::WideCharToMultiByte(CP_UTF8, 0, value.data(), size, &result[0], length, nullptr, nullptr);
    return result;
}

inline std::string make_temp_dir()
{
    // 测试目录放在当前工作目录下，而不是 %TEMP%：受限环境（沙箱、只读
    // 临时目录）里系统临时目录可能不可写，而构建目录始终可写。
    wchar_t buffer[MAX_PATH + 1];
    const DWORD length = ::GetCurrentDirectoryW(static_cast<DWORD>(MAX_PATH + 1), buffer);
    if (length == 0 || length > MAX_PATH)
    {
        return {};
    }
    const std::wstring base(buffer, length);
    const DWORD process_id = ::GetCurrentProcessId();
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        const std::wstring candidate =
            base + L"\\singleproc-test-" + std::to_wstring(process_id) + L"-" + std::to_wstring(attempt);
        const BOOL created = ::CreateDirectoryW(candidate.c_str(), nullptr);
        const DWORD create_error = ::GetLastError();
        if (created)
        {
            return to_utf8(candidate);
        }
        if (create_error != ERROR_ALREADY_EXISTS)
        {
            return {};
        }
    }
    return {};
}

inline void remove_file(const std::string& path)
{
    const std::wstring wide = to_wide(path);
    if (!wide.empty())
    {
        (void)::DeleteFileW(wide.c_str());
    }
}

/// 删除目录内的普通文件后删除目录本身；命名管道与密码学对象由系统管理。
inline void remove_directory(const std::string& directory)
{
    const std::wstring wide_directory = to_wide(directory);
    if (wide_directory.empty())
    {
        return;
    }
    WIN32_FIND_DATAW entry = {};
    const std::wstring pattern = wide_directory + L"\\*";
    HANDLE finder = ::FindFirstFileW(pattern.c_str(), &entry);
    if (finder != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (entry.cFileName[0] == L'.')
            {
                continue;
            }
            const std::wstring path = wide_directory + L"\\" + entry.cFileName;
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                remove_directory(to_utf8(path));
            }
            else
            {
                (void)::DeleteFileW(path.c_str());
            }
        } while (::FindNextFileW(finder, &entry));
        (void)::FindClose(finder);
    }
    (void)::RemoveDirectoryW(wide_directory.c_str());
}

inline void cleanup(const std::string& directory, const std::string& key)
{
    (void)directory;
    (void)key;
}

#else

inline std::string make_temp_dir()
{
    char pattern[] = "/tmp/singleproc-test-XXXXXX";
    char* path = ::mkdtemp(pattern);
    if (path == nullptr)
    {
        return {};
    }
    return {path};
}

inline std::string lock_path(const std::string& directory, const std::string& key)
{
    return directory + "/singleproc-" + std::to_string(static_cast<unsigned long>(::getuid())) + "-" + key + ".lock";
}

inline std::string socket_path(const std::string& directory, const std::string& key)
{
    return directory + "/singleproc-" + std::to_string(static_cast<unsigned long>(::getuid())) + "-" + key + ".sock";
}

inline void remove_file(const std::string& path)
{
    (void)::unlink(path.c_str());
}

inline void cleanup(const std::string& directory, const std::string& key)
{
    remove_file(lock_path(directory, key));
    remove_file(socket_path(directory, key));
}

inline void remove_directory(const std::string& directory)
{
    (void)::rmdir(directory.c_str());
}

#endif

inline int finish(const char* name)
{
    if (failure_count() != 0)
    {
        (void)std::fprintf(stderr, "%d checks failed\n", failure_count());
        return 1;
    }
    (void)std::printf("%s passed\n", name);
    return 0;
}

} // namespace test_support

#define CHECK(condition) ::test_support::check((condition), #condition, __FILE__, __LINE__)

#endif // SINGLEPROC_TEST_SUPPORT_HPP
