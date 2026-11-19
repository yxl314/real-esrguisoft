///
/// \file
/// \brief                      极小自研测试断言与用例运行器：不引入第三方测试框架。
///
/// 单元/冒烟测试每个文件定义一个套件类（类名 Test<源文件名>），用例写成
/// 类的 static 成员函数；main() 用 test_harness::run<Suite>() 注册并执行。
/// runner 在每个用例前后调用套件的 static init()/cleanup()，保证用例之间不
/// 共享可变状态。用例保留为类成员（而不是自由函数）是为了让文件、类与用例
/// 的归属关系可检查，也便于以后迁移到其它测试框架。
///
/// 断言失败抛出 test_harness::failure，runner 捕获后计为用例失败，并继续执行
/// 后续用例；失败信息包含表达式、文件与行号。
///

#ifndef SINGLEPROC_TEST_HARNESS_HPP
#define SINGLEPROC_TEST_HARNESS_HPP

#include <cstdio>
#include <exception>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace test_harness
{

///
/// \brief                      断言失败：由 runner 捕获并计为用例失败。
///
class failure : public std::runtime_error
{
public:
  ///
  /// \brief                    用可读的失败消息构造。
  ///
  explicit failure(const std::string& message) : std::runtime_error(message)
  {
  }
};

///
/// \brief                      组装统一格式的断言失败消息。
///
/// \param expression           断言表达式文本。
/// \param file                 源文件路径（由 __FILE__ 传入）。
/// \param line                 行号（由 __LINE__ 传入）。
/// \param detail               可选的额外说明（例如期望值/实际值）。
/// \return                     带位置与表达式的失败消息。
///
inline std::string
failure_message(const char* expression,
                const char* file,
                int line,
                const std::string& detail = {})
{
  std::ostringstream stream;
  stream << file << ":" << line << ": CHECK(" << expression << ")";
  if (!detail.empty())
    {
      stream << "  [" << detail << "]";
    }
  return stream.str();
}

///
/// \brief                      把值转成失败消息里的可读文本；要求类型可流输出。
///
template <typename Value>
std::string
describe(const Value& value)
{
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

///
/// \brief                      运行套件的全部用例；返回进程退出码（0 表示全部通过）。
///
/// \param cases                用例列表：用例名与 static 成员函数指针。
/// \return                     失败用例数为 0 时返回 0，否则返回 1。
///
template <typename Suite>
int
run(std::initializer_list<std::pair<const char*, void (*)()>> cases)
{
  int total = 0;
  int failed = 0;
  for (const auto& item : cases)
    {
      ++total;
      bool ok = true;
      std::string message;
      Suite::init();
      try
        {
          item.second();
        }
      catch (const failure& error)
        {
          ok = false;
          message = error.what();
        }
      catch (const std::exception& error)
        {
          ok = false;
          message = std::string("uncaught std::exception: ") + error.what();
        }
      catch (...)
        {
          ok = false;
          message = "uncaught non-std exception";
        }
      Suite::cleanup();
      if (ok)
        {
          (void)std::printf("[  PASS  ] %s\n", item.first);
        }
      else
        {
          (void)std::fprintf(
              stderr, "[  FAIL  ] %s: %s\n", item.first, message.c_str());
          ++failed;
        }
    }
  (void)std::printf("%d/%d cases passed\n", total - failed, total);
  return failed == 0 ? 0 : 1;
}

} // namespace test_harness

///
/// \brief                      断言条件为真；失败时抛出 failure。
///
#define CHECK(condition)                                                       \
  do                                                                           \
    {                                                                          \
      if (!(condition))                                                        \
        {                                                                      \
          throw ::test_harness::failure(::test_harness::failure_message(       \
              #condition, __FILE__, __LINE__));                                \
        }                                                                      \
    }                                                                          \
  while (false)

///
/// \brief                      断言两个值相等；失败信息包含实际值与期望值。
///
#define CHECK_EQ(actual, expected)                                             \
  do                                                                           \
    {                                                                          \
      const auto& actual_value = (actual);                                     \
      const auto& expected_value = (expected);                                 \
      if (!(actual_value == expected_value))                                   \
        {                                                                      \
          throw ::test_harness::failure(::test_harness::failure_message(       \
              #actual " == " #expected,                                        \
              __FILE__,                                                        \
              __LINE__,                                                        \
              "actual=" + ::test_harness::describe(actual_value)               \
                  + " expected=" + ::test_harness::describe(expected_value))); \
        }                                                                      \
    }                                                                          \
  while (false)

///
/// \brief                      断言表达式抛出指定类型的异常。
///
#define CHECK_THROWS(expression, exception_type)                               \
  do                                                                           \
    {                                                                          \
      bool caught = false;                                                     \
      try                                                                      \
        {                                                                      \
          (void)(expression);                                                  \
        }                                                                      \
      catch (const exception_type&)                                            \
        {                                                                      \
          caught = true;                                                       \
        }                                                                      \
      catch (...)                                                              \
        {                                                                      \
        }                                                                      \
      if (!caught)                                                             \
        {                                                                      \
          throw ::test_harness::failure(::test_harness::failure_message(       \
              #expression " throws " #exception_type, __FILE__, __LINE__));    \
        }                                                                      \
    }                                                                          \
  while (false)

#endif // SINGLEPROC_TEST_HARNESS_HPP
