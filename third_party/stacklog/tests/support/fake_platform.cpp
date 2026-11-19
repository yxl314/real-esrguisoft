/// @file
/// @brief 平台端口的内存假实现。

#include "fake_platform.hpp"

#include "platform.hpp"

#include <stdexcept>

namespace fake_platform
{

FakePlatform&
FakePlatform::instance()
{
  static FakePlatform value;
  return value;
}

void
FakePlatform::reset()
{
  *this = FakePlatform();
}

bool
FakePlatform::has_file(const std::string& path) const
{
  const std::map<std::string, FakeFile>::const_iterator found
      = files.find(path);
  return found != files.end() && found->second.exists;
}

std::string
FakePlatform::file_content(const std::string& path) const
{
  const std::map<std::string, FakeFile>::const_iterator found
      = files.find(path);
  return (found == files.end()) ? std::string() : found->second.content;
}

int
FakePlatform::count_calls(const std::string& name) const
{
  int count = 0;
  for (const FakeCall& call : calls)
    {
      if (call.name == name)
        {
          ++count;
        }
    }
  return count;
}

void
FakePlatform::set_file(const std::string& path,
                       std::uint64_t size,
                       const std::string& content)
{
  FakeFile file;
  file.exists = true;
  file.size = size;
  file.content = content;
  files[path] = file;
}

void
FakePlatform::clear_calls()
{
  calls.clear();
  open_calls = 0;
  write_calls = 0;
}

} // namespace fake_platform

namespace stacklog
{
namespace platform
{
namespace
{

using fake_platform::FakeCall;
using fake_platform::FakeFile;
using fake_platform::FakePlatform;

/// @brief 记录一次端口调用。
void
record(const std::string& name,
       const std::string& first,
       const std::string& second = std::string())
{
  FakeCall call;
  call.name = name;
  call.first = first;
  call.second = second;
  FakePlatform::instance().calls.push_back(call);
}

/// @brief 在假文件系统中查找文件。
FakeFile*
find_file(const std::string& path)
{
  FakePlatform& fake = FakePlatform::instance();
  const std::map<std::string, FakeFile>::iterator found = fake.files.find(path);
  return (found == fake.files.end()) ? nullptr : &found->second;
}

/// @brief 假文件句柄：写入内存文件并按开关注入写失败。
class fake_file_sink : public file_sink
{
public:
  explicit fake_file_sink(const std::string& path) : path_(path)
  {
  }

  bool
  is_open() const override
  {
    return open_;
  }

  bool
  write_line(const std::string& text) override
  {
    FakePlatform& fake = FakePlatform::instance();
    ++fake.write_calls;
    record("write_line", path_, text);
    if (fake.write_fail_count > 0)
      {
        --fake.write_fail_count;
        return false;
      }
    if (fake.throw_write_count > 0)
      {
        --fake.throw_write_count;
        throw std::runtime_error("injected write exception");
      }
    FakeFile* file = find_file(path_);
    if (nullptr == file)
      {
        FakeFile created;
        created.exists = true;
        fake.files[path_] = created;
        file = &fake.files[path_];
      }
    file->content += text;
    file->content += '\n';
    file->size = static_cast<std::uint64_t>(file->content.size());
    return true;
  }

  void
  flush() override
  {
    record("flush", path_);
  }

  void
  close() override
  {
    record("close", path_);
    open_ = false;
  }

private:
  std::string path_;
  bool open_ = true;
};

} // namespace

std::unique_ptr<file_sink>
open_append(const std::string& path)
{
  FakePlatform& fake = FakePlatform::instance();
  record("open_append", path);
  ++fake.open_calls;
  if (fake.open_fail_count > 0)
    {
      --fake.open_fail_count;
      return nullptr;
    }
  if (!fake.has_file(path))
    {
      FakeFile file;
      file.exists = true;
      fake.files[path] = file;
    }
  return std::unique_ptr<file_sink>(new fake_file_sink(path));
}

bool
valid_utf8_path(const std::string& path)
{
  record("valid_utf8_path", path);
  return FakePlatform::instance().utf8_valid;
}

bool
path_exists(const std::string& path)
{
  record("path_exists", path);
  return FakePlatform::instance().has_file(path);
}

bool
regular_file_size(const std::string& path, std::uint64_t& size)
{
  record("regular_file_size", path);
  FakeFile* file = find_file(path);
  if (nullptr == file || !file->exists || file->directory)
    {
      return false;
    }
  size = file->size;
  return true;
}

bool
make_dirs(const std::string& directory)
{
  record("make_dirs", directory);
  return FakePlatform::instance().make_dirs_ok;
}

bool
remove_file(const std::string& path)
{
  FakePlatform& fake = FakePlatform::instance();
  record("remove_file", path);
  if (fake.remove_fail_count > 0)
    {
      --fake.remove_fail_count;
      return false;
    }
  const std::map<std::string, FakeFile>::iterator found = fake.files.find(path);
  if (found == fake.files.end() || !found->second.exists)
    {
      return false;
    }
  fake.files.erase(found);
  return true;
}

bool
rename_file(const std::string& from, const std::string& to)
{
  FakePlatform& fake = FakePlatform::instance();
  record("rename_file", from, to);
  if (fake.rename_fail_count > 0)
    {
      --fake.rename_fail_count;
      return false;
    }
  const std::map<std::string, FakeFile>::iterator source
      = fake.files.find(from);
  if (source == fake.files.end() || !source->second.exists)
    {
      return false;
    }
  if (fake.has_file(to))
    {
      return false;
    }
  fake.files[to] = source->second;
  fake.files.erase(source);
  return true;
}

std::string
join_path(const std::string& dir, const std::string& name)
{
  record("join_path", dir, name);
  if (dir.empty())
    {
      return name;
    }
  const char last = dir[dir.size() - 1];
  if (last == '/' || last == '\\')
    {
      return dir + name;
    }
  return dir + "/" + name;
}

std::string
timestamp()
{
  record("timestamp", std::string());
  return FakePlatform::instance().timestamp_value;
}

} // namespace platform
} // namespace stacklog
