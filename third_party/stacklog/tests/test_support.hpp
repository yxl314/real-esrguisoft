#ifndef STACKLOG_TEST_SUPPORT_HPP
#define STACKLOG_TEST_SUPPORT_HPP

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

/// 创建测试临时目录；失败返回空串。
inline std::string test_temp_dir()
{
    char pattern[] = "/tmp/stacklog-test-XXXXXX";
    char* path = mkdtemp(pattern);
    return (nullptr == path) ? std::string() : std::string(path);
}

/// 删除测试临时目录。
inline void test_cleanup(const std::string& dir)
{
    if (!dir.empty())
    {
        const std::string command = "rm -rf \"" + dir + "\"";
        (void)std::system(command.c_str()); // NOLINT(cert-env33-c): 测试临时目录清理
    }
}

/// 判断路径是否是普通文件。
inline bool test_is_regular_file(const std::string& path)
{
#ifdef _WIN32
    struct _stat info;
    return _stat(path.c_str(), &info) == 0 && (info.st_mode & _S_IFMT) == _S_IFREG;
#else
    struct stat info;
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
#endif
}

/// 读取文本文件的所有行。
inline std::vector<std::string> test_read_lines(const std::string& path)
{
    std::vector<std::string> lines;
    std::ifstream stream(path.c_str());
    std::string line;
    while (std::getline(stream, line))
    {
        lines.push_back(line);
    }
    return lines;
}

/// 按未转义的 `|` 拆分日志行，保留字段中的转义序列。
inline std::vector<std::string> test_log_fields(const std::string& line)
{
    std::vector<std::string> fields;
    std::string current;
    bool escaped = false;
    for (char ch : line)
    {
        if (escaped)
        {
            current += '\\';
            current += ch;
            escaped = false;
        }
        else if (ch == '\\')
        {
            escaped = true;
        }
        else if (ch == '|')
        {
            fields.push_back(current);
            current.clear();
        }
        else
        {
            current += ch;
        }
    }
    if (escaped)
    {
        current += '\\';
    }
    fields.push_back(current);
    return fields;
}

/// 查找包含指定文本的第一行，并按未转义的 `|` 拆成字段。
inline std::vector<std::string> test_find_line(const std::vector<std::string>& lines, const std::string& needle)
{
    for (const std::string& line : lines)
    {
        if (line.find(needle) != std::string::npos)
        {
            return test_log_fields(line);
        }
    }
    return {};
}

#define STACKLOG_TEST_CHECK(condition)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            (void)std::fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__);                  \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

#endif // STACKLOG_TEST_SUPPORT_HPP
