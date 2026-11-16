#include <stacklog/stacklog.hpp>
#include "test_support.hpp"

int main()
{
    const std::string dir = test_temp_dir();
    STACKLOG_TEST_CHECK(!dir.empty());

    stacklog::config cfg;
    cfg.dir = dir;
    cfg.base_name = "trace";
    cfg.max_file_size = 1024ULL * 1024;
    cfg.max_files = 2;
    cfg.min_level = stacklog::level::debug;

    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    {
        STACKLOG_TRACE("root");
        STACKLOG_INFO() << "root-message";
        {
            STACKLOG_TRACE("child");
            STACKLOG_INFO() << "child-message";
        }
    }
    stacklog::shutdown();

    const std::vector<std::string> lines = test_read_lines(dir + "/trace.log");
    STACKLOG_TEST_CHECK(lines.size() == 2);

    std::vector<std::string> root;
    std::vector<std::string> child;
    for (const auto& line : lines)
    {
        if (line.find("root-message") != std::string::npos)
        {
            root = test_log_fields(line);
        }
        if (line.find("child-message") != std::string::npos)
        {
            child = test_log_fields(line);
        }
    }

    STACKLOG_TEST_CHECK(root.size() == 8);
    STACKLOG_TEST_CHECK(child.size() == 8);
    STACKLOG_TEST_CHECK(root[2] == child[2]);
    STACKLOG_TEST_CHECK(root[4] == "-");
    STACKLOG_TEST_CHECK(root[3] == child[4]);
    STACKLOG_TEST_CHECK(root[5] == "root");
    STACKLOG_TEST_CHECK(child[5] == "child");
    STACKLOG_TEST_CHECK(child[3] != child[4]);

    test_cleanup(dir);

    // 三层父子关系、多个根、作用域结束和空名称。
    const std::string deep_dir = test_temp_dir();
    STACKLOG_TEST_CHECK(!deep_dir.empty());
    cfg.dir = deep_dir;
    cfg.base_name = "deep";
    STACKLOG_TEST_CHECK(stacklog::init(cfg));
    {
        STACKLOG_TRACE("deep-root");
        STACKLOG_INFO() << "deep-root-message";
        {
            STACKLOG_TRACE("deep-child");
            STACKLOG_INFO() << "deep-child-message";
            {
                STACKLOG_TRACE("deep-grand");
                STACKLOG_INFO() << "deep-grand-message";
            }
        }
    }
    {
        STACKLOG_TRACE("deep-root2");
        STACKLOG_INFO() << "deep-root2-message";
    }
    STACKLOG_INFO() << "deep-no-trace";
    {
        STACKLOG_TRACE("");
        STACKLOG_INFO() << "deep-unnamed";
    }
    stacklog::shutdown();

    const std::vector<std::string> deep_lines = test_read_lines(deep_dir + "/deep.log");
    STACKLOG_TEST_CHECK(deep_lines.size() == 6);
    const std::vector<std::string> deep_root = test_find_line(deep_lines, "deep-root-message");
    const std::vector<std::string> deep_child = test_find_line(deep_lines, "deep-child-message");
    const std::vector<std::string> deep_grand = test_find_line(deep_lines, "deep-grand-message");
    const std::vector<std::string> deep_root2 = test_find_line(deep_lines, "deep-root2-message");
    const std::vector<std::string> deep_none = test_find_line(deep_lines, "deep-no-trace");
    const std::vector<std::string> deep_unnamed = test_find_line(deep_lines, "deep-unnamed");

    STACKLOG_TEST_CHECK(deep_root.size() == 8);
    STACKLOG_TEST_CHECK(deep_child.size() == 8);
    STACKLOG_TEST_CHECK(deep_grand.size() == 8);
    STACKLOG_TEST_CHECK(deep_root2.size() == 8);
    STACKLOG_TEST_CHECK(deep_none.size() == 8);
    STACKLOG_TEST_CHECK(deep_unnamed.size() == 8);

    STACKLOG_TEST_CHECK(deep_root[4] == "-");
    STACKLOG_TEST_CHECK(deep_root[2] == deep_child[2]);
    STACKLOG_TEST_CHECK(deep_root[2] == deep_grand[2]);
    STACKLOG_TEST_CHECK(deep_child[4] == deep_root[3]);
    STACKLOG_TEST_CHECK(deep_grand[4] == deep_child[3]);
    STACKLOG_TEST_CHECK(deep_root[3] != deep_child[3]);
    STACKLOG_TEST_CHECK(deep_child[3] != deep_grand[3]);

    STACKLOG_TEST_CHECK(deep_root2[4] == "-");
    STACKLOG_TEST_CHECK(deep_root2[2] != deep_root[2]);
    STACKLOG_TEST_CHECK(deep_none[2] == "-");
    STACKLOG_TEST_CHECK(deep_none[3] == "-");
    STACKLOG_TEST_CHECK(deep_none[4] == "-");
    STACKLOG_TEST_CHECK(deep_none[5] == "-");
    STACKLOG_TEST_CHECK(deep_unnamed[5] == "unnamed");
    test_cleanup(deep_dir);

    return 0;
}
