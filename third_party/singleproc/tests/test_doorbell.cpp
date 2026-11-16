#include <singleproc/singleproc.hpp>

#include "test_support.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

void test_doorbell_receives(const std::string& directory)
{
    const std::string key = "doorbell-receives";
    int handler_count = 0;
    {
        singleproc::Guard primary(key, directory);
        CHECK(primary.is_primary());
        CHECK(primary.error_code() == 0);
#ifdef _WIN32
        CHECK(primary.doorbell_wait_handle() != nullptr);
#else
        CHECK(primary.doorbell_fd() >= 0);

        const int doorbell_flags = ::fcntl(primary.doorbell_fd(), F_GETFD);
        CHECK(doorbell_flags >= 0);
        if (doorbell_flags >= 0)
        {
            CHECK((doorbell_flags & FD_CLOEXEC) != 0);
        }

        struct stat socket_status = {};
        const bool stat_ok = ::stat(test_support::socket_path(directory, key).c_str(), &socket_status) == 0;
        CHECK(stat_ok);
        if (stat_ok)
        {
            CHECK((socket_status.st_mode & 0777) == 0600);
        }
#endif

        primary.set_doorbell_handler(
            [&handler_count]()
            {
                ++handler_count;
            });

        {
            singleproc::Guard secondary(key, directory);
            CHECK(secondary.state() == singleproc::Guard::State::AlreadyRunning);
            CHECK(secondary.error_code() == 0);
            CHECK(secondary.doorbell_fd() == -1);
#ifdef _WIN32
            CHECK(secondary.doorbell_wait_handle() == nullptr);
#endif
            CHECK(!secondary.process_doorbell(0));
        }

        CHECK(primary.process_doorbell(2000));
        CHECK(handler_count == 1);
        CHECK(!primary.process_doorbell(0));
    }
#ifdef _WIN32
    // 命名管道随 Guard 销毁自动释放，没有需要检查的残留文件。
#else
    CHECK(::access(test_support::socket_path(directory, key).c_str(), F_OK) != 0);
#endif
    test_support::cleanup(directory, key);
}

void test_handler_can_be_set_late(const std::string& directory)
{
    const std::string key = "doorbell-late-handler";
    int handler_count = 0;
    {
        singleproc::Guard primary(key, directory);
        {
            const singleproc::Guard secondary(key, directory);
            CHECK(secondary.state() == singleproc::Guard::State::AlreadyRunning);
        }

        CHECK(!primary.process_doorbell(0));

        primary.set_doorbell_handler(
            [&handler_count]()
            {
                ++handler_count;
            });
        CHECK(primary.process_doorbell(0));
        CHECK(handler_count == 1);
    }
    test_support::cleanup(directory, key);
}

void test_notifications_coalesce(const std::string& directory)
{
    const std::string key = "doorbell-coalesce";
    int handler_count = 0;
    {
        singleproc::Guard primary(key, directory);
        primary.set_doorbell_handler(
            [&handler_count]()
            {
                ++handler_count;
            });

        {
            const singleproc::Guard first(key, directory);
            CHECK(first.state() == singleproc::Guard::State::AlreadyRunning);
        }
        {
            const singleproc::Guard second(key, directory);
            CHECK(second.state() == singleproc::Guard::State::AlreadyRunning);
        }

        CHECK(primary.process_doorbell(2000));
        CHECK(handler_count == 1);
    }
    test_support::cleanup(directory, key);
}

void test_handler_exception_is_not_swallowed(const std::string& directory)
{
    const std::string key = "doorbell-exception";
    int handler_count = 0;
    {
        singleproc::Guard primary(key, directory);
        primary.set_doorbell_handler(
            [&handler_count]()
            {
                ++handler_count;
                throw std::runtime_error("doorbell handler");
            });

        {
            const singleproc::Guard secondary(key, directory);
            CHECK(secondary.state() == singleproc::Guard::State::AlreadyRunning);
        }

        bool caught = false;
        try
        {
            (void)primary.process_doorbell(2000);
        }
        catch (const std::runtime_error&)
        {
            caught = true;
        }
        CHECK(caught);
        CHECK(handler_count == 1);
        CHECK(!primary.process_doorbell(0));
    }
    test_support::cleanup(directory, key);
}

#ifdef _WIN32

void test_doorbell_wait_handle(const std::string& directory)
{
    const std::string key = "doorbell-wait-handle";
    int handler_count = 0;
    {
        singleproc::Guard primary(key, directory);
        primary.set_doorbell_handler(
            [&handler_count]()
            {
                ++handler_count;
            });

        {
            const singleproc::Guard secondary(key, directory);
            CHECK(secondary.state() == singleproc::Guard::State::AlreadyRunning);
        }

        const HANDLE handle = static_cast<HANDLE>(primary.doorbell_wait_handle());
        CHECK(handle != nullptr);
        if (handle != nullptr)
        {
            CHECK(::WaitForSingleObject(handle, 2000) == WAIT_OBJECT_0);
        }
        CHECK(primary.process_doorbell(0));
        CHECK(handler_count == 1);
    }
    test_support::cleanup(directory, key);
}

#else

void test_doorbell_from_child(const std::string& directory)
{
    const std::string key = "doorbell-child";
    int handler_count = 0;
    {
        singleproc::Guard primary(key, directory);
        CHECK(primary.is_primary());
        primary.set_doorbell_handler(
            [&handler_count]()
            {
                ++handler_count;
            });

        const pid_t child = ::fork();
        if (child == 0)
        {
            const singleproc::Guard secondary(key, directory);
            ::_exit(secondary.state() == singleproc::Guard::State::AlreadyRunning ? 0 : 1);
        }

        int status = 0;
        CHECK(child > 0);
        CHECK(::waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status));
        CHECK(WEXITSTATUS(status) == 0);

        CHECK(primary.process_doorbell(2000));
        CHECK(handler_count == 1);
    }
    test_support::cleanup(directory, key);
}

void test_external_poll_integration(const std::string& directory)
{
    const std::string key = "doorbell-poll";
    int handler_count = 0;
    {
        singleproc::Guard primary(key, directory);
        primary.set_doorbell_handler(
            [&handler_count]()
            {
                ++handler_count;
            });
        CHECK(primary.doorbell_fd() >= 0);

        {
            const singleproc::Guard secondary(key, directory);
            CHECK(secondary.state() == singleproc::Guard::State::AlreadyRunning);
        }

        pollfd descriptor;
        descriptor.fd = primary.doorbell_fd();
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        CHECK(::poll(&descriptor, 1, 2000) == 1);
        CHECK((descriptor.revents & POLLIN) != 0);

        CHECK(primary.process_doorbell(0));
        CHECK(handler_count == 1);
    }
    test_support::cleanup(directory, key);
}

void test_too_long_socket_path_disables_only_doorbell(const std::string& directory)
{
    const std::string key(100, 'k');
    {
        singleproc::Guard primary(key, directory);
        CHECK(primary.is_primary());
        CHECK(primary.error_code() == 0);
        CHECK(primary.doorbell_fd() == -1);

        {
            const singleproc::Guard secondary(key, directory);
            CHECK(secondary.state() == singleproc::Guard::State::AlreadyRunning);
            CHECK(secondary.error_code() == 0);
        }

        CHECK(!primary.process_doorbell(0));
    }
    test_support::cleanup(directory, key);
}

void test_stale_socket_is_reclaimed(const std::string& directory)
{
    const std::string key = "doorbell-stale";
    const std::string path = test_support::socket_path(directory, key);

    const int socket_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(socket_fd >= 0);
    if (socket_fd >= 0)
    {
        sockaddr_un address;
        std::memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
        CHECK(::bind(socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
        (void)::close(socket_fd);
    }

    int handler_count = 0;
    {
        singleproc::Guard primary(key, directory);
        CHECK(primary.is_primary());
        CHECK(primary.doorbell_fd() >= 0);
        primary.set_doorbell_handler(
            [&handler_count]()
            {
                ++handler_count;
            });

        {
            const singleproc::Guard secondary(key, directory);
            CHECK(secondary.state() == singleproc::Guard::State::AlreadyRunning);
        }
        CHECK(primary.process_doorbell(2000));
        CHECK(handler_count == 1);
    }
    CHECK(::access(path.c_str(), F_OK) != 0);
    test_support::cleanup(directory, key);
}

#endif // _WIN32

} // namespace

int main()
{
    const std::string directory = test_support::make_temp_dir();
    if (directory.empty())
    {
        (void)std::fprintf(stderr, "cannot create temporary directory\n");
        return 1;
    }

    test_doorbell_receives(directory);
    test_handler_can_be_set_late(directory);
    test_notifications_coalesce(directory);
    test_handler_exception_is_not_swallowed(directory);

#ifdef _WIN32
    test_doorbell_wait_handle(directory);
#else
    test_doorbell_from_child(directory);
    test_external_poll_integration(directory);
    test_too_long_socket_path_disables_only_doorbell(directory);
    test_stale_socket_is_reclaimed(directory);
#endif

    test_support::remove_directory(directory);
    return test_support::finish("singleproc_doorbell_tests");
}
