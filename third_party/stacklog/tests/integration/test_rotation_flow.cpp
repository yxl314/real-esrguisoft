#include "../support/test_harness.hpp"
#include "../support/test_support.hpp"

#include <cstdlib>
#include <stacklog/stacklog.hpp>

class TestRotationFlow // NOLINT(readability-identifier-naming):
    // 测试策略要求类名 Test<被测单元或流程>
{
public:
  static void rotation_flow();
};

void
TestRotationFlow::rotation_flow()
{
  const std::string dir = test_temp_dir();
  STACKLOG_TEST_CHECK(!dir.empty());

  stacklog::config cfg;
  cfg.dir = dir;
  cfg.base_name = "rotate";
  cfg.max_file_size = 400;
  cfg.max_files = 2;
  cfg.min_level = stacklog::level::info;

  STACKLOG_TEST_CHECK(stacklog::init(cfg));
  for (int index = 0; index < 60; ++index)
    {
      STACKLOG_INFO() << "rotation-line-" << index;
    }
  stacklog::shutdown();

  const std::vector<std::string> current = test_read_lines(dir + "/rotate.log");
  const std::vector<std::string> first = test_read_lines(dir + "/rotate.log.1");
  const std::vector<std::string> second
      = test_read_lines(dir + "/rotate.log.2");
  STACKLOG_TEST_CHECK(!current.empty());
  STACKLOG_TEST_CHECK(!first.empty());
  STACKLOG_TEST_CHECK(!second.empty());
  STACKLOG_TEST_CHECK(test_read_lines(dir + "/rotate.log.3").empty());

  // max_files=1 时只保留一个历史文件。
  const std::string single_dir = test_temp_dir();
  STACKLOG_TEST_CHECK(!single_dir.empty());
  cfg.dir = single_dir;
  cfg.base_name = "single";
  cfg.max_file_size = 180;
  cfg.max_files = 1;
  STACKLOG_TEST_CHECK(stacklog::init(cfg));
  for (int index = 0; index < 30; ++index)
    {
      STACKLOG_INFO() << "single-line-" << index;
    }
  stacklog::shutdown();
  STACKLOG_TEST_CHECK(!test_read_lines(single_dir + "/single.log").empty());
  STACKLOG_TEST_CHECK(!test_read_lines(single_dir + "/single.log.1").empty());
  STACKLOG_TEST_CHECK(test_read_lines(single_dir + "/single.log.2").empty());
  test_cleanup(single_dir);

  // 单行超过上限时也必须完整落盘。
  const std::string large_dir = test_temp_dir();
  STACKLOG_TEST_CHECK(!large_dir.empty());
  cfg.dir = large_dir;
  cfg.base_name = "large";
  cfg.max_file_size = 40;
  cfg.max_files = 2;
  STACKLOG_TEST_CHECK(stacklog::init(cfg));
  STACKLOG_INFO() << "this-single-line-is-longer-than-the-limit";
  STACKLOG_INFO() << "after-large-line";
  stacklog::shutdown();
  const std::vector<std::string> large_lines
      = test_read_lines(large_dir + "/large.log");
  STACKLOG_TEST_CHECK(large_lines.size() == 1);
  STACKLOG_TEST_CHECK(large_lines[0].find("after-large-line")
                      != std::string::npos);
  test_cleanup(large_dir);

  // 多轮归档后所有记录都必须保留且只出现一次。
  const std::string preserve_dir = test_temp_dir();
  STACKLOG_TEST_CHECK(!preserve_dir.empty());
  cfg.dir = preserve_dir;
  cfg.base_name = "preserve";
  cfg.max_file_size = 600;
  cfg.max_files = 60;
  cfg.min_level = stacklog::level::info;
  STACKLOG_TEST_CHECK(stacklog::init(cfg));
  const int record_count = 200;
  for (int index = 0; index < record_count; ++index)
    {
      STACKLOG_INFO() << "preserve-" << index;
    }
  stacklog::shutdown();

  std::vector<int> seen(record_count, 0);
  for (unsigned archive = 0; archive <= cfg.max_files; ++archive)
    {
      std::string path = preserve_dir + "/preserve.log";
      if (archive > 0)
        {
          path += "." + std::to_string(archive);
        }
      const std::vector<std::string> lines = test_read_lines(path);
      for (const auto& line : lines)
        {
          const std::vector<std::string> fields = test_log_fields(line);
          STACKLOG_TEST_CHECK(fields.size() == 8);
          const std::string::size_type pos = line.find("preserve-");
          STACKLOG_TEST_CHECK(pos != std::string::npos);
          char* end = nullptr;
          const long value = std::strtol(line.c_str() + pos + 9, &end, 10);
          STACKLOG_TEST_CHECK(end != nullptr);
          STACKLOG_TEST_CHECK(*end == '\0');
          STACKLOG_TEST_CHECK(value >= 0);
          STACKLOG_TEST_CHECK(value < record_count);
          ++seen[static_cast<std::size_t>(value)];
        }
    }
  for (int index = 0; index < record_count; ++index)
    {
      STACKLOG_TEST_CHECK(seen[static_cast<std::size_t>(index)] == 1);
    }
  test_cleanup(preserve_dir);

  test_cleanup(dir);
}

STACKLOG_TEST_MAIN(TestRotationFlow,
                   STACKLOG_TEST_CASE(TestRotationFlow, rotation_flow));
