/// @file
/// @brief 冒烟：配置失败 → 恢复初始化 → 外部删除后重建 → 重复 shutdown。

#include "../support/test_harness.hpp"
#include "../support/test_support.hpp"

#include <stacklog/stacklog.hpp>
#include <string>

class TestRecoveryFlow // NOLINT(readability-identifier-naming):
    // 测试策略要求类名 Test<被测单元或流程>
{
public:
  static void recovery_flow();
};

void
TestRecoveryFlow::recovery_flow()
{
  const std::string root = test_temp_dir();
  STACKLOG_TEST_CHECK(!root.empty());

  const std::string dir = root + "/logs";
  const std::string base = dir + "/recovery";

  // 非法配置：空目录、空基础名、零上限都必须干净失败。
  stacklog::config invalid;
  invalid.dir.clear();
  STACKLOG_TEST_CHECK(!stacklog::init(invalid));
  invalid = stacklog::config();
  invalid.dir = dir;
  invalid.base_name.clear();
  STACKLOG_TEST_CHECK(!stacklog::init(invalid));
  invalid.base_name = "recovery";
  invalid.max_file_size = 0;
  STACKLOG_TEST_CHECK(!stacklog::init(invalid));
  invalid.max_file_size = 1024;
  invalid.max_files = 0;
  STACKLOG_TEST_CHECK(!stacklog::init(invalid));

  stacklog::config cfg;
  cfg.dir = dir;
  cfg.base_name = "recovery";
  cfg.max_file_size = 1024;
  cfg.max_files = 3;
  cfg.min_level = stacklog::level::info;

  STACKLOG_TEST_CHECK(stacklog::init(cfg));
  STACKLOG_INFO() << "before-delete";
  stacklog::shutdown();
  STACKLOG_TEST_CHECK(test_is_regular_file(base + ".log"));

  // 外部删除后重新初始化，必须能重建文件并继续写。
  STACKLOG_TEST_CHECK(std::remove((base + ".log").c_str()) == 0);
  STACKLOG_TEST_CHECK(!test_path_exists(base + ".log"));
  STACKLOG_TEST_CHECK(stacklog::init(cfg));
  STACKLOG_INFO() << "after-recreate";
  stacklog::shutdown();
  stacklog::shutdown();

  STACKLOG_TEST_CHECK(test_is_regular_file(base + ".log"));
  const std::vector<std::string> lines = test_read_lines(base + ".log");
  STACKLOG_TEST_CHECK(lines.size() == 1);
  STACKLOG_TEST_CHECK(lines[0].find("after-recreate") != std::string::npos);

  test_cleanup(root);
}

STACKLOG_TEST_MAIN(TestRecoveryFlow,
                   STACKLOG_TEST_CASE(TestRecoveryFlow, recovery_flow));
