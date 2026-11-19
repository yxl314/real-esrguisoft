#include "../support/test_harness.hpp"
#include "../support/test_support.hpp"

#include <set>
#include <stacklog/stacklog.hpp>
#include <thread>
#include <vector>

class TestCrossThreadFlow // NOLINT(readability-identifier-naming):
    // 测试策略要求类名 Test<被测单元或流程>
{
public:
  static void cross_thread_flow();
};

void
TestCrossThreadFlow::cross_thread_flow()
{
  const std::string dir = test_temp_dir();
  STACKLOG_TEST_CHECK(!dir.empty());

  stacklog::config cfg;
  cfg.dir = dir;
  cfg.base_name = "cross_thread";
  cfg.max_file_size = 1024ULL * 1024;
  cfg.max_files = 2;
  cfg.min_level = stacklog::level::debug;

  STACKLOG_TEST_CHECK(stacklog::init(cfg));

  stacklog::trace_frame captured;
  {
    STACKLOG_TRACE("cross-main");
    STACKLOG_INFO() << "main-scope";
    captured = stacklog::capture_trace();
    STACKLOG_TEST_CHECK(captured.valid());
    STACKLOG_INFO() << "main-after-capture";

    std::thread worker([captured] {
      STACKLOG_RESTORE_TRACE(captured, "cross-worker");
      STACKLOG_INFO() << "worker-scope";
      {
        STACKLOG_TRACE("cross-child");
        STACKLOG_INFO() << "worker-child";
      }
    });
    worker.join();

    // 多个线程同时接续同一帧时，节点 ID 仍必须唯一。
    std::vector<std::thread> fanout;
    fanout.reserve(4);
    for (int index = 0; index < 4; ++index)
      {
        fanout.emplace_back([captured, index] {
          STACKLOG_RESTORE_TRACE(captured, "cross-fanout");
          STACKLOG_INFO() << "fanout-" << index;
        });
      }
    for (auto& worker_thread : fanout)
      {
        worker_thread.join();
      }
  }

  const stacklog::trace_frame invalid = stacklog::capture_trace();
  STACKLOG_TEST_CHECK(!invalid.valid());
  {
    stacklog::restore_scope restore(invalid);
    STACKLOG_INFO() << "invalid-restore-scope";
  }

  const bool should_restore = false;
  // NOLINTNEXTLINE(readability-braces-around-statements): 验证宏展开为单条声明
  if (should_restore)
    STACKLOG_RESTORE_TRACE(captured, "must-not-run");
  STACKLOG_INFO() << "after-braced-if";

  stacklog::shutdown();

  const std::vector<std::string> lines
      = test_read_lines(dir + "/cross_thread.log");
  STACKLOG_TEST_CHECK(lines.size() == 10);

  const std::vector<std::string> main_line
      = test_find_line(lines, "main-scope");
  const std::vector<std::string> worker_line
      = test_find_line(lines, "worker-scope");
  const std::vector<std::string> child_line
      = test_find_line(lines, "worker-child");
  const std::vector<std::string> invalid_line
      = test_find_line(lines, "invalid-restore-scope");
  const std::vector<std::string> braced_line
      = test_find_line(lines, "after-braced-if");

  STACKLOG_TEST_CHECK(main_line.size() == 8);
  STACKLOG_TEST_CHECK(worker_line.size() == 8);
  STACKLOG_TEST_CHECK(child_line.size() == 8);
  STACKLOG_TEST_CHECK(invalid_line.size() == 8);
  STACKLOG_TEST_CHECK(braced_line.size() == 8);

  STACKLOG_TEST_CHECK(main_line[2] == worker_line[2]);
  STACKLOG_TEST_CHECK(worker_line[2] == child_line[2]);
  STACKLOG_TEST_CHECK(main_line[4] == "-");
  STACKLOG_TEST_CHECK(worker_line[4] == main_line[3]);
  STACKLOG_TEST_CHECK(child_line[4] == worker_line[3]);
  STACKLOG_TEST_CHECK(main_line[3] != worker_line[3]);
  STACKLOG_TEST_CHECK(worker_line[3] != child_line[3]);

  STACKLOG_TEST_CHECK(main_line[5] == "cross-main");
  STACKLOG_TEST_CHECK(worker_line[5] == "cross-worker");
  STACKLOG_TEST_CHECK(child_line[5] == "cross-child");

  STACKLOG_TEST_CHECK(invalid_line[2] == "-");
  STACKLOG_TEST_CHECK(invalid_line[3] == "-");
  STACKLOG_TEST_CHECK(invalid_line[4] == "-");
  STACKLOG_TEST_CHECK(invalid_line[5] == "-");

  STACKLOG_TEST_CHECK(braced_line[2] == "-");
  STACKLOG_TEST_CHECK(braced_line[3] == "-");
  STACKLOG_TEST_CHECK(braced_line[4] == "-");
  STACKLOG_TEST_CHECK(braced_line[5] == "-");

  std::set<std::string> nodes;
  nodes.insert(main_line[3]);
  nodes.insert(worker_line[3]);
  nodes.insert(child_line[3]);
  int fanout_count = 0;
  for (const auto& line : lines)
    {
      if (line.find("fanout-") == std::string::npos)
        {
          continue;
        }
      ++fanout_count;
      const std::vector<std::string> fields = test_log_fields(line);
      STACKLOG_TEST_CHECK(fields.size() == 8);
      STACKLOG_TEST_CHECK(fields[2] == main_line[2]);
      STACKLOG_TEST_CHECK(fields[4] == main_line[3]);
      STACKLOG_TEST_CHECK(fields[5] == "cross-fanout");
      STACKLOG_TEST_CHECK(nodes.insert(fields[3]).second);
    }
  STACKLOG_TEST_CHECK(fanout_count == 4);
  STACKLOG_TEST_CHECK(nodes.size() == 7);

  test_cleanup(dir);
}

STACKLOG_TEST_MAIN(TestCrossThreadFlow,
                   STACKLOG_TEST_CASE(TestCrossThreadFlow, cross_thread_flow));
