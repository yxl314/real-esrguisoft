#ifndef SINGLEPROC_TEST_SUPPORT_WIN32_HPP
#define SINGLEPROC_TEST_SUPPORT_WIN32_HPP

#ifdef _WIN32

// Windows 测试用的小工具：临时目录、子进程、SID 标识、文件标记。
// POSIX 测试使用 tests/test_support.hpp；两个平台不共用进程模型。

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdio>
#include <cstring>
#include <sddl.h>
#include <string>
#include <vector>
#include <windows.h>

namespace test_support
{

inline int&
failure_count()
{
  static int count = 0;
  return count;
}

inline void
check(bool condition, const char* expression, const char* file, int line)
{
  if (!condition)
    {
      (void)std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
      ++failure_count();
    }
}

///
/// \brief          UTF-8 → UTF-16；失败返回空串。
///
inline std::wstring
utf8_to_wide(const std::string& value)
{
  if (value.empty())
    {
      return {};
    }
  const int length = ::MultiByteToWideChar(CP_UTF8,
                                           MB_ERR_INVALID_CHARS,
                                           value.data(),
                                           static_cast<int>(value.size()),
                                           nullptr,
                                           0);
  if (length <= 0)
    {
      return {};
    }
  std::wstring buffer(static_cast<std::size_t>(length), L'\0');
  // C++11 的 std::wstring::data() 返回 const 指针，不能当写入缓冲，只能取
  // 首元素地址；clang-tidy 建议的 data() 需要 C++17，故在此抑制。
  if (::MultiByteToWideChar(
          CP_UTF8,
          MB_ERR_INVALID_CHARS,
          value.data(),
          static_cast<int>(value.size()),
          &buffer[0], // NOLINT(readability-container-data-pointer)
          length)
      != length)
    {
      return {};
    }
  return buffer;
}

///
/// \brief          UTF-16 → UTF-8；失败返回空串。
///
inline std::string
wide_to_utf8(const std::wstring& value)
{
  if (value.empty())
    {
      return {};
    }
  const int length = ::WideCharToMultiByte(CP_UTF8,
                                           0,
                                           value.data(),
                                           static_cast<int>(value.size()),
                                           nullptr,
                                           0,
                                           nullptr,
                                           nullptr);
  if (length <= 0)
    {
      return {};
    }
  std::string buffer(static_cast<std::size_t>(length), '\0');
  // 同 utf8_to_wide()：C++11 下 std::string::data() 是 const，不能作写入缓冲。
  if (::WideCharToMultiByte(
          CP_UTF8,
          0,
          value.data(),
          static_cast<int>(value.size()),
          &buffer[0], // NOLINT(readability-container-data-pointer)
          length,
          nullptr,
          nullptr)
      != length)
    {
      return {};
    }
  return buffer;
}

///
/// \brief          读取环境变量；未设置时返回空串。
///
inline std::string
environment_variable(const wchar_t* name)
{
  const DWORD length = ::GetEnvironmentVariableW(name, nullptr, 0);
  if (length == 0)
    {
      return {};
    }
  std::wstring value(static_cast<std::size_t>(length), L'\0');
  // 同 utf8_to_wide()：C++11 下 std::wstring::data() 是 const。
  const DWORD written = ::GetEnvironmentVariableW(
      name,
      &value[0], // NOLINT(readability-container-data-pointer)
      length);
  if (written == 0 || written >= length)
    {
      return {};
    }
  value.resize(static_cast<std::size_t>(written));
  return wide_to_utf8(value);
}

///
/// \brief          当前用户 SID 字符串；用于复现库内部的锁文件命名。
///
inline std::string
user_identifier()
{
  HANDLE token = nullptr;
  if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token) == 0)
    {
      return "unknown";
    }
  DWORD size = 0;
  (void)::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::string result;
  if (size >= sizeof(TOKEN_USER))
    {
      std::vector<unsigned char> buffer(size);
      if (::GetTokenInformation(token, TokenUser, buffer.data(), size, &size)
          != 0)
        {
          TOKEN_USER token_user;
          std::memcpy(&token_user, buffer.data(), sizeof(token_user));
          LPWSTR sid_text = nullptr;
          if (::ConvertSidToStringSidW(token_user.User.Sid, &sid_text) != 0)
            {
              result = wide_to_utf8(sid_text);
              ::LocalFree(sid_text);
            }
        }
    }
  (void)::CloseHandle(token);
  return result.empty() ? "unknown" : result;
}

///
/// \brief          默认运行时目录：%LOCALAPPDATA% → %TEMP%。
///
inline std::string
default_runtime_dir()
{
  std::string directory = environment_variable(L"LOCALAPPDATA");
  if (!directory.empty())
    {
      return directory;
    }
  return environment_variable(L"TEMP");
}

inline std::string
join_path(const std::string& directory, const std::string& name)
{
  if (directory.empty())
    {
      return name;
    }
  if (directory[directory.size() - 1] == '\\'
      || directory[directory.size() - 1] == '/')
    {
      return directory + name;
    }
  return directory + "\\" + name;
}

inline std::string
lock_path(const std::string& directory, const std::string& key)
{
  return join_path(directory,
                   "singleproc-" + user_identifier() + "-" + key + ".lock");
}

///
/// \brief          在 %TEMP% 下创建唯一目录；失败返回空串。
///
inline std::string
make_temp_dir()
{
  wchar_t buffer[MAX_PATH + 1];
  const DWORD length = ::GetTempPathW(MAX_PATH + 1, buffer);
  if (length == 0 || length > MAX_PATH)
    {
      return {};
    }
  std::wstring base(buffer, static_cast<std::size_t>(length));
  while (!base.empty()
         && (base[base.size() - 1] == L'\\' || base[base.size() - 1] == L'/'))
    {
      base.resize(base.size() - 1);
    }

  for (unsigned long attempt = 0; attempt < 100; ++attempt)
    {
      const std::wstring candidate
          = base + L"\\singleproc-test-"
            + std::to_wstring(
                static_cast<unsigned long>(::GetCurrentProcessId()))
            + L"-" + std::to_wstring(attempt);
      if (::CreateDirectoryW(candidate.c_str(), nullptr) != 0)
        {
          return wide_to_utf8(candidate);
        }
      if (::GetLastError() != ERROR_ALREADY_EXISTS)
        {
          return {};
        }
    }
  return {};
}

inline bool
file_exists(const std::string& path)
{
  const std::wstring wide = utf8_to_wide(path);
  return !wide.empty()
         && ::GetFileAttributesW(wide.c_str()) != INVALID_FILE_ATTRIBUTES;
}

inline bool
create_directory(const std::string& path)
{
  const std::wstring wide = utf8_to_wide(path);
  return !wide.empty() && ::CreateDirectoryW(wide.c_str(), nullptr) != FALSE;
}

inline bool
write_marker(const std::string& path)
{
  const std::wstring wide = utf8_to_wide(path);
  if (wide.empty())
    {
      return false;
    }
  const HANDLE file = ::CreateFileW(wide.c_str(),
                                    GENERIC_WRITE,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
  if (file == INVALID_HANDLE_VALUE)
    {
      return false;
    }
  (void)::CloseHandle(file);
  return true;
}

inline bool
wait_for_file(const std::string& path, DWORD timeout_ms)
{
  const ULONGLONG deadline
      = ::GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
  for (;;)
    {
      if (file_exists(path))
        {
          return true;
        }
      if (::GetTickCount64() >= deadline)
        {
          return false;
        }
      ::Sleep(5);
    }
}

inline void
remove_file(const std::string& path)
{
  const std::wstring wide = utf8_to_wide(path);
  if (!wide.empty())
    {
      (void)::DeleteFileW(wide.c_str());
    }
}

inline void
remove_directory(const std::string& directory)
{
  const std::wstring wide = utf8_to_wide(directory);
  if (wide.empty())
    {
      return;
    }
  const std::wstring pattern = wide + L"\\*";
  WIN32_FIND_DATAW data;
  const HANDLE find = ::FindFirstFileW(pattern.c_str(), &data);
  if (find != INVALID_HANDLE_VALUE)
    {
      do
        {
          const std::wstring name(data.cFileName);
          if (name == L"." || name == L"..")
            {
              continue;
            }
          std::wstring target = wide;
          target += L'\\';
          target += name;
          (void)::DeleteFileW(target.c_str());
        }
      while (::FindNextFileW(find, &data) != 0);
      (void)::FindClose(find);
    }
  (void)::RemoveDirectoryW(wide.c_str());
}

///
/// \brief          清理某个 key 留下的锁文件。
///
inline void
cleanup(const std::string& directory, const std::string& key)
{
  remove_file(lock_path(directory, key));
}

inline int
finish(const char* name)
{
  if (failure_count() != 0)
    {
      (void)std::fprintf(stderr, "%d checks failed\n", failure_count());
      return 1;
    }
  (void)std::printf("%s passed\n", name);
  return 0;
}

///
/// \brief          子进程句柄。
///
struct ChildProcess
{
  HANDLE process = nullptr;
};

///
/// \brief          当前测试程序的完整路径。
///
inline std::string
executable_path()
{
  wchar_t buffer[MAX_PATH + 1];
  const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH + 1);
  if (length == 0 || length >= MAX_PATH + 1)
    {
      return {};
    }
  return wide_to_utf8(std::wstring(buffer, static_cast<std::size_t>(length)));
}

///
/// \brief          以当前可执行文件启动子进程，参数依次追加在模式名之后。
///
inline ChildProcess
spawn_child(const std::string& mode, const std::vector<std::string>& arguments)
{
  ChildProcess child;
  const std::string path = executable_path();
  if (path.empty())
    {
      return child;
    }

  std::wstring command_line
      = L"\"" + utf8_to_wide(path) + L"\" \"" + utf8_to_wide(mode) + L"\"";
  for (const std::string& argument : arguments)
    {
      command_line += L" \"" + utf8_to_wide(argument) + L"\"";
    }

  STARTUPINFOW startup;
  std::memset(&startup, 0, sizeof(startup));
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION information;
  std::memset(&information, 0, sizeof(information));

  std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
  buffer.push_back(L'\0');
  if (::CreateProcessW(nullptr,
                       buffer.data(),
                       nullptr,
                       nullptr,
                       FALSE,
                       CREATE_NO_WINDOW,
                       nullptr,
                       nullptr,
                       &startup,
                       &information)
      == 0)
    {
      return child;
    }
  (void)::CloseHandle(information.hThread);
  child.process = information.hProcess;
  return child;
}

///
/// \brief          等待子进程结束并回收句柄。
///
inline bool
wait_child(ChildProcess& child, DWORD timeout_ms, DWORD& exit_code)
{
  if (child.process == nullptr)
    {
      return false;
    }
  if (::WaitForSingleObject(child.process, timeout_ms) != WAIT_OBJECT_0)
    {
      return false;
    }
  const bool read = ::GetExitCodeProcess(child.process, &exit_code) != FALSE;
  (void)::CloseHandle(child.process);
  child.process = nullptr;
  return read;
}

///
/// \brief          强制结束子进程并回收句柄。
///
inline bool
terminate_child(ChildProcess& child)
{
  if (child.process == nullptr)
    {
      return false;
    }
  const bool terminated = ::TerminateProcess(child.process, 1) != FALSE;
  (void)::WaitForSingleObject(child.process, 10000);
  (void)::CloseHandle(child.process);
  child.process = nullptr;
  return terminated;
}

///
/// \brief          启动一个子进程并等待其退出。
///
inline bool
run_child(const std::string& mode,
          const std::vector<std::string>& arguments,
          DWORD timeout_ms,
          DWORD& exit_code)
{
  ChildProcess child = spawn_child(mode, arguments);
  if (child.process == nullptr)
    {
      return false;
    }
  if (!wait_child(child, timeout_ms, exit_code))
    {
      (void)::TerminateProcess(child.process, 1);
      (void)::WaitForSingleObject(child.process, 10000);
      (void)::CloseHandle(child.process);
      child.process = nullptr;
      return false;
    }
  return true;
}

} // namespace test_support

#define CHECK(condition)                                                       \
  ::test_support::check((condition), #condition, __FILE__, __LINE__)

#endif // _WIN32

#endif // SINGLEPROC_TEST_SUPPORT_WIN32_HPP
