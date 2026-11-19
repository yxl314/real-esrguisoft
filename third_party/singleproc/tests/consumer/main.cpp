#include <singleproc/singleproc.hpp>
#include <string>

int
main(int argc, char** argv)
{
  if (argc != 2)
    {
      return 3;
    }

  // 安装验证只需要证明 find_package 后的消费端能构建并运行；
  // key 固定即可，旧锁文件复用不影响单实例语义。
  const singleproc::guard guard("consumer-install-test", argv[1]);
  if (guard.state() == singleproc::guard::status::error)
    {
      return 2;
    }
  return guard.is_primary() ? 0 : 1;
}
