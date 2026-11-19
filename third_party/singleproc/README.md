# singleproc

纯 C++ 单实例运行库，适用于 C++ / C++ Qt 项目。

同一个用户、同一台机器、同一个 `key`，同一时刻只允许一个进程持有独占权。
库不依赖 Qt，也不依赖其它第三方库。

支持平台：

- **Linux/POSIX**：`flock` 文件锁 + Unix domain socket 门铃；
- **Windows**：`LockFileEx` 文件锁 + 命名管道门铃。


## 构建

```bash
cmake -S . -B build
cmake --build build
(cd build && ctest --output-on-failure)
```

要求：

- Linux 或 Windows
- C++11
- CMake 3.16+

Windows 使用 MSYS2 UCRT64 工具链（显式指定 UCRT64 的 g++，避免 PATH 里
其它 MSYS2 子环境的编译器混入；门禁需要 `compile_commands.json`）：

```powershell
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Debug `
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON `
    -DCMAKE_CXX_COMPILER=C:/msys2/ucrt64/bin/g++.exe
cmake --build build-win
ctest --test-dir build-win --output-on-failure
```

Windows 使用 Visual Studio（MSVC）时注意多配置生成器必须带 `-C`：

```powershell
cmake -S . -B build-msvc -G "Visual Studio 18 2026"
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

MSVC 分支使用 `/W4` + `/utf-8`（源码含中文注释/文本，不加 `/WX`）。
MSVC 构建路径本轮没有可用环境、未复验，保留命令供有 Visual Studio 的机器使用。

工程化检查使用同仓库外的三个工具，另有覆盖率实测与可选的 Windows 交叉编译检查。`.clang-format` / `.clang-tidy` 只负责排版和命名，采用 C++ 标准库风格：类型、函数和变量使用 lower_snake_case，私有成员使用尾随下划线，宏使用 UPPER_CASE。


```bash
# 构建和测试流水线
cxx-ci -c ci.toml

# 排版和命名检查
cxx-standardize -c standardize.toml --check

# 或一次执行 ci + style
cxx-ci -c ci.toml && cxx-standardize -c standardize.toml --check

# clang-tidy / CPD / ASan / Valgrind
cxx-audit -c audit.toml all

# 公开接口/分支覆盖率实测（Linux gcov JSON；低于 100% 非零退出）
./check.sh coverage

# Windows 交叉编译检查（可选，不进入 all；需要 mingw-w64）
./check.sh windows
```

Windows 上的同一套门禁（PowerShell；三个工具已安装并在 PATH 中，
并把 MSYS2 的 mingw64 目录放进 PATH 供 clang-format / clang-tidy / cppcheck
使用）：

```powershell
$env:PATH = "C:\msys2\ucrt64\bin;C:\msys2\usr\bin;C:\msys2\mingw64\bin;" + $env:PATH
cxx-ci -c ci.toml
cxx-standardize -c standardize.toml --check
cxx-audit -c audit.toml all
```

Windows 侧 `audit` 会按平台跳过 ASan 与 Valgrind 并说明原因（ASan 无 Windows
运行期实现，Valgrind 不支持 Windows；`audit.toml` 中 ASan 保持默认启用）。

`./check.sh windows` 用 `x86_64-w64-mingw32-g++` 以严格告警集
（`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
-Wold-style-cast -Wcast-qual -Werror`）交叉编译并链接 Windows 平台实现、Windows 集成测试、单元测试
（假端口）与冒烟测试。**边界**：它只证明“可编译、可链接且无警告”，不证明运行期
正确；Windows 运行期只有真实 Windows 能验证。未安装 mingw-w64 时打印跳过
信息并返回 0。

`audit.toml` 的 ASan 默认启用；受限容器内可临时关闭，Windows 上按平台跳过。

UBSan 可以单独跑：

```bash
cmake -S . -B build/ubsan \
    -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined"
cmake --build build/ubsan -j
(cd build/ubsan && ctest --output-on-failure)
```

`audit.toml` 的 Valgrind 覆盖锁、门铃和进程三组 POSIX 测试，由
`cxx-audit valgrind` 统一构建并汇总，不需要手工补充。

## 使用

```cpp
#include <singleproc/singleproc.hpp>

int main()
{
    singleproc::guard guard("com.example.myapp");

    if (guard.state() == singleproc::guard::status::already_running)
    {
        return 0;
    }
    if (guard.state() == singleproc::guard::status::error)
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
    singleproc::guard guard("com.example.qtapp");
    if (guard.state() == singleproc::guard::status::error)
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

- 构造时创建并尝试获取运行时目录下的锁文件独占锁：
  - Linux/POSIX：`<目录>/singleproc-<uid>-<key>.lock`，使用 `flock`；
  - Windows：`<目录>\singleproc-<SID>-<key>.lock`，使用 `LockFileEx`。
- 取得锁：`status::primary`。
- 已有进程持锁：`status::already_running`。
- key、目录或锁文件不可用：`status::error`，原因通过 `error_code()` 返回，
  值为 POSIX `errno`。Windows 平台由库把 `GetLastError()` 映射成同一套
  errno，调用方读 `error_code()` 的代码不需要分平台。
- `is_primary()` 仅在 `primary` 时为 true；`error` 也为 false，
  需要区分时请查 `state()`。
- 析构时关闭文件描述符 / 句柄，锁由系统释放。
- 进程异常终止时锁同样由系统释放，不依赖 PID 文件或清理逻辑。
- 锁文件不删除。文件存在不代表实例存活，锁本身才代表。

锁文件会校验：

- 必须是普通文件；锁文件位置是目录时 `error_code()` 为 `EISDIR`；
- POSIX：属主必须是当前用户；在文件可打开的前提下，权限会被纠正为 `0600`；
  使用 `O_NOFOLLOW` 拒绝符号链接；
- Windows：拒绝重解析点（符号链接）和非普通文件，权限依赖运行时目录自身的
  ACL，库不收紧锁文件权限。

两个平台对同一输入给出同一个 `error_code()`：`EISDIR`（锁文件位置是目录）和
`ENOTDIR`（路径中某一级不是目录）在 Windows 上由库额外映射，POSIX 上由内核
直接给出。

属主/类型校验能拒绝其他用户预创建的锁文件，但不能阻止对方制造启动失败；
多用户主机应使用私有的运行时目录，不要依赖 `/tmp` 回退。

## 二次启动门铃

主实例可以监听一个本地通知端点：

- Linux/POSIX：Unix domain socket，用 `doorbell_fd()` 交给 `poll`/`select`；
- Windows：命名管道，用 `doorbell_handle()` 交给 `WaitForSingleObject` /
  `QWinEventNotifier`。

二次实例在得到 `already_running` 后发送一次无载荷通知；主实例通过回调决定
要做什么，例如前置已有窗口、刷新界面或写日志。

```cpp
singleproc::guard guard("com.example.myapp");
if (guard.state() == singleproc::guard::status::error)
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
using doorbell_handler = std::function<void()>;

void set_doorbell_handler(doorbell_handler handler);
bool process_doorbell(int timeout_ms = 0);
int doorbell_fd() const noexcept;       // POSIX：监听 fd；Windows 恒 -1
void* doorbell_handle() const noexcept; // Windows：就绪事件 HANDLE；POSIX 恒 nullptr
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
- `guard` 的方法不是线程安全的，应固定在一个线程中调用。
- `doorbell_fd()` 与 `doorbell_handle()` 都是借用句柄：由 `guard` 拥有，
  有效至 `guard` 析构；调用方不得 `close()` / `CloseHandle()`。
- Windows 的 `doorbell_handle()` 是 **manual-reset 事件**，不是命名管道句柄：
  事件有信号时调用 `process_doorbell(0)` 即可，库会收取通知、重新挂起监听
  并复位事件，调用方不要自己 `SetEvent`/`ResetEvent`；也不要对它调用
  `ReadFile`（会立即返回 `FALSE` + `ERROR_INVALID_HANDLE`）。
- 合并时机是实现的细节：handler 未设置、通知积压时，一次
  `process_doorbell()` 是否把多条通知合并成一次回调可能随平台与版本不同；
  调用方不应依赖“每次二次启动恰好一次回调”，文档只保证“允许合并”。

Qt 项目把门铃接入已有事件循环：

```cpp
// Linux/POSIX：QSocketNotifier + fd
if (guard.doorbell_fd() >= 0)
{
    auto* notifier = new QSocketNotifier(guard.doorbell_fd(), QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app,
                     [&](int) { guard.process_doorbell(0); });
    // notifier 的生命周期需要覆盖 Qt 事件循环
}
```

```cpp
// Windows：QWinEventNotifier + HANDLE
if (guard.doorbell_handle() != nullptr)
{
    auto* notifier = new QWinEventNotifier(static_cast<HANDLE>(guard.doorbell_handle()), &app);
    QObject::connect(notifier, &QWinEventNotifier::activated, &app,
                     [&] { guard.process_doorbell(0); });
    // notifier 的生命周期需要覆盖 Qt 事件循环
}
```

QWinEventNotifier 不会复位 manual-reset 事件；这里由 `process_doorbell(0)`
负责复位，所以收到 `activated` 后应立即调用它，不要在槽里长期延迟。

外部只应在 fd / 事件句柄就绪时调用 `process_doorbell()`，不要直接读取 fd，
也不要直接读取或关闭管道句柄。

## key 和锁目录

- `key` 不能为空。
- `key` 只允许字母、数字、`.`、`_`、`-`。
- 默认目录：
  - Linux/POSIX：`$XDG_RUNTIME_DIR`，未设置时使用 `/tmp`；
  - Windows：`%LOCALAPPDATA%`，未设置时使用 `%TEMP%`。
- 也可以显式传入目录；建议使用绝对路径，避免锁位置随工作目录变化。
- 文件名带当前用户标识（POSIX 为 UID，Windows 为用户 SID），避免不同用户
  共用同一个锁文件。
- `/tmp` 是多用户共享目录：库会检测并拒绝属主不符/非普通文件，但不能阻止
  其他用户预创建同名文件造成启动失败。多用户主机应使用私有目录。

示例：

```cpp
singleproc::guard guard("com.example.myapp", "/tmp/my-app-locks");
```

## Windows 已知限制

- `doorbell_fd()` 在 Windows 上恒为 `-1`：Windows 没有 fd 概念，请改用
  `doorbell_handle()`；后者返回的是 manual-reset 事件（见上文门铃语义）。
- 门铃是单实例命名管道：多个二次实例同时到达时用 `WaitNamedPipe` 排队，
  主实例长时间不处理通知时后来的通知可能丢失（属于 best-effort 范围）。
- 命名管道带显式 DACL，只允许当前用户 SID 访问；不拒绝本机同用户进程，
  单用户桌面场景下与 POSIX socket 的 `0600` 等价。
- 锁文件权限模型不同：没有 `0600` 和属主字段，依赖运行时目录 ACL。
- 不启用 Windows 长路径前缀：路径超过 `MAX_PATH`（260）时返回
  `ENAMETOOLONG`；建议使用较短的运行时目录。
- 本轮实际验证环境是 Windows 10 22H2（10.0.19045）+ MSYS2 UCRT64
  （g++ 16.2.0 / CMake 4.5.0 / Ninja 1.13.2）；Windows 11 未验证。
  MSVC 分支（`/W4`，Release）本轮没有可用环境、未复验；32 位构建未验证。
- Windows 门禁用 MSYS2 的 clang-format / clang-tidy 23.1.5（Linux 基线为
  18.1.3）；新版检查导致的代码差异与 `.clang-tidy` 中关闭的两个字面量后缀
  检查见 - Windows 平台不覆盖 fork/SIGKILL 语义，对应测试改为 `CreateProcess` +
  `TerminateProcess` 的进程模型。

## 测试结构

测试按 `ai_guidelines/测试策略.md` 分三级；目录、文件名与类名和被测对象对应，
CTest 用 LABELS 区分：`ctest -L unit`、`ctest -L integration`、`ctest -L smoke`。

| 级别 | 目录/命名 | 内容 |
|---|---|---|
| 单元 | `tests/unit/test_singleproc.cpp`，类 `test_singleproc` | 公开接口 + 公开函数分支；平台端口全部替换为 `tests/support/fake_platform.cpp`，不链接真实 `platform_*.cpp` |
| 集成 | `tests/integration/test_<流程>.cpp`，类 `test_<流程>` | 真实平台的文件系统、多进程、socket/命名管道、安装消费 |
| 冒烟 | `tests/smoke/test_<业务流程>.cpp`，类 `test_<业务流程>` | 核心完整业务线，按进程真实退出码判定 |

- 假端口 `tests/support/fake_platform.cpp` 实现 `src/platform.hpp` 的全部函数，
  可编排锁结果（primary/already_running/Error+errno）、默认目录/用户标识、
  门铃目标合法性、门铃 start 成功/返回 `nullptr`/抛 `std::bad_alloc`、
  wait/notify/stop、原生 fd/handle，并记录调用与参数；产品目标不引用
  `tests/support/`。
- 单元测试覆盖公开头 `include/singleproc/singleproc.hpp` 的全部公开方法
  （含 `doorbell_fd()` / `doorbell_handle()`），以及公开函数内的非异常分支：
  key/目录校验、路径过长、加锁失败/已占用、门铃创建失败与 `bad_alloc`、
  析构释放、`process_doorbell()` 的超时/handler 延迟/pending 保留/异常不重放。
- 覆盖率实测（Linux，gcov JSON）：

  ```bash
  ./check.sh coverage
  # 等价：python3 tests/coverage/run_coverage.py --root .
  ```

  脚本用 `--coverage -O0` 构建 unit 目标并跑 unit 测试，输出
  `public functions: x/x`、`public branches: y/y`；低于 100% 非零退出。
  不可达/编译器插入分支必须写进 `tests/coverage/branch_whitelist.txt` 并给
  理由（当前仅登记 `src/singleproc.cpp:77` 的 `new impl()` 分配失败分支；gcov 的 throw 分支按约定自动剔除）。

- 集成测试（真实平台，`ctest -L integration`）：
  - `singleproc_guard_integration`：primary/already_running/析构重取、非法
    key/目录/路径、锁文件权限/符号链接/FIFO、`EISDIR`/`ENOTDIR`、反斜杠结尾
    `runtime_dir`、XDG_RUNTIME_DIR 与 `/tmp` 回退；
  - `singleproc_doorbell_integration`：通知送达/延迟设置/合并、`poll` 接入、
    socket `0600`、残留 socket 清理、路径过长、handler 异常；
  - `singleproc_process_integration`：多进程并发唯一 primary、SIGKILL 恢复、
    fork 继承锁、exec `O_CLOEXEC`；
  - Windows：`singleproc_win32_integration`（锁、命名管道门铃、
    `TerminateProcess` 恢复、errno 映射、取消风暴）与
    `singleproc_win32_connect_race_integration`（`ERROR_NO_DATA` 竞态）；
  - `singleproc_install_consumer`：`cmake --install` 后独立 consumer 通过
    `find_package` 消费。
- 冒烟测试（两个平台，`ctest -L smoke`）：
  - `singleproc_guard_lifecycle_smoke`：Primary → 二次通知 → 释放 → 重新 primary；
  - `singleproc_second_instance_smoke`：真实子进程得到 already_running 并通知
    主实例；主实例释放后子进程成为 primary。

Unit 目标单独编译 `src/singleproc.cpp` + `tests/support/fake_platform.cpp`，
不链接真实平台实现，因此单元测试在 Linux 与 Windows 上都能跑；真实平台分支由
两平台各自的集成测试覆盖。

## 明确不做

- 不自动唤醒已有窗口；门铃只发送通知，窗口行为由调用方决定
- 不提供带 payload 的主实例/次实例消息通道
- 不提供 PID 文件
- 不清理锁文件
- 不创建后台线程，不注册信号处理器
- 不在构造函数中自动退出进程
- 不提供 Windows 命名互斥量 / 跨平台线程间同步抽象

单实例锁负责唯一性，门铃只负责通知；两者都不替应用执行业务动作。

## 许可证

MIT，见 [LICENSE](LICENSE)。
