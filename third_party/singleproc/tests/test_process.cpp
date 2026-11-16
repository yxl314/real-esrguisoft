#include <singleproc/singleproc.hpp>

#include "test_support.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace
{

constexpr int io_timeout_ms = 5000;

void write_byte(int fd, char value)
{
    ssize_t written = 0;
    do
    {
        written = ::write(fd, &value, 1);
    } while (written < 0 && errno == EINTR);
}

int read_byte(int fd)
{
    unsigned char value = 0;
    ssize_t count = 0;
    do
    {
        count = ::read(fd, &value, 1);
    } while (count < 0 && errno == EINTR);
    if (count != 1)
    {
        return -1;
    }
    return static_cast<int>(value);
}

int read_byte_timeout(int fd, int timeout_ms)
{
    pollfd descriptor;
    descriptor.fd = fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;

    int result = 0;
    do
    {
        result = ::poll(&descriptor, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result <= 0)
    {
        return -1;
    }
    return read_byte(fd);
}

void wait_for_child(pid_t child)
{
    int status = 0;
    CHECK(::waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
}

void test_concurrent_start(const std::string& directory)
{
    const std::string key = "concurrent-start";
    const int child_count = 8;
    int start_pipe[2] = {-1, -1};
    int result_pipe[2] = {-1, -1};
    int release_pipe[2] = {-1, -1};
    CHECK(::pipe(start_pipe) == 0);
    CHECK(::pipe(result_pipe) == 0);
    CHECK(::pipe(release_pipe) == 0);
    if (start_pipe[0] < 0 || result_pipe[0] < 0 || release_pipe[0] < 0)
    {
        return;
    }

    std::vector<pid_t> children;
    children.reserve(child_count);
    for (int index = 0; index < child_count; ++index)
    {
        const pid_t child = ::fork();
        if (child == 0)
        {
            (void)::close(start_pipe[1]);
            (void)::close(result_pipe[0]);
            (void)::close(release_pipe[1]);
            if (read_byte_timeout(start_pipe[0], io_timeout_ms) < 0)
            {
                ::_exit(3);
            }

            const singleproc::Guard guard(key, directory);
            write_byte(result_pipe[1], static_cast<char>(guard.state()));
            (void)::close(result_pipe[1]);
            if (guard.state() == singleproc::Guard::State::Primary &&
                read_byte_timeout(release_pipe[0], io_timeout_ms) < 0)
            {
                ::_exit(4);
            }
            ::_exit(0);
        }

        CHECK(child > 0);
        if (child > 0)
        {
            children.push_back(child);
        }
        else
        {
            break;
        }
    }

    (void)::close(start_pipe[0]);
    (void)::close(result_pipe[1]);
    (void)::close(release_pipe[0]);
    for (std::size_t index = 0; index < children.size(); ++index)
    {
        write_byte(start_pipe[1], 'x');
    }
    (void)::close(start_pipe[1]);

    int primary_count = 0;
    int already_count = 0;
    int error_count = 0;
    for (std::size_t index = 0; index < children.size(); ++index)
    {
        const int state = read_byte_timeout(result_pipe[0], io_timeout_ms);
        if (state == static_cast<int>(singleproc::Guard::State::Primary))
        {
            ++primary_count;
        }
        else if (state == static_cast<int>(singleproc::Guard::State::AlreadyRunning))
        {
            ++already_count;
        }
        else
        {
            ++error_count;
        }
    }
    (void)::close(result_pipe[0]);
    if (primary_count > 0)
    {
        write_byte(release_pipe[1], 'x');
    }
    (void)::close(release_pipe[1]);

    for (const pid_t child : children)
    {
        wait_for_child(child);
    }

    CHECK(children.size() == static_cast<std::size_t>(child_count));
    CHECK(primary_count == 1);
    CHECK(already_count == static_cast<int>(children.size()) - 1);
    CHECK(error_count == 0);

    test_support::cleanup(directory, key);
}

void test_sigkill_recovery(const std::string& directory)
{
    const std::string key = "sigkill-recovery";
    int ready_pipe[2] = {-1, -1};
    CHECK(::pipe(ready_pipe) == 0);
    if (ready_pipe[0] < 0)
    {
        return;
    }

    const pid_t child = ::fork();
    if (child == 0)
    {
        (void)::close(ready_pipe[0]);
        const singleproc::Guard guard(key, directory);
        write_byte(ready_pipe[1], static_cast<char>(guard.state()));
        (void)::close(ready_pipe[1]);
        if (guard.state() == singleproc::Guard::State::Primary)
        {
            ::sleep(30);
        }
        ::_exit(0);
    }

    CHECK(child > 0);
    (void)::close(ready_pipe[1]);
    const int state = read_byte_timeout(ready_pipe[0], io_timeout_ms);
    (void)::close(ready_pipe[0]);
    CHECK(state == static_cast<int>(singleproc::Guard::State::Primary));

    if (child > 0)
    {
        CHECK(::kill(child, SIGKILL) == 0);
        int status = 0;
        CHECK(::waitpid(child, &status, 0) == child);
        CHECK(WIFSIGNALED(status));
        CHECK(WTERMSIG(status) == SIGKILL);
    }

    {
        const singleproc::Guard guard(key, directory);
        CHECK(guard.is_primary());
        CHECK(guard.doorbell_fd() >= 0);
    }
    test_support::cleanup(directory, key);
}

void test_fork_inherits_lock(const std::string& directory)
{
    const std::string key = "fork-inherits-lock";
    int gate_pipe[2] = {-1, -1};
    int result_pipe[2] = {-1, -1};
    CHECK(::pipe(gate_pipe) == 0);
    CHECK(::pipe(result_pipe) == 0);
    if (gate_pipe[0] < 0 || result_pipe[0] < 0)
    {
        return;
    }

    pid_t child = -1;
    {
        const singleproc::Guard parent_guard(key, directory);
        CHECK(parent_guard.is_primary());

        child = ::fork();
        if (child == 0)
        {
            (void)::close(gate_pipe[1]);
            (void)::close(result_pipe[0]);
            if (read_byte_timeout(gate_pipe[0], io_timeout_ms) < 0)
            {
                ::_exit(3);
            }

            const singleproc::Guard guard(key, directory);
            write_byte(result_pipe[1], static_cast<char>(guard.state()));
            ::_exit(0);
        }

        CHECK(child > 0);
        if (child > 0)
        {
            (void)::close(gate_pipe[0]);
            (void)::close(result_pipe[1]);
        }
    }

    if (child > 0)
    {
        write_byte(gate_pipe[1], 'x');
        (void)::close(gate_pipe[1]);
        const int state = read_byte_timeout(result_pipe[0], io_timeout_ms);
        (void)::close(result_pipe[0]);
        CHECK(state == static_cast<int>(singleproc::Guard::State::AlreadyRunning));
        wait_for_child(child);
    }

    {
        const singleproc::Guard guard(key, directory);
        CHECK(guard.is_primary());
    }
    test_support::cleanup(directory, key);
}

void test_exec_closes_lock(const std::string& directory, const std::string& probe_path)
{
    const std::string key = "exec-closes-lock";
    int gate_pipe[2] = {-1, -1};
    CHECK(::pipe(gate_pipe) == 0);
    if (gate_pipe[0] < 0)
    {
        return;
    }
    if (probe_path.empty())
    {
        CHECK(false);
        return;
    }

    pid_t child = -1;
    {
        const singleproc::Guard parent_guard(key, directory);
        CHECK(parent_guard.is_primary());

        child = ::fork();
        if (child == 0)
        {
            (void)::close(gate_pipe[1]);
            if (read_byte_timeout(gate_pipe[0], io_timeout_ms) < 0)
            {
                ::_exit(3);
            }

            ::execl(
                probe_path.c_str(), "singleproc_probe", key.c_str(), directory.c_str(), static_cast<char*>(nullptr));
            ::_exit(127);
        }

        CHECK(child > 0);
        if (child > 0)
        {
            (void)::close(gate_pipe[0]);
        }
    }

    if (child > 0)
    {
        write_byte(gate_pipe[1], 'x');
        (void)::close(gate_pipe[1]);
        int status = 0;
        CHECK(::waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status));
        CHECK(WEXITSTATUS(status) == 0);
    }
    test_support::cleanup(directory, key);
}

} // namespace

int main(int argc, char** argv)
{
    (void)::signal(SIGPIPE, SIG_IGN);

    const std::string directory = test_support::make_temp_dir();
    if (directory.empty())
    {
        (void)std::fprintf(stderr, "cannot create temporary directory\n");
        return 1;
    }

    const std::string probe_path = argc > 1 ? std::string(argv[1]) : std::string();
    test_concurrent_start(directory);
    test_sigkill_recovery(directory);
    test_fork_inherits_lock(directory);
    test_exec_closes_lock(directory, probe_path);

    test_support::remove_directory(directory);
    return test_support::finish("singleproc_process_tests");
}
