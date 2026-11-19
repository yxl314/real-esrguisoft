#include <cerrno>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stacklog/stacklog.hpp>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace
{

/// 把命令行参数解析为正整数；非法或缺失时返回 fallback。
int
parse_count(const char* text, int fallback)
{
  if (text == nullptr)
    {
      return fallback;
    }
  char* end = nullptr;
  errno = 0;
  const long value = std::strtol(text, &end, 10);
  if (end == text || *end != '\0' || errno == ERANGE || value <= 0
      || value > INT_MAX)
    {
      return fallback;
    }
  return static_cast<int>(value);
}

/// 删除旧日志；不存在视为成功，其它错误会导致基准无法继续。
bool
remove_if_exists(const std::string& path)
{
  if (std::remove(path.c_str()) == 0 || errno == ENOENT)
    {
      return true;
    }
  std::cerr << "cannot remove " << path << ": "
            << std::error_code(errno, std::generic_category()).message()
            << '\n';
  return false;
}

} // namespace

int
main(int argc, char** argv)
{
  const int line_count = parse_count(argc > 1 ? argv[1] : nullptr, 200000);
  const int thread_count = parse_count(argc > 2 ? argv[2] : nullptr, 1);

  stacklog::config cfg;
  cfg.dir = "bench-logs";
  cfg.base_name = "bench";
  cfg.max_file_size = 512ULL * 1024 * 1024;
  cfg.max_files = 1;
  cfg.min_level = stacklog::level::info;

  if (!remove_if_exists("bench-logs/bench.log")
      || !remove_if_exists("bench-logs/bench.log.1"))
    {
      return 1;
    }
  if (!stacklog::init(cfg))
    {
      std::cerr << "stacklog benchmark init failed\n";
      return 1;
    }

  const int base = line_count / thread_count;
  const int remainder = line_count % thread_count;
  const std::chrono::steady_clock::time_point start
      = std::chrono::steady_clock::now();

  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(thread_count));
  for (int thread_index = 0; thread_index < thread_count; ++thread_index)
    {
      const int this_count = base + (thread_index < remainder ? 1 : 0);
      workers.emplace_back(
          [thread_index, this_count]()
            {
              for (int index = 0; index < this_count; ++index)
                {
                  STACKLOG_INFO()
                      << "bench thread=" << thread_index << " line=" << index;
                }
            });
    }
  for (auto& worker : workers)
    {
      worker.join();
    }

  const std::chrono::steady_clock::time_point end
      = std::chrono::steady_clock::now();
  stacklog::shutdown();

  const double seconds = std::chrono::duration<double>(end - start).count();
  std::cout << "threads=" << thread_count << " lines=" << line_count
            << " seconds=" << seconds
            << " lines/s=" << static_cast<double>(line_count) / seconds << '\n';
  return 0;
}
