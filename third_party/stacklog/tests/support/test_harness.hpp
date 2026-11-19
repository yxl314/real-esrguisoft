///
/// \file
/// \brief          极小的自研断言/用例注册器；不引入第三方测试框架。
///

#ifndef STACKLOG_TEST_HARNESS_HPP
#define STACKLOG_TEST_HARNESS_HPP

#include <cstddef>
#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace test_harness
{

///
/// \brief          断言失败时抛出，用于中止当前用例。
///
class assertion_failure : public std::exception
{
public:
  const char*
  what() const noexcept override
  {
    return "test assertion failed";
  }
};

///
/// \brief          一条用例：名字 + 静态成员函数指针。
///
struct case_entry
{
public:
  const char* name;
  void (*run)();
};

inline std::vector<case_entry>&
cases()
{
  static std::vector<case_entry> value;
  return value;
}

inline int&
failures()
{
  static int value = 0;
  return value;
}

inline void
add_case(const char* name, void (*run)())
{
  cases().push_back(case_entry{ name, run });
}

///
/// \brief          构造一条用例（供 STACKLOG_TEST_CASE 使用）。
///
inline case_entry
make_case(const char* name, void (*run)())
{
  return case_entry{ name, run };
}

///
/// \brief          记录失败；detail 可空。
///
inline void
fail(const char* expression,
     const char* file,
     int line,
     const std::string& detail)
{
  ++failures();
  std::fprintf(stderr, "FAIL %s:%d: %s", file, line, expression);
  if (!detail.empty())
    {
      std::fprintf(stderr, " [%s]", detail.c_str());
    }
  std::fprintf(stderr, "\n");
}

inline void
check(bool condition, const char* expression, const char* file, int line)
{
  if (!condition)
    {
      fail(expression, file, line, std::string());
    }
}

///
/// \brief          失败即中止当前用例。
///
inline void
require(bool condition, const char* expression, const char* file, int line)
{
  if (!condition)
    {
      fail(expression, file, line, std::string());
      throw assertion_failure();
    }
}

template <typename Left, typename Right>
void
check_equal(const Left& left,
            const Right& right,
            const char* expression,
            const char* file,
            int line)
{
  if (left == right)
    {
      return;
    }
  std::ostringstream detail;
  detail << "left=" << left << " right=" << right;
  fail(expression, file, line, detail.str());
}

template <typename Left, typename Right>
void
require_equal(const Left& left,
              const Right& right,
              const char* expression,
              const char* file,
              int line)
{
  if (left == right)
    {
      return;
    }
  std::ostringstream detail;
  detail << "left=" << left << " right=" << right;
  fail(expression, file, line, detail.str());
  throw assertion_failure();
}

///
/// \brief          运行全部用例；返回进程退出码。
///
inline int
run_cases(const char* suite, const case_entry* entries, std::size_t count)
{
  for (std::size_t index = 0; index < count; ++index)
    {
      const int before = failures();
      std::fprintf(stdout, "[ RUN      ] %s.%s\n", suite, entries[index].name);
      try
        {
          entries[index].run();
        }
      catch (const assertion_failure&)
        {
          // 已由断言记录失败。
        }
      catch (const std::exception& error)
        {
          ++failures();
          std::fprintf(stderr,
                       "FAIL %s.%s: unexpected exception: %s\n",
                       suite,
                       entries[index].name,
                       error.what());
        }
      catch (...)
        {
          ++failures();
          std::fprintf(stderr,
                       "FAIL %s.%s: unknown exception\n",
                       suite,
                       entries[index].name);
        }
      if (failures() == before)
        {
          std::fprintf(
              stdout, "[       OK ] %s.%s\n", suite, entries[index].name);
        }
    }
  std::fprintf(stdout,
               "TEST-RESULT suite=%s cases=%zu failures=%d\n",
               suite,
               count,
               failures());
  return (failures() == 0) ? 0 : 1;
}

} // namespace test_harness

///
/// \brief          断言；失败记录并继续。
///
#define STACKLOG_CHECK(condition)                                              \
  ::test_harness::check((condition), #condition, __FILE__, __LINE__)

///
/// \brief          断言；失败记录并抛出以中止当前用例。
///
#define STACKLOG_REQUIRE(condition)                                            \
  ::test_harness::require((condition), #condition, __FILE__, __LINE__)

///
/// \brief          相等断言；失败记录并继续。
///
#define STACKLOG_CHECK_EQ(left, right)                                         \
  ::test_harness::check_equal(                                                 \
      (left), (right), #left " == " #right, __FILE__, __LINE__)

///
/// \brief          相等断言；失败记录并抛出。
///
#define STACKLOG_REQUIRE_EQ(left, right)                                       \
  ::test_harness::require_equal(                                               \
      (left), (right), #left " == " #right, __FILE__, __LINE__)

///
/// \brief          注册一条静态成员函数用例。
///
#define STACKLOG_TEST_CASE(Class, method)                                      \
  ::test_harness::make_case(#method, &Class::method)

///
/// \brief          定义 main 并运行给定用例列表。
///
#define STACKLOG_TEST_MAIN(Class, ...)                                         \
  int main()                                                                   \
  {                                                                            \
    const ::test_harness::case_entry entries[] = { __VA_ARGS__ };              \
    return ::test_harness::run_cases(                                          \
        #Class, entries, sizeof(entries) / sizeof(entries[0]));                \
  }

#endif // STACKLOG_TEST_HARNESS_HPP
