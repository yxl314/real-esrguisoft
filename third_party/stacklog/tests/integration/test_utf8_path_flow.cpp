#include <stacklog/stacklog.hpp>
#include "../support/test_harness.hpp"
#include "../support/test_support.hpp"

#include <string>

#ifdef _WIN32
#include <io.h>
#endif

namespace
{

// 用 u8 字面量固定为 UTF-8 字节，避免 Windows 编译器按本地代码页转码。
const char* const chinese_dir = u8"\u4e2d\u6587\u76ee\u5f55"; // 中文目录
const char* const chinese_nested = u8"\u65e5\u5fd7"; // 日志
const char* const chinese_base = u8"\u5e94\u7528"; // 应用
const char* const chinese_message = u8"\u4f60\u597d\uff0c\u65e5\u5fd7"; // 你好，日志

#ifdef _WIN32
/// 把 stderr 重定向到 path；返回保存的原始文件描述符，失败返回 -1。
int capture_stderr_begin(const std::string& path)
{
    const std::wstring wide = test_wide_path(path);
    if (wide.empty())
    {
        return -1;
    }
    const int saved = ::_dup(2);
    if (saved < 0)
    {
        return -1;
    }
    std::FILE* file = nullptr;
    if (0 != ::_wfreopen_s(&file, wide.c_str(), L"w", stderr))
    {
        (void)::_close(saved);
        return -1;
    }
    return saved;
}

/// 恢复 stderr；之后可以读取捕获文件。
void capture_stderr_end(int saved)
{
    (void)std::fflush(stderr);
    if (saved >= 0)
    {
        (void)::_dup2(saved, 2);
        (void)::_close(saved);
    }
}

/// 把一个空闲盘符映射到 dir（subst 的编程版，不需要管理员权限）；成功返回盘符字母。
char map_drive(const std::string& dir)
{
    const std::wstring target = L"\\??\\" + test_wide_path(dir);
    const DWORD mask = ::GetLogicalDrives();
    for (char letter = 'Z'; letter >= 'D'; --letter)
    {
        if (0 != (mask & (1U << static_cast<unsigned>(letter - 'A'))))
        {
            continue;
        }
        const std::wstring drive = std::wstring(1, static_cast<wchar_t>(letter)) + L":";
        if (0 != ::DefineDosDeviceW(DDD_RAW_TARGET_PATH, drive.c_str(), target.c_str()))
        {
            return letter;
        }
    }
    return 0;
}

/// 解除 map_drive 建立的盘符映射。
void unmap_drive(char letter, const std::string& dir)
{
    const std::wstring target = L"\\??\\" + test_wide_path(dir);
    const std::wstring drive = std::wstring(1, static_cast<wchar_t>(letter)) + L":";
    (void)::DefineDosDeviceW(DDD_REMOVE_DEFINITION, drive.c_str(), target.c_str());
}
#endif

} // namespace

class TestUtf8PathFlow // NOLINT(readability-identifier-naming): 测试策略要求类名 Test<被测单元或流程>
{
public:
    static void utf8_path_flow();
};

void TestUtf8PathFlow::utf8_path_flow()
{
    const std::string root = test_temp_dir();
    STACKLOG_TEST_CHECK(!root.empty());

    // 目录逐级创建也要经过 UTF-8 路径：Windows 上需要转宽字符 API。
    const std::string dir = root + "/" + chinese_dir + "/" + chinese_nested;

    stacklog::config cfg;
    cfg.dir = dir;
    cfg.base_name = chinese_base;
    cfg.max_file_size = 512;
    cfg.max_files = 3;
    cfg.min_level = stacklog::level::info;

    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    for (int index = 0; index < 60; ++index)
    {
        STACKLOG_INFO() << chinese_message << "-" << index;
    }
    stacklog::shutdown();

    const std::string log_path = dir + "/" + chinese_base + ".log";
    STACKLOG_TEST_CHECK(test_is_regular_file(log_path));
    const std::vector<std::string> lines = test_read_lines(log_path);
    STACKLOG_TEST_CHECK(!lines.empty());
    const std::vector<std::string> last = test_log_fields(lines.back());
    STACKLOG_TEST_CHECK(last.size() == 8);
    STACKLOG_TEST_CHECK(last[7].find(chinese_message) != std::string::npos);

    // 轮转需要删除旧归档并重命名；多级轮转才能覆盖“删除最旧归档”的宽字符路径。
    bool archived_has_message = false;
    for (unsigned archive = 1; archive <= cfg.max_files; ++archive)
    {
        const std::string archive_path = log_path + "." + std::to_string(archive);
        STACKLOG_TEST_CHECK(test_is_regular_file(archive_path));
        const std::vector<std::string> archived = test_read_lines(archive_path);
        STACKLOG_TEST_CHECK(!archived.empty());
        archived_has_message = archived_has_message || !test_find_line(archived, chinese_message).empty();
    }
    STACKLOG_TEST_CHECK(archived_has_message);
    STACKLOG_TEST_CHECK(!test_is_regular_file(log_path + "." + std::to_string(cfg.max_files + 1)));

    // 末尾带分隔符的目录也要能正常初始化并继续写同一个文件。
    cfg.dir = dir + "/";
    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    STACKLOG_INFO() << chinese_message << "-trailing-separator";
    stacklog::shutdown();
    STACKLOG_TEST_CHECK(!test_find_line(test_read_lines(log_path), "-trailing-separator").empty());

#ifdef _WIN32
    // Windows 窄字符 API 只能按 ANSI 代码页解释路径；非法 UTF-8 必须干净地失败，
    // 不能把字节原样交给窄字符接口后悄悄写到错误位置。
    {
        const std::string invalid = std::string("\xff\x80", 2);

        cfg.dir = dir + "/" + invalid;
        STACKLOG_TEST_CHECK(!stacklog::init(cfg));

        // base_name 非法时，init 必须在创建目录之前失败，不能留下空目录。
        const std::string invalid_base_dir = root + "/invalid-base";
        const std::string capture_path = root + "/stderr-capture.log";
        cfg.dir = invalid_base_dir;
        cfg.base_name = invalid;
        const int saved = capture_stderr_begin(capture_path);
        STACKLOG_TEST_CHECK(saved >= 0);
        const bool invalid_base_init = stacklog::init(cfg);
        STACKLOG_INFO() << "invalid-utf8-fallback";
        capture_stderr_end(saved);

        STACKLOG_TEST_CHECK(!invalid_base_init);
        STACKLOG_TEST_CHECK(!test_path_exists(invalid_base_dir));
        const std::vector<std::string> captured = test_read_lines(capture_path);
        STACKLOG_TEST_CHECK(!test_find_line(captured, "路径不是合法 UTF-8").empty());
        const std::vector<std::string> fallback_line = test_find_line(captured, "invalid-utf8-fallback");
        STACKLOG_TEST_CHECK(fallback_line.size() == 8);
    }

    // Windows 的 \\?\ 前缀不做 '/' 归一化，库内部拼接必须用反斜杠；整条扩展路径
    // 要能初始化、写入和轮转。
    {
        const std::string extended_dir = R"(\\?\)" + root + "\\ext\\nest";
        cfg.dir = extended_dir;
        cfg.base_name = "extended";
        cfg.max_file_size = 256;
        cfg.max_files = 2;
        STACKLOG_TEST_CHECK(stacklog::init(cfg));
        for (int index = 0; index < 20; ++index)
        {
            STACKLOG_INFO() << "extended-line-" << index;
        }
        stacklog::shutdown();
        const std::string extended_log = extended_dir + "\\extended.log";
        STACKLOG_TEST_CHECK(test_is_regular_file(extended_log));
        STACKLOG_TEST_CHECK(!test_find_line(test_read_lines(extended_log), "extended-line-19").empty());
        STACKLOG_TEST_CHECK(test_is_regular_file(extended_log + ".1"));
    }

    // 盘符根 "X:"、"X:\\"、"X:/" 必须被识别为已存在的根而直接成功（不能把 "X:"
    // 交给 _wmkdir 靠 EEXIST 兜底）。用一个临时映射的盘符指向测试目录来验证。
    // UNC 共享根（\\\\server\\share）无法在本机构造（建共享需要管理员权限）；代码推演
    // 结论是 make_dirs 只校验共享根存在、不做逐级 mkdir，等有真实共享时再补实例。
    {
        const char letter = map_drive(root);
        if (0 == letter)
        {
            (void)std::fprintf(stderr, "skip: 没有空闲盘符可用于盘符根用例\n");
        }
        else
        {
            const std::string drive = std::string(1, letter) + ":";
            cfg.base_name = "root-drive";
            const char* const suffixes[] = {"", "\\", "/"};
            for (const char* suffix : suffixes)
            {
                cfg.dir = drive + suffix;
                STACKLOG_TEST_CHECK(stacklog::init(cfg));
                STACKLOG_INFO() << "root-drive-line";
                stacklog::shutdown();
                const std::string root_log = drive + "\\root-drive.log";
                STACKLOG_TEST_CHECK(test_is_regular_file(root_log));
                STACKLOG_TEST_CHECK(test_remove_file(root_log));
            }
            unmap_drive(letter, root);
        }
    }
#endif

    test_cleanup(root);
}

STACKLOG_TEST_MAIN(TestUtf8PathFlow, STACKLOG_TEST_CASE(TestUtf8PathFlow, utf8_path_flow));
