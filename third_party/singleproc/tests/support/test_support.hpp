// 本文件只在 POSIX 构建中编译。整文件加平台守卫，Windows 门禁里的 clang-tidy
// 会把它当成空翻译单元，不会因为找不到 POSIX 头文件误报。
#ifndef _WIN32

#ifndef SINGLEPROC_TEST_SUPPORT_HPP
#define SINGLEPROC_TEST_SUPPORT_HPP

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

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

#endif // !defined(_WIN32)
