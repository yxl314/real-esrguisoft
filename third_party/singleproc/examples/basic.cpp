#include <iostream>
#include <singleproc/singleproc.hpp>

int
main()
{
  singleproc::guard guard("com.example.singleproc.example");

  if (guard.state() == singleproc::guard::status::already_running)
    {
      std::cout << "already running\n";
      return 0;
    }
  if (guard.state() == singleproc::guard::status::error)
    {
      std::cerr << "singleproc error: " << guard.error_code() << '\n';
      return 1;
    }

  std::cout << "primary instance\n";
  return 0;
}
