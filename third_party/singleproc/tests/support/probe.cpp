// 本文件只在 POSIX 构建中编译。整文件加平台守卫，Windows 门禁里的 clang-tidy
// 会把它当成空翻译单元，不会因为找不到 POSIX 头文件误报。
#ifndef _WIN32

#include <singleproc/singleproc.hpp>

int
main(int argc, char** argv)
{
  if (argc != 3)
    {
      return 3;
    }

  const singleproc::guard guard(argv[1], argv[2]);
  switch (guard.state())
    {
    case singleproc::guard::status::primary:
      return 0;
    case singleproc::guard::status::already_running:
      return 1;
    case singleproc::guard::status::error:
      return 2;
    }
  return 2;
}

#endif // !defined(_WIN32)
