#ifndef STACKLOG_TEST_SUPPORT_HPP
#define STACKLOG_TEST_SUPPORT_HPP

#include "test_harness.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <direct.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <sys/stat.h>

#ifdef _WIN32
///
/// \brief          C++11 的 std::string/std::wstring::data()
///                 只读；取可写缓冲区需要
///                 &s[0] （C++17 才能用非 const
///                 data()）。集中在这里以抑制对应 clang-tidy 修复建议。
///
inline wchar_t*
writable_buffer(std::wstring& value)
{
  return &value[0]; // NOLINT(readability-container-data-pointer): C++11 的
      // data() 只读
}

inline char*
writable_buffer(std::string& value)
{
  return &value[0]; // NOLINT(readability-container-data-pointer): C++11 的
      // data() 只读
}

///
/// \brief          UTF-8 转 UTF-16；与库内约定一致，测试也把 std::string
///                 路径视为
///                 UTF-8。
///
inline std::wstring
test_wide_path(const std::string& path)
{
  if (path.empty())
    {
      return {};
    }
  const int length = ::MultiByteToWideChar(CP_UTF8,
                                           MB_ERR_INVALID_CHARS,
                                           path.data(),
                                           static_cast<int>(path.size()),
                                           nullptr,
                                           0);
  if (length <= 0)
    {
      return {};
    }
  std::wstring wide(static_cast<std::wstring::size_type>(length), L'\0');
  if (::MultiByteToWideChar(CP_UTF8,
                            MB_ERR_INVALID_CHARS,
                            path.data(),
                            static_cast<int>(path.size()),
                            writable_buffer(wide),
                            length)
      != length)
    {
      return {};
    }
  return wide;
}

///
/// \brief          UTF-16 转 UTF-8；用于把系统临时目录转回库使用的路径形式。
///
inline std::string
test_utf8_path(const std::wstring& wide)
{
  if (wide.empty())
    {
      return {};
    }
  const int length = ::WideCharToMultiByte(CP_UTF8,
                                           0,
                                           wide.data(),
                                           static_cast<int>(wide.size()),
                                           nullptr,
                                           0,
                                           nullptr,
                                           nullptr);
  if (length <= 0)
    {
      return {};
    }
  std::string path(static_cast<std::string::size_type>(length), '\0');
  if (::WideCharToMultiByte(CP_UTF8,
                            0,
                            wide.data(),
                            static_cast<int>(wide.size()),
                            writable_buffer(path),
                            length,
                            nullptr,
                            nullptr)
      != length)
    {
      return {};
    }
  return path;
}

///
/// \brief          递归删除目录（Windows 无 rm -rf）；调用前应保证文件都已关闭。
///
inline void
test_remove_tree(const std::wstring& path)
{
  WIN32_FIND_DATAW data;
  const std::wstring pattern = path + L"\\*";
  HANDLE handle = ::FindFirstFileW(pattern.c_str(), &data);
  if (INVALID_HANDLE_VALUE != handle)
    {
      do
        {
          const std::wstring name(data.cFileName);
          if (name == L"." || name == L"..")
            {
              continue;
            }
          const std::wstring child = path + L"\\" + name;
          if (0 != (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
              test_remove_tree(child);
            }
          else
            {
              (void)::DeleteFileW(child.c_str());
            }
        }
      while (0 != ::FindNextFileW(handle, &data));
      (void)::FindClose(handle);
    }
  (void)::RemoveDirectoryW(path.c_str());
}
#endif

///
/// \brief          创建测试临时目录；失败返回空串。
///
inline std::string
test_temp_dir()
{
#ifdef _WIN32
  wchar_t buffer[MAX_PATH + 1];
  const DWORD length = ::GetTempPathW(static_cast<DWORD>(MAX_PATH + 1), buffer);
  if (length == 0 || length > MAX_PATH)
    {
      return {};
    }
  std::wstring prefix(buffer, static_cast<std::wstring::size_type>(length));
  if (prefix.empty()
      || (prefix[prefix.size() - 1] != L'\\'
          && prefix[prefix.size() - 1] != L'/'))
    {
      prefix += L'\\';
    }

  // 进程号加序号已经足够唯一：每个测试是独立进程，ctest 并行也不会撞车。
  static unsigned counter = 0;
  for (unsigned attempt = 0; attempt < 100; ++attempt)
    {
      ++counter;
      const std::wstring candidate = prefix + L"stacklog-test-"
                                     + std::to_wstring(::_getpid()) + L"-"
                                     + std::to_wstring(counter);
      if (0 != ::CreateDirectoryW(candidate.c_str(), nullptr))
        {
          return test_utf8_path(candidate);
        }
      if (::GetLastError() != ERROR_ALREADY_EXISTS)
        {
          return {};
        }
    }
  return {};
#else
  char pattern[] = "/tmp/stacklog-test-XXXXXX";
  char* path = mkdtemp(pattern);
  return (nullptr == path) ? std::string() : std::string(path);
#endif
}

///
/// \brief          删除测试临时目录。
///
inline void
test_cleanup(const std::string& dir)
{
  if (dir.empty())
    {
      return;
    }
#ifdef _WIN32
  const std::wstring wide = test_wide_path(dir);
  if (!wide.empty())
    {
      test_remove_tree(wide);
    }
#else
  const std::string command = "rm -rf \"" + dir + "\"";
  const int status
      = std::system(command.c_str()); // NOLINT(cert-env33-c): 测试临时目录清理
  (void)status;
#endif
}

///
/// \brief          判断路径是否存在（文件或目录）。
///
inline bool
test_path_exists(const std::string& path)
{
#ifdef _WIN32
  const std::wstring wide = test_wide_path(path);
  return !wide.empty()
         && ::GetFileAttributesW(wide.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
  struct stat info;
  return ::stat(path.c_str(), &info) == 0;
#endif
}

///
/// \brief          判断路径是否是普通文件。
///
inline bool
test_is_regular_file(const std::string& path)
{
#ifdef _WIN32
  const std::wstring wide = test_wide_path(path);
  if (wide.empty())
    {
      return false;
    }
  // 与库一致用 Win32 原生 API：MSVCRT 的 _wstat 不支持 \\?\ 扩展路径。
  const DWORD attributes = ::GetFileAttributesW(wide.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES
         && 0 == (attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat info;
  return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
#endif
}

///
/// \brief          创建单个测试目录；已存在也算成功。
///
inline bool
test_make_dir(const std::string& path)
{
#ifdef _WIN32
  const std::wstring wide = test_wide_path(path);
  if (wide.empty())
    {
      return false;
    }
  return 0 != ::CreateDirectoryW(wide.c_str(), nullptr)
         || ::GetLastError() == ERROR_ALREADY_EXISTS;
#else
  return ::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

///
/// \brief          删除空测试目录。
///
inline bool
test_remove_dir(const std::string& path)
{
#ifdef _WIN32
  const std::wstring wide = test_wide_path(path);
  return !wide.empty() && 0 != ::RemoveDirectoryW(wide.c_str());
#else
  return ::rmdir(path.c_str()) == 0;
#endif
}

///
/// \brief          删除测试文件。
///
inline bool
test_remove_file(const std::string& path)
{
#ifdef _WIN32
  const std::wstring wide = test_wide_path(path);
  return !wide.empty() && 0 != ::DeleteFileW(wide.c_str());
#else
  return std::remove(path.c_str()) == 0;
#endif
}

///
/// \brief          覆盖写测试文件；内容按字节写入，不做换行转换。
///
inline bool
test_write_file(const std::string& path, const std::string& content)
{
#ifdef _WIN32
  const std::wstring wide = test_wide_path(path);
  if (wide.empty())
    {
      return false;
    }
  std::ofstream stream(wide.c_str(), std::ios::out | std::ios::binary);
#else
  std::ofstream stream(path.c_str(), std::ios::out | std::ios::binary);
#endif
  if (!stream.is_open())
    {
      return false;
    }
  stream.write(content.data(), static_cast<std::streamsize>(content.size()));
  stream.flush();
  return stream.good();
}

///
/// \brief          读取文本文件的所有行。
///
inline std::vector<std::string>
test_read_lines(const std::string& path)
{
  std::vector<std::string> lines;
#ifdef _WIN32
  const std::wstring wide = test_wide_path(path);
  if (wide.empty())
    {
      return lines;
    }
  std::ifstream stream(wide.c_str(), std::ios::in | std::ios::binary);
#else
  std::ifstream stream(path.c_str(), std::ios::in | std::ios::binary);
#endif
  std::string line;
  while (std::getline(stream, line))
    {
      // 日志按二进制模式写入，行尾只有 '\n'；这里仍容忍
      // CRLF，便于人工查看文件。
      if (!line.empty() && line[line.size() - 1] == '\r')
        {
          line.erase(line.size() - 1);
        }
      lines.push_back(line);
    }
  return lines;
}

///
/// \brief          按未转义的 `|` 拆分日志行，保留字段中的转义序列。
///
inline std::vector<std::string>
test_log_fields(const std::string& line)
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
  if (escaped)
    {
      current += '\\';
    }
  fields.push_back(current);
  return fields;
}

///
/// \brief          查找包含指定文本的第一行，并按未转义的 `|` 拆成字段。
///
inline std::vector<std::string>
test_find_line(const std::vector<std::string>& lines, const std::string& needle)
{
  for (const std::string& line : lines)
    {
      if (line.find(needle) != std::string::npos)
        {
          return test_log_fields(line);
        }
    }
  return {};
}

#define STACKLOG_TEST_CHECK(condition) STACKLOG_REQUIRE(condition)

#endif // STACKLOG_TEST_SUPPORT_HPP
