# singleproc

纯 C++ 单实例运行库，适用于 C++ / C++ Qt 项目。

当前版本：`0.2.0`。

同一个用户、同一个 `key`，同一时刻只允许一个进程持有独占权。
Windows 上按 Windows 会话隔离：同一用户的不同会话（ssh/服务所在的
session 0 与登录桌面 session 1、并发 RDP 会话）可各自运行一个实例。
库不依赖 Qt，也不依赖其它第三方库。

平台支持：

- Linux / POSIX：`flock` 独占锁 + Unix domain socket 门铃；
- Windows 10+：独占共享模式文件句柄 + 命名管道门铃。

当前已在 Linux（POSIX 实现）与 Windows 10/11（Win32 实现）实测；
其它 POSIX 平台尚未验证。

## 构建

```bash
cmake -S . -B build
cmake --build build
(cd build && ctest --output-on-failure)
```

Windows（MSYS2 UCRT64 实测，编译器 g++ 16）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
cmake --build build
(cd build && ctest --output-on-failure)
```

要求：

- Linux（POSIX 实现）或 Windows 10+（Win32 实现）
- C++11
- CMake 3.16+

工程化检查使用同仓库外的三个工具：

```bash
# 构建和测试流水线
python3 ../cxx_ci/ci/ci.py ci.toml

# 排版和命名检查
python3 ../cxx_ci/standardize/standardize.py --check

# clang-tidy / CPD / ASan / Valgrind
PYTHONPATH=../cxx_audit/src python3 -m cpp_audit -c audit.toml all
```

`audit.toml` 默认启用 ASan 运行。若在受限容器中 ASan 运行时无法初始化，
把 `[asan].enabled` 改为 `false`，UBSan 仍可单独跑。

UBSan 可以单独跑：

```bash
cmake -S . -B build/ubsan \
    -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined"
cmake --build build/ubsan -j
(cd build/ubsan && ctest --output-on-failure)
```

`audit.toml` 的 Valgrind 覆盖锁、门铃和进程三组测试，由 `cpp_audit valgrind`
统一构建并汇总，不需要手工补充。

## 使用

```cpp
#include <singleproc/singleproc.hpp>

int main()
{
    singleproc::Guard guard("com.example.myapp");

    if (guard.state() == singleproc::Guard::State::AlreadyRunning)
    {
        return 0;
    }
    if (guard.state() == singleproc::Guard::State::Error)
    {
        return 1;
    }

    // 这里是主实例
    return 0;
}
```

Qt 项目：

```cpp
#include <QApplication>
#include <singleproc/singleproc.hpp>

int main(int argc, char** argv)
{
    singleproc::Guard guard("com.example.qtapp");
    if (guard.state() == singleproc::Guard::State::Error)
    {
        return 1;
    }
    if (!guard.is_primary())
    {
        return 0;
    }

    QApplication app(argc, argv);
    // ...
}
```

CMake 集成：

```cmake
add_subdirectory(singleproc)
target_link_libraries(myapp PRIVATE singleproc::singleproc)
```

安装后也可以：

```bash
cmake --install build --prefix /some/prefix
```

```cmake
find_package(singleproc REQUIRED)
target_link_libraries(myapp PRIVATE singleproc::singleproc)
```

## 行为

- 构造时创建并尝试获取锁文件：
  - POSIX：`<目录>/singleproc-<uid>-<key>.lock` 的独占文件锁（`flock`）；
  - Windows：`<目录>\singleproc-<用户名>-<会话号>-<key>.lock` 的独占共享模式句柄
    （`CreateFileW(share=0)`）。
- 取得锁：`State::Primary`。
- 已有进程持锁：`State::AlreadyRunning`。
- key、目录或锁文件不可用：`State::Error`，原因通过 `error_code()` 返回：
  POSIX 为 `errno`，Windows 为由 Win32 错误码翻译出的等价 `errno` 取值
  （`EACCES`、`ENOENT`、`EINVAL`、`ENAMETOOLONG`、`ELOOP` 等）。
- `is_primary()` 仅在 `Primary` 时为 true；`Error` 也为 false，
  需要区分时请查 `state()`。
- 析构时关闭锁句柄，锁由内核释放。
- 进程异常终止时锁同样由内核释放（POSIX 的信号终止、Windows 的
  `TerminateProcess` 都适用），不依赖 PID 文件或清理逻辑。
- 锁文件不删除。文件存在不代表实例存活，锁本身才代表。

锁文件会校验：

- 必须是普通文件；
- 必须归当前用户所有 / 由当前用户打开；
- 在文件可打开的前提下，POSIX 权限会被纠正为 `0600`；
- 拒绝符号链接：POSIX 使用 `O_NOFOLLOW`，Windows 使用
  `FILE_FLAG_OPEN_REPARSE_POINT` 并检查重解析点属性。

属主/类型校验能拒绝其他用户预创建的锁文件，但不能阻止对方制造启动失败；
多用户主机应使用私有的 `XDG_RUNTIME_DIR`，不要依赖 `/tmp` 回退。
Windows 默认目录是当前用户的 `%TEMP%`，本身按用户隔离。

## 二次启动门铃

主实例可以监听一个本机通知端点。二次实例在得到 `AlreadyRunning` 后发送一次
无载荷通知；主实例通过回调决定要做什么，例如前置已有窗口、刷新界面或写日志。

通知端点的平台实现：

- POSIX：本地 Unix domain socket，二次实例非阻塞连接一次；
- Windows：命名管道 `\\.\pipe\singleproc-<用户名>-<会话号>-<key>`，主实例持有 4 个
  重叠 IO 等待的管道实例，二次实例连接后写一个标记字节即关闭。连接本身即
  通知，标记字节不会被主实例读取。

```cpp
singleproc::Guard guard("com.example.myapp");
if (guard.state() == singleproc::Guard::State::Error)
{
    return 1;
}
if (!guard.is_primary())
{
    return 0;
}

guard.set_doorbell_handler([]() {
    // 二次启动发生：前置窗口、刷新界面或记录日志
});

while (running)
{
    guard.process_doorbell(50);
    // 其他应用逻辑
}
```

API：

```cpp
using DoorbellHandler = std::function<void()>;

void set_doorbell_handler(DoorbellHandler handler);
bool process_doorbell(int timeout_ms = 0);
int doorbell_fd() const noexcept;

// Windows 专用：门铃命名管道的可等待句柄（其它平台没有该接口）。
void* doorbell_wait_handle() const noexcept;
```

语义：

- 门铃只发送“二次启动发生了”这一事实，不携带 payload。
- 回调在调用 `process_doorbell()` 的线程中同步执行，必须保持简短。
- `process_doorbell(0)` 非阻塞；`timeout_ms < 0` 时等待到有通知为止。
- 返回 `true` 表示本次调用至少执行了一次回调。
- 通知先到、handler 后设置时，通知会保留到下一次处理。
- 多个二次实例短时间内到达时允许合并为一次回调。
- 门铃失败不影响单实例锁，也不改变 `state()` 或 `error_code()`。
- handler 抛出的异常不会被库吞掉；本次通知已经消费，不会重放。
- 门铃是 best-effort：要求主实例端点已就绪；启动竞态、队列满或发送失败时
  通知可能丢失。二次实例最多等待约 100ms。
- `Guard` 的方法不是线程安全的，应固定在一个线程中调用。
- `doorbell_fd()` 是借用 fd：由 `Guard` 拥有，有效至 `Guard` 析构；
  调用方不得 `close()` 或修改它。Windows 没有 fd 抽象，始终返回 `-1`；
  Windows 下用 `doorbell_wait_handle()` 等待，或按固定间隔调用
  `process_doorbell(0)`。

Qt 项目可以把 `doorbell_fd()` 接入已有事件循环：

```cpp
if (guard.doorbell_fd() >= 0)
{
    auto* notifier = new QSocketNotifier(guard.doorbell_fd(), QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app,
                     [&](int) { guard.process_doorbell(0); });
    // notifier 的生命周期需要覆盖 Qt 事件循环
}
```

Windows 下没有 fd，Qt 项目用定时器轮询即可，两个平台可以共用同一段代码：

```cpp
QTimer timer;
QObject::connect(&timer, &QTimer::timeout, &app, [&]() { guard.process_doorbell(0); });
timer.start(200);
```

外部只应在 fd 可读时调用 `process_doorbell()`，不要直接读取该 fd。

## key 和锁目录

- `key` 不能为空。
- `key` 只允许字母、数字、`.`、`_`、`-`。
- 默认目录：POSIX 为 `$XDG_RUNTIME_DIR`，未设置时使用 `/tmp`；
  Windows 为 `%TEMP%`。
- 也可以显式传入目录；建议使用绝对路径，避免锁位置随工作目录变化。
  目录参数在 Windows 上按 UTF-8 解释，可以包含中文。
- 文件名带当前用户标识（POSIX 为 UID，Windows 为受限字符集形式的用户名），
  避免不同用户共用同一个锁文件。
- `/tmp` 是多用户共享目录：库会检测并拒绝属主不符/非普通文件，但不能阻止
  其他用户预创建同名文件造成启动失败。多用户主机应使用私有目录。

示例：

```cpp
singleproc::Guard guard("com.example.myapp", "/tmp/my-app-locks");
```

## 测试覆盖

自动测试分组：

- `singleproc_tests`（两个平台）
  - Primary / AlreadyRunning / 析构后重新获取
  - 非法 key、NUL、非法目录、路径过长
  - 锁文件 `0600`、符号链接、FIFO（POSIX）
  - 子进程判定 AlreadyRunning（POSIX）
  - XDG_RUNTIME_DIR 与 `/tmp` 回退（POSIX）
- `singleproc_doorbell_tests`
  - 通知送达、handler 延迟设置、通知合并（两个平台）
  - handler 异常向外传播（两个平台）
  - `poll` 式 fd 接入（POSIX）、命名管道可等待句柄（Windows）
  - socket `0600`、残留 socket 清理、析构删除 socket（POSIX）
  - socket 路径过长时只禁用门铃（POSIX）
- `singleproc_process_tests`（POSIX）
  - 多进程并发启动只有一个 Primary
  - SIGKILL 后新进程重新取得锁
  - fork 后子进程继承锁
  - exec 后 `O_CLOEXEC` 关闭锁描述符
- `singleproc_win32_tests`（Windows）
  - 两个进程只有一个是 Primary
  - 进程被强杀后锁自动释放
  - 二次实例经命名管道送达通知
  - 多个二次实例的通知合并
  - UTF-8（中文）运行时目录
- `singleproc_install_consumer`
  - `cmake --install` 后独立 consumer 通过 `find_package` 消费

## 明确不做

- 不自动唤醒已有窗口；门铃只发送通知，窗口行为由调用方决定
- 不提供带 payload 的主实例/次实例消息通道
- 不提供 PID 文件
- 不清理锁文件
- 不提供 Windows 下的 fd 抽象（`doorbell_fd()` 返回 `-1`）
- 不创建后台线程，不注册信号处理器
- 不在构造函数中自动退出进程

单实例锁负责唯一性，门铃只负责通知；两者都不替应用执行业务动作。

## 许可证

MIT，见 [LICENSE](LICENSE)。

## 设计文档

当前设计快照见 [docs/项目设计.md](docs/项目设计.md)。

后续版本范围和发布条件见 [docs/版本规划.md](docs/版本规划.md)。

## 目录

```text
singleproc/
├── .clang-format
├── .clang-tidy
├── audit.toml
├── ci.toml
├── standardize.toml
├── LICENSE
├── CMakeLists.txt
├── include/singleproc/singleproc.hpp
├── src/
│   ├── platform.hpp
│   ├── singleproc.cpp          # 公共逻辑：状态、key 校验、门铃分发
│   ├── singleproc_posix.cpp    # flock + Unix domain socket
│   └── singleproc_win32.cpp    # 独占文件句柄 + 命名管道
├── tests/
│   ├── CMakeLists.txt
│   ├── test_guard.cpp
│   ├── test_doorbell.cpp
│   ├── test_process.cpp        # POSIX 进程测试
│   ├── test_win32.cpp          # Windows 进程测试
│   ├── test_support.hpp
│   ├── probe.cpp
│   ├── install_test.cmake
│   └── consumer/
├── examples/basic.cpp
├── cmake/singleprocConfig.cmake.in
├── docs/项目设计.md
└── docs/版本规划.md
```
