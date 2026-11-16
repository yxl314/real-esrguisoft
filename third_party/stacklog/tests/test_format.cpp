#include <stacklog/stacklog.hpp>
#include "test_support.hpp"

int main()
{
    const std::string dir = test_temp_dir();
    STACKLOG_TEST_CHECK(!dir.empty());

    stacklog::config cfg;
    cfg.dir = dir;
    cfg.base_name = "format";
    cfg.max_file_size = 1024ULL * 1024;
    cfg.max_files = 2;
    cfg.min_level = stacklog::level::debug;

    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    {
        STACKLOG_INFO() << "hello|world";
        STACKLOG_WARN() << "line1\nline2";
    }
    stacklog::shutdown();

    const std::vector<std::string> lines = test_read_lines(dir + "/format.log");
    STACKLOG_TEST_CHECK(lines.size() == 2);
    STACKLOG_TEST_CHECK(lines[0].find("|INFO|") != std::string::npos);
    STACKLOG_TEST_CHECK(lines[0].find("hello\\|world") != std::string::npos);
    STACKLOG_TEST_CHECK(lines[1].find("|WARN|") != std::string::npos);
    STACKLOG_TEST_CHECK(lines[1].find("line1\\nline2") != std::string::npos);
    STACKLOG_TEST_CHECK(lines[0].find("|-|-|-|-|") != std::string::npos);

    test_cleanup(dir);

    const std::string level_dir = test_temp_dir();
    STACKLOG_TEST_CHECK(!level_dir.empty());
    cfg.dir = level_dir;
    cfg.base_name = "level";
    cfg.min_level = stacklog::level::warn;
    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    STACKLOG_INFO() << "hidden-info";
    STACKLOG_WARN() << "visible-warn";
    stacklog::shutdown();
    const std::vector<std::string> level_lines = test_read_lines(level_dir + "/level.log");
    STACKLOG_TEST_CHECK(level_lines.size() == 1);
    STACKLOG_TEST_CHECK(level_lines[0].find("|WARN|") != std::string::npos);
    STACKLOG_TEST_CHECK(level_lines[0].find("hidden-info") == std::string::npos);
    test_cleanup(level_dir);

    // 转义、空消息和流操纵符都必须保持一行一条记录。
    const std::string edge_dir = test_temp_dir();
    STACKLOG_TEST_CHECK(!edge_dir.empty());
    cfg.dir = edge_dir;
    cfg.base_name = "edge";
    cfg.min_level = stacklog::level::debug;
    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    STACKLOG_INFO() << "escape\\pipe|cr\rlf\nend";
    STACKLOG_INFO() << "";
    STACKLOG_INFO() << "endl-line" << std::endl; // NOLINT(performance-avoid-endl): 验证流操纵符支持
    stacklog::shutdown();

    const std::vector<std::string> edge_lines = test_read_lines(edge_dir + "/edge.log");
    STACKLOG_TEST_CHECK(edge_lines.size() == 3);
    const std::vector<std::string> escape_fields = test_log_fields(edge_lines[0]);
    STACKLOG_TEST_CHECK(escape_fields.size() == 8);
    STACKLOG_TEST_CHECK(escape_fields[7] == "escape\\\\pipe\\|cr\\rlf\\nend");
    const std::vector<std::string> empty_fields = test_log_fields(edge_lines[1]);
    STACKLOG_TEST_CHECK(empty_fields.size() == 8);
    STACKLOG_TEST_CHECK(empty_fields[7] == "-");
    const std::vector<std::string> endl_fields = test_log_fields(edge_lines[2]);
    STACKLOG_TEST_CHECK(endl_fields.size() == 8);
    STACKLOG_TEST_CHECK(endl_fields[7] == "endl-line\\n");
    test_cleanup(edge_dir);

    // 运行期级别切换和 Off 必须立即生效。
    const std::string dynamic_dir = test_temp_dir();
    STACKLOG_TEST_CHECK(!dynamic_dir.empty());
    cfg.dir = dynamic_dir;
    cfg.base_name = "dynamic";
    cfg.min_level = stacklog::level::info;
    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    STACKLOG_DEBUG() << "hidden-debug";
    STACKLOG_INFO() << "visible-info";
    stacklog::set_level(stacklog::level::debug);
    STACKLOG_DEBUG() << "visible-debug";
    stacklog::set_level(stacklog::level::off);
    STACKLOG_ERROR() << "hidden-off";
    stacklog::set_level(stacklog::level::info);
    STACKLOG_INFO() << "visible-again";
    stacklog::shutdown();

    const std::vector<std::string> dynamic_lines = test_read_lines(dynamic_dir + "/dynamic.log");
    STACKLOG_TEST_CHECK(dynamic_lines.size() == 3);
    STACKLOG_TEST_CHECK(!test_find_line(dynamic_lines, "visible-info").empty());
    STACKLOG_TEST_CHECK(!test_find_line(dynamic_lines, "visible-debug").empty());
    STACKLOG_TEST_CHECK(!test_find_line(dynamic_lines, "visible-again").empty());
    STACKLOG_TEST_CHECK(test_find_line(dynamic_lines, "hidden-debug").empty());
    STACKLOG_TEST_CHECK(test_find_line(dynamic_lines, "hidden-off").empty());
    test_cleanup(dynamic_dir);

    return 0;
}
