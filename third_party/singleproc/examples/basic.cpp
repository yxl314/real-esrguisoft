#include <singleproc/singleproc.hpp>

#include <iostream>

int main()
{
    singleproc::Guard guard("com.example.singleproc.example");

    if (guard.state() == singleproc::Guard::State::AlreadyRunning)
    {
        std::cout << "already running\n";
        return 0;
    }
    if (guard.state() == singleproc::Guard::State::Error)
    {
        std::cerr << "singleproc error: " << guard.error_code() << '\n';
        return 1;
    }

    std::cout << "primary instance\n";
    return 0;
}
