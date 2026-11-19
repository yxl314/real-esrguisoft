/// @file
/// @brief stacklog 公开接口的单元测试；平台层用 fake_platform 替身。

#include "../support/fake_platform.hpp"
#include "../support/test_harness.hpp"

#include <cstddef>
#include <stacklog/stacklog.hpp>
#include <string>
#include <vector>

namespace
{

using fake_platform::FakePlatform;

FakePlatform&
fake()
{
  return FakePlatform::instance();
}

/// @brief 每个用例先关闭日志器、再清空假平台，保证用例间状态独立。
void
reset_state()
{
  stacklog::shutdown();
  fake().reset();
}

stacklog::config
make_config()
{
  stacklog::config cfg;
  cfg.dir = "/fake/logs";
  cfg.base_name = "app";
  cfg.max_file_size = 4096;
  cfg.max_files = 3;
  cfg.min_level = stacklog::level::info;
  return cfg;
}

std::string
log_path()
{
  return "/fake/logs/app.log";
}

std::string
archived_path(unsigned index)
{
  return log_path() + "." + std::to_string(index);
}

std::vector<std::string>
split_lines(const std::string& text)
{
  std::vector<std::string> lines;
  std::string current;
  for (char ch : text)
    {
      if (ch == '\n')
        {
          lines.push_back(current);
          current.clear();
        }
      else
        {
          current += ch;
        }
    }
  if (!current.empty())
    {
      lines.push_back(current);
    }
  return lines;
}

/// @brief 按未转义的 '|' 拆字段（与日志格式约定一致）。
std::vector<std::string>
split_fields(const std::string& line)
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
  fields.push_back(current);
  return fields;
}

bool
contains(const std::string& text, const std::string& needle)
{
  return text.find(needle) != std::string::npos;
}

} // namespace

class TestStacklog // NOLINT(readability-identifier-naming): 测试策略要求类名
    // Test<被测单元或流程>
{
public:
  // init
  static void init_empty_dir_returns_false();
  static void init_empty_base_name_returns_false();
  static void init_zero_max_file_size_returns_false();
  static void init_zero_max_files_returns_false();
  static void init_invalid_utf8_fails_before_make_dirs();
  static void init_make_dirs_failure_returns_false();
  static void init_open_failure_returns_false();
  static void init_success_creates_current_file();
  static void init_trailing_separator_dir_joins_path();
  static void init_twice_closes_previous_file();
  static void init_after_shutdown_reopens_file();
  // shutdown / level
  static void shutdown_without_init_is_noop();
  static void shutdown_closes_open_file();
  static void shutdown_is_repeatable();
  static void set_level_below_minimum_is_disabled();
  static void set_level_off_disables_all_output();
  static void set_level_reenables_after_off();
  // log_line
  static void log_line_severity_off_is_disabled();
  static void log_line_below_minimum_is_disabled();
  static void log_line_writes_formatted_fields();
  static void log_line_stream_manipulator_is_buffered();
  static void log_line_null_file_uses_dash_location();
  static void log_line_backslash_file_uses_base_name();
  // trace
  static void trace_scope_nested_links_parent_and_scope();
  static void trace_scope_empty_name_uses_unnamed();
  static void capture_trace_without_scope_returns_invalid();
  static void trace_frame_valid_requires_both_ids();
  static void capture_trace_with_scope_returns_valid_frame();
  static void restore_scope_with_invalid_frame_is_noop();
  static void restore_scope_with_valid_frame_restores_trace();
  static void restore_trace_scope_with_valid_frame_links_child();
  static void restore_trace_scope_with_invalid_frame_starts_trace();
  // 写失败 / 重试 / 轮转 / 外部变化
  static void write_failure_falls_back_then_retries_open();
  static void write_line_exception_is_caught_by_log_line_destructor();
  static void write_retry_reopen_failure_then_success();
  static void rotation_creates_archives_and_prunes_oldest();
  static void rotation_remove_failure_keeps_writing();
  static void rotation_rename_failure_keeps_writing();
  static void rotation_reopen_failure_falls_back();
  static void external_delete_reopens_at_check_interval();
  static void external_truncation_reopens_at_check_interval();
};

void
TestStacklog::init_empty_dir_returns_false()
{
  reset_state();
  stacklog::config cfg = make_config();
  cfg.dir.clear();
  STACKLOG_CHECK(!stacklog::init(cfg));
  STACKLOG_CHECK_EQ(fake().count_calls("open_append"), 0);
}

void
TestStacklog::init_empty_base_name_returns_false()
{
  reset_state();
  stacklog::config cfg = make_config();
  cfg.base_name.clear();
  STACKLOG_CHECK(!stacklog::init(cfg));
}

void
TestStacklog::init_zero_max_file_size_returns_false()
{
  reset_state();
  stacklog::config cfg = make_config();
  cfg.max_file_size = 0;
  STACKLOG_CHECK(!stacklog::init(cfg));
}

void
TestStacklog::init_zero_max_files_returns_false()
{
  reset_state();
  stacklog::config cfg = make_config();
  cfg.max_files = 0;
  STACKLOG_CHECK(!stacklog::init(cfg));
}

void
TestStacklog::init_invalid_utf8_fails_before_make_dirs()
{
  reset_state();
  fake().utf8_valid = false;
  STACKLOG_CHECK(!stacklog::init(make_config()));
  STACKLOG_CHECK_EQ(fake().count_calls("make_dirs"), 0);
  STACKLOG_CHECK_EQ(fake().count_calls("open_append"), 0);
}

void
TestStacklog::init_make_dirs_failure_returns_false()
{
  reset_state();
  fake().make_dirs_ok = false;
  STACKLOG_CHECK(!stacklog::init(make_config()));
  STACKLOG_CHECK_EQ(fake().count_calls("open_append"), 0);
}

void
TestStacklog::init_open_failure_returns_false()
{
  reset_state();
  fake().open_fail_count = 1;
  STACKLOG_CHECK(!stacklog::init(make_config()));
  STACKLOG_CHECK(!fake().has_file(log_path()));
}

void
TestStacklog::init_success_creates_current_file()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  STACKLOG_CHECK(fake().has_file(log_path()));
  STACKLOG_CHECK_EQ(fake().count_calls("make_dirs"), 1);
  STACKLOG_CHECK_EQ(fake().count_calls("valid_utf8_path"), 1);
}

void
TestStacklog::init_trailing_separator_dir_joins_path()
{
  reset_state();
  stacklog::config cfg = make_config();
  cfg.dir = "/fake/logs/";
  STACKLOG_CHECK(stacklog::init(cfg));
  STACKLOG_CHECK(fake().has_file(log_path()));
}

void
TestStacklog::init_twice_closes_previous_file()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  STACKLOG_INFO() << "first";
  const std::string first_content = fake().file_content(log_path());
  STACKLOG_CHECK(contains(first_content, "first"));
  STACKLOG_CHECK(stacklog::init(make_config()));
  STACKLOG_CHECK_EQ(fake().open_calls, 2);
  STACKLOG_CHECK(fake().count_calls("close") >= 1);
  STACKLOG_CHECK_EQ(fake().file_content(log_path()), first_content);
}

void
TestStacklog::init_after_shutdown_reopens_file()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  stacklog::shutdown();
  STACKLOG_CHECK(stacklog::init(make_config()));
  STACKLOG_CHECK_EQ(fake().open_calls, 2);
  STACKLOG_INFO() << "after-reopen";
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "after-reopen"));
}

void
TestStacklog::shutdown_without_init_is_noop()
{
  reset_state();
  stacklog::shutdown();
  stacklog::shutdown();
  STACKLOG_CHECK_EQ(fake().count_calls("close"), 0);
}

void
TestStacklog::shutdown_closes_open_file()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  const std::string before = fake().file_content(log_path());
  stacklog::shutdown();
  STACKLOG_CHECK(fake().count_calls("close") >= 1);
  STACKLOG_INFO() << "after-shutdown";
  STACKLOG_CHECK_EQ(fake().file_content(log_path()), before);
}

void
TestStacklog::shutdown_is_repeatable()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  stacklog::shutdown();
  const int closes = fake().count_calls("close");
  stacklog::shutdown();
  STACKLOG_CHECK_EQ(fake().count_calls("close"), closes);
}

void
TestStacklog::set_level_below_minimum_is_disabled()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  stacklog::set_level(stacklog::level::warn);
  STACKLOG_INFO() << "hidden";
  STACKLOG_CHECK(fake().file_content(log_path()).empty());
  STACKLOG_WARN() << "shown";
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "shown"));
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "|WARN|"));
}

void
TestStacklog::set_level_off_disables_all_output()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  stacklog::set_level(stacklog::level::off);
  STACKLOG_ERROR() << "hidden";
  STACKLOG_CHECK(fake().file_content(log_path()).empty());
}

void
TestStacklog::set_level_reenables_after_off()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  stacklog::set_level(stacklog::level::off);
  stacklog::set_level(stacklog::level::debug);
  STACKLOG_DEBUG() << "visible-again";
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "visible-again"));
}

void
TestStacklog::log_line_severity_off_is_disabled()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  {
    stacklog::detail::log_line line(stacklog::level::off, __FILE__, __LINE__);
    line << "never";
  }
  STACKLOG_CHECK(fake().file_content(log_path()).empty());
}

void
TestStacklog::log_line_below_minimum_is_disabled()
{
  reset_state();
  stacklog::config cfg = make_config();
  cfg.min_level = stacklog::level::warn;
  STACKLOG_CHECK(stacklog::init(cfg));
  STACKLOG_INFO() << "hidden" << 1;
  STACKLOG_INFO()
      << "hidden-manip"
      << std::endl; // NOLINT(performance-avoid-endl): 刻意覆盖流操纵符重载
  STACKLOG_CHECK(fake().file_content(log_path()).empty());
}

void
TestStacklog::log_line_writes_formatted_fields()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  STACKLOG_INFO() << "hello" << 42;
  const std::vector<std::string> lines
      = split_lines(fake().file_content(log_path()));
  STACKLOG_REQUIRE_EQ(lines.size(), static_cast<std::size_t>(1));
  const std::vector<std::string> fields = split_fields(lines[0]);
  STACKLOG_REQUIRE_EQ(fields.size(), static_cast<std::size_t>(8));
  STACKLOG_CHECK_EQ(fields[0], fake().timestamp_value);
  STACKLOG_CHECK_EQ(fields[1], std::string("INFO"));
  STACKLOG_CHECK_EQ(fields[2], std::string("-"));
  STACKLOG_CHECK_EQ(fields[3], std::string("-"));
  STACKLOG_CHECK_EQ(fields[4], std::string("-"));
  STACKLOG_CHECK_EQ(fields[5], std::string("-"));
  STACKLOG_CHECK(contains(fields[6], "test_stacklog.cpp:"));
  STACKLOG_CHECK_EQ(fields[7], std::string("hello42"));
}

void
TestStacklog::log_line_stream_manipulator_is_buffered()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  STACKLOG_WARN()
      << "manip"
      << std::endl; // NOLINT(performance-avoid-endl): 刻意覆盖流操纵符重载
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "manip"));
}

void
TestStacklog::log_line_null_file_uses_dash_location()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  {
    stacklog::detail::log_line line(stacklog::level::info, nullptr, 9);
    line << "null-file";
  }
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "|-:9|"));
}

void
TestStacklog::log_line_backslash_file_uses_base_name()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  {
    stacklog::detail::log_line line(
        stacklog::level::info, "dir\\sub\\file.cpp", 12);
    line << "backslash-file";
  }
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "file.cpp:12"));
}

void
TestStacklog::trace_scope_nested_links_parent_and_scope()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  stacklog::trace_frame outer;
  stacklog::trace_frame inner;
  {
    stacklog::trace_scope outer_scope("outer");
    outer = stacklog::capture_trace();
    {
      stacklog::trace_scope inner_scope("inner");
      inner = stacklog::capture_trace();
      STACKLOG_INFO() << "nested-line";
    }
  }
  STACKLOG_CHECK(outer.valid());
  STACKLOG_CHECK(inner.valid());
  STACKLOG_CHECK_EQ(inner.trace_id, outer.trace_id);
  STACKLOG_CHECK_EQ(inner.parent_id, outer.node_id);
  STACKLOG_CHECK(!stacklog::capture_trace().valid());
  const std::vector<std::string> lines
      = split_lines(fake().file_content(log_path()));
  STACKLOG_REQUIRE_EQ(lines.size(), static_cast<std::size_t>(1));
  const std::vector<std::string> fields = split_fields(lines[0]);
  STACKLOG_REQUIRE_EQ(fields.size(), static_cast<std::size_t>(8));
  STACKLOG_CHECK_EQ(fields[2], inner.trace_id);
  STACKLOG_CHECK_EQ(fields[3], inner.node_id);
  STACKLOG_CHECK_EQ(fields[4], outer.node_id);
  STACKLOG_CHECK_EQ(fields[5], std::string("inner"));
}

void
TestStacklog::trace_scope_empty_name_uses_unnamed()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  {
    stacklog::trace_scope scope("");
    STACKLOG_INFO() << "unnamed-line";
  }
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "|unnamed|"));
}

void
TestStacklog::capture_trace_without_scope_returns_invalid()
{
  reset_state();
  STACKLOG_CHECK(!stacklog::capture_trace().valid());
}

void
TestStacklog::trace_frame_valid_requires_both_ids()
{
  reset_state();
  stacklog::trace_frame frame;
  STACKLOG_CHECK(!frame.valid());
  frame.trace_id = "t-1";
  STACKLOG_CHECK(!frame.valid());
  frame.node_id = "t-1-t1-1";
  STACKLOG_CHECK(frame.valid());
}

void
TestStacklog::capture_trace_with_scope_returns_valid_frame()
{
  reset_state();
  stacklog::trace_frame frame;
  {
    stacklog::trace_scope scope("captured");
    frame = stacklog::capture_trace();
  }
  STACKLOG_CHECK(frame.valid());
  STACKLOG_CHECK_EQ(frame.scope, std::string("captured"));
  STACKLOG_CHECK(!frame.trace_id.empty());
  STACKLOG_CHECK(!frame.node_id.empty());
}

void
TestStacklog::restore_scope_with_invalid_frame_is_noop()
{
  reset_state();
  {
    stacklog::restore_scope restore{ stacklog::trace_frame() };
    STACKLOG_CHECK(!stacklog::capture_trace().valid());
  }
  STACKLOG_CHECK(!stacklog::capture_trace().valid());
}

void
TestStacklog::restore_scope_with_valid_frame_restores_trace()
{
  reset_state();
  stacklog::trace_frame frame;
  {
    stacklog::trace_scope scope("origin");
    frame = stacklog::capture_trace();
  }
  {
    stacklog::restore_scope restore(frame);
    const stacklog::trace_frame restored = stacklog::capture_trace();
    STACKLOG_CHECK_EQ(restored.trace_id, frame.trace_id);
    STACKLOG_CHECK_EQ(restored.node_id, frame.node_id);
    STACKLOG_CHECK_EQ(restored.parent_id, frame.parent_id);
  }
  STACKLOG_CHECK(!stacklog::capture_trace().valid());
}

void
TestStacklog::restore_trace_scope_with_valid_frame_links_child()
{
  reset_state();
  stacklog::trace_frame frame;
  {
    stacklog::trace_scope scope("outer");
    frame = stacklog::capture_trace();
  }
  {
    stacklog::detail::restore_trace_scope scope(frame, "sub");
    const stacklog::trace_frame restored = stacklog::capture_trace();
    STACKLOG_CHECK_EQ(restored.trace_id, frame.trace_id);
    STACKLOG_CHECK_EQ(restored.parent_id, frame.node_id);
    STACKLOG_CHECK_EQ(restored.scope, std::string("sub"));
  }
  STACKLOG_CHECK(!stacklog::capture_trace().valid());
}

void
TestStacklog::restore_trace_scope_with_invalid_frame_starts_trace()
{
  reset_state();
  {
    stacklog::detail::restore_trace_scope scope(stacklog::trace_frame(), "");
    const stacklog::trace_frame restored = stacklog::capture_trace();
    STACKLOG_CHECK(restored.valid());
    STACKLOG_CHECK(restored.parent_id.empty());
    STACKLOG_CHECK_EQ(restored.scope, std::string("unnamed"));
  }
  STACKLOG_CHECK(!stacklog::capture_trace().valid());
}

void
TestStacklog::write_line_exception_is_caught_by_log_line_destructor()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  fake().throw_write_count = 1;
  STACKLOG_INFO() << "throws-from-write";
  STACKLOG_CHECK(fake().count_calls("write_line") == 1);
  // 异常在 log_line 析构里被捕获，后续日志仍然可写。
  STACKLOG_INFO() << "after-throw";
  STACKLOG_CHECK(fake().count_calls("write_line") == 2);
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "after-throw"));
}

void
TestStacklog::write_failure_falls_back_then_retries_open()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  fake().write_fail_count = 1;
  STACKLOG_INFO() << "failed-write";
  STACKLOG_CHECK(fake().file_content(log_path()).empty());
  for (int index = 0; index < 32; ++index)
    {
      STACKLOG_INFO() << "retry-" << index;
    }
  STACKLOG_CHECK(fake().open_calls >= 2);
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "retry-31"));
}

void
TestStacklog::write_retry_reopen_failure_then_success()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  fake().write_fail_count = 1;
  STACKLOG_INFO() << "failed-write";
  fake().open_fail_count = 1;
  for (int index = 0; index < 32; ++index)
    {
      STACKLOG_INFO() << "retry-a-" << index;
    }
  const int opens_after_failure = fake().open_calls;
  STACKLOG_CHECK(opens_after_failure >= 2);
  for (int index = 0; index < 32; ++index)
    {
      STACKLOG_INFO() << "retry-b-" << index;
    }
  STACKLOG_CHECK(fake().open_calls > opens_after_failure);
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "retry-b-31"));
}

void
TestStacklog::rotation_creates_archives_and_prunes_oldest()
{
  reset_state();
  stacklog::config cfg = make_config();
  cfg.max_file_size = 1;
  cfg.max_files = 2;
  STACKLOG_CHECK(stacklog::init(cfg));
  STACKLOG_INFO() << "line-1";
  STACKLOG_INFO() << "line-2";
  STACKLOG_INFO() << "line-3";
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "line-3"));
  STACKLOG_CHECK(contains(fake().file_content(archived_path(1)), "line-2"));
  STACKLOG_CHECK(contains(fake().file_content(archived_path(2)), "line-1"));
  STACKLOG_CHECK(!fake().has_file(archived_path(3)));
}

void
TestStacklog::rotation_remove_failure_keeps_writing()
{
  reset_state();
  stacklog::config cfg = make_config();
  cfg.max_file_size = 1;
  cfg.max_files = 2;
  STACKLOG_CHECK(stacklog::init(cfg));
  STACKLOG_INFO() << "line-1";
  STACKLOG_INFO() << "line-2";
  STACKLOG_INFO() << "line-3";
  fake().remove_fail_count = 1;
  STACKLOG_INFO() << "line-4";
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "line-4"));
  STACKLOG_CHECK(fake().count_calls("remove_file") >= 1);
}

void
TestStacklog::rotation_rename_failure_keeps_writing()
{
  reset_state();
  stacklog::config cfg = make_config();
  cfg.max_file_size = 1;
  cfg.max_files = 2;
  STACKLOG_CHECK(stacklog::init(cfg));
  STACKLOG_INFO() << "line-1";
  STACKLOG_INFO() << "line-2";
  fake().rename_fail_count = 1;
  STACKLOG_INFO() << "line-3";
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "line-3"));
}

void
TestStacklog::rotation_reopen_failure_falls_back()
{
  reset_state();
  stacklog::config cfg = make_config();
  cfg.max_file_size = 1;
  cfg.max_files = 2;
  STACKLOG_CHECK(stacklog::init(cfg));
  STACKLOG_INFO() << "line-1";
  fake().open_fail_count = 1;
  STACKLOG_INFO() << "line-2";
  STACKLOG_CHECK(!contains(fake().file_content(log_path()), "line-2"));
  for (int index = 0; index < 32; ++index)
    {
      STACKLOG_INFO() << "after-reopen-" << index;
    }
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "after-reopen-31"));
}

void
TestStacklog::external_delete_reopens_at_check_interval()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  for (int index = 0; index < 1023; ++index)
    {
      STACKLOG_INFO() << "bulk-" << index;
    }
  const int opens_before = fake().open_calls;
  fake().files.erase(log_path());
  STACKLOG_INFO() << "after-delete";
  STACKLOG_CHECK(fake().open_calls > opens_before);
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "after-delete"));
  STACKLOG_CHECK(!contains(fake().file_content(log_path()), "bulk-0"));
}

void
TestStacklog::external_truncation_reopens_at_check_interval()
{
  reset_state();
  STACKLOG_CHECK(stacklog::init(make_config()));
  for (int index = 0; index < 1023; ++index)
    {
      STACKLOG_INFO() << "bulk-" << index;
    }
  const int opens_before = fake().open_calls;
  fake_platform::FakeFile& file = fake().files[log_path()];
  file.size = 0;
  file.content.clear();
  STACKLOG_INFO() << "after-truncate";
  STACKLOG_CHECK(fake().open_calls > opens_before);
  STACKLOG_CHECK(contains(fake().file_content(log_path()), "after-truncate"));
  STACKLOG_CHECK(!contains(fake().file_content(log_path()), "bulk-0"));
}

STACKLOG_TEST_MAIN(
    TestStacklog,
    STACKLOG_TEST_CASE(TestStacklog, init_empty_dir_returns_false),
    STACKLOG_TEST_CASE(TestStacklog, init_empty_base_name_returns_false),
    STACKLOG_TEST_CASE(TestStacklog, init_zero_max_file_size_returns_false),
    STACKLOG_TEST_CASE(TestStacklog, init_zero_max_files_returns_false),
    STACKLOG_TEST_CASE(TestStacklog, init_invalid_utf8_fails_before_make_dirs),
    STACKLOG_TEST_CASE(TestStacklog, init_make_dirs_failure_returns_false),
    STACKLOG_TEST_CASE(TestStacklog, init_open_failure_returns_false),
    STACKLOG_TEST_CASE(TestStacklog, init_success_creates_current_file),
    STACKLOG_TEST_CASE(TestStacklog, init_trailing_separator_dir_joins_path),
    STACKLOG_TEST_CASE(TestStacklog, init_twice_closes_previous_file),
    STACKLOG_TEST_CASE(TestStacklog, init_after_shutdown_reopens_file),
    STACKLOG_TEST_CASE(TestStacklog, shutdown_without_init_is_noop),
    STACKLOG_TEST_CASE(TestStacklog, shutdown_closes_open_file),
    STACKLOG_TEST_CASE(TestStacklog, shutdown_is_repeatable),
    STACKLOG_TEST_CASE(TestStacklog, set_level_below_minimum_is_disabled),
    STACKLOG_TEST_CASE(TestStacklog, set_level_off_disables_all_output),
    STACKLOG_TEST_CASE(TestStacklog, set_level_reenables_after_off),
    STACKLOG_TEST_CASE(TestStacklog, log_line_severity_off_is_disabled),
    STACKLOG_TEST_CASE(TestStacklog, log_line_below_minimum_is_disabled),
    STACKLOG_TEST_CASE(TestStacklog, log_line_writes_formatted_fields),
    STACKLOG_TEST_CASE(TestStacklog, log_line_stream_manipulator_is_buffered),
    STACKLOG_TEST_CASE(TestStacklog, log_line_null_file_uses_dash_location),
    STACKLOG_TEST_CASE(TestStacklog, log_line_backslash_file_uses_base_name),
    STACKLOG_TEST_CASE(TestStacklog, trace_scope_nested_links_parent_and_scope),
    STACKLOG_TEST_CASE(TestStacklog, trace_scope_empty_name_uses_unnamed),
    STACKLOG_TEST_CASE(TestStacklog,
                       capture_trace_without_scope_returns_invalid),
    STACKLOG_TEST_CASE(TestStacklog, trace_frame_valid_requires_both_ids),
    STACKLOG_TEST_CASE(TestStacklog,
                       capture_trace_with_scope_returns_valid_frame),
    STACKLOG_TEST_CASE(TestStacklog, restore_scope_with_invalid_frame_is_noop),
    STACKLOG_TEST_CASE(TestStacklog,
                       restore_scope_with_valid_frame_restores_trace),
    STACKLOG_TEST_CASE(TestStacklog,
                       restore_trace_scope_with_valid_frame_links_child),
    STACKLOG_TEST_CASE(TestStacklog,
                       restore_trace_scope_with_invalid_frame_starts_trace),
    STACKLOG_TEST_CASE(TestStacklog,
                       write_failure_falls_back_then_retries_open),
    STACKLOG_TEST_CASE(TestStacklog,
                       write_line_exception_is_caught_by_log_line_destructor),
    STACKLOG_TEST_CASE(TestStacklog, write_retry_reopen_failure_then_success),
    STACKLOG_TEST_CASE(TestStacklog,
                       rotation_creates_archives_and_prunes_oldest),
    STACKLOG_TEST_CASE(TestStacklog, rotation_remove_failure_keeps_writing),
    STACKLOG_TEST_CASE(TestStacklog, rotation_rename_failure_keeps_writing),
    STACKLOG_TEST_CASE(TestStacklog, rotation_reopen_failure_falls_back),
    STACKLOG_TEST_CASE(TestStacklog, external_delete_reopens_at_check_interval),
    STACKLOG_TEST_CASE(TestStacklog,
                       external_truncation_reopens_at_check_interval));
