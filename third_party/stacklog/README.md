# stacklog

一个面向 C++ / Qt 项目的轻量日志库。

特性：

- DEBUG / INFO / WARN / ERROR 四级日志
- 线程安全
- 按文件大小轮转
- 运行期检测日志文件被外部删除/替换/重命名，并自动重开（Windows 上打开中的文件不能被外部删除、替换或重命名，该恢复路径仅 POSIX 可用）
- 归档失败时继续写当前文件，写失败有有限重试
- `trace_scope` 追踪调用链，日志包含当前节点和父亲节点 ID
- 显式 `trace_frame` + `restore_scope` 支持跨线程接续
- C++11，无 Qt / 第三方依赖（目录创建、文件打开等使用薄平台封装）
- Linux 与 Windows：Windows 路径按 UTF-8 转宽字符 API 后调用 Win32 原生文件 API；MinGW-w64（UCRT64 与 mingw64/MSVCRT）与 MSVC 下真实构建，Windows 10 22H2 上 14/14 CTest 通过（unit 1 + integration 11 + smoke 2）
- CMake 与 qmake 集成

## 平台支持

```text
Linux    Ubuntu 24.04（本次实测 aarch64，GCC 13 / Clang 18）；./check.sh ci、style 通过；
         audit 的 tidy / CPD / Valgrind 通过；ASan 默认启用，普通 Linux 实测通过（本次 PRoot 容器环境阻塞，见下）
Windows  Windows 10 22H2 x64 + MSYS2 UCRT64（g++ 16.2 + CMake 4.5 + Ninja + Qt 6.12 qmake）
         真实构建，14/14 CTest 通过（unit 1 + integration 11 + smoke 2）
Windows  Windows 10 22H2 x64 + MSYS2 mingw64（MSVCRT 运行时）核心构建与 14/14 CTest 通过；
         Qt6 qmake 集成使用 UCRT64 的 Qt 6.12（本机 mingw64 没有 Qt6）
Windows  Windows 11 x64 + MinGW-w64 g++ 16.2 / MSVC 19.51（2026-11-15 历史验证记录）
```

- **已验证 / 未验证**：2026-11-17 在 Windows 10 22H2（10.0.19045）x64 上实测 MSYS2 UCRT64
  （g++ 16.2 / Qt 6.12）与 mingw64（MSVCRT）；Windows 11 记录来自 2026-11-15。未验证 32 位
  （x86）、其它 MSVC 版本与其它 Windows 版本。Windows 上 ASan / Valgrind 由审计工具按平台
  跳过（见 `audit.toml` 与 `reports/`）。
- **MSYS2 环境不要混用**：编译器、Qt、make 应来自同一套 MSYS2 环境（推荐 UCRT64）。同一
  PATH 混入 mingw64 与 ucrt64 时，MSVCRT 与 UCRT 运行库会互相覆盖，典型症状是进程启动
  报 `0xC0000139`（entry point not found）或堆损坏。`stacklog_qmake` 集成测试已在自己的
  子进程里把 qmake 所在目录置顶 PATH、并优先选用同一环境的 make；其余测试可执行文件仍按
  调用者 PATH 加载运行库，因此日常构建/测试应保持单一环境。
- **MSVC 编译选项**：CMake 为本项目目标配置 `/W4 /utf-8`，不加 `/WX`（第三方头文件告警不会阻断
  构建）。`/utf-8` 让源码中的中文注释和错误文本不依赖系统代码页；不经 CMake 自行编译源码时，
  MSVC 也建议自行加 `/utf-8`。

- **路径编码**：`config.dir` 和 `config.base_name` 在 Windows 上按 UTF-8 解释，库内部转换为 UTF-16 后调用宽字符文件 API；含中文的目录可以正常初始化、写入和轮转。传入非法 UTF-8 时 `init()` 返回 false，且不会创建目录或写到错误的路径。
- **长路径**：支持 `\\?\` 扩展长度路径（必须使用反斜杠）。文件存在性、大小、删除、改名与建目录统一走 Win32 原生 API（`GetFileAttributesW` / `GetFileAttributesExW` / `DeleteFileW` / `MoveFileW` / `CreateDirectoryW`），在 UCRT 与 MSVCRT（mingw64）运行时下都能正确处理扩展路径；未加前缀且超过 `MAX_PATH` 的路径仍受 Windows 限制。
- **UNC 路径**：共享根（`\\server\share`）只校验存在、不逐级创建，共享内目录正常创建；本机没有可用共享，未做真机验证。
- **嵌入式 NUL**：路径按 C 字符串处理，遇到 NUL 截断；POSIX 与 Windows 行为一致，使用方不应传入含 NUL 的路径。
- **文件轮转**：Windows 的 rename 不允许覆盖已存在的目标，实现先删除目标归档再改名，行为与 POSIX 一致。
- **静态库**：当前目标是 `STATIC`，不导出 DLL 符号；作为 DLL 使用需要自行添加 `__declspec(dllexport/dllimport)`，不属于当前范围。
- **POSIX 专属测试**：`/dev/full` 写失败注入、删除/替换仍被打开的文件、`symlink` 等场景在 Windows 上不执行；其余测试在 Windows 上真实运行（含 install 与 qmake 集成测试）。

完整的 Windows 验证矩阵、实现要点、已知限制和审计修复记录见 [docs/windows支持.md](docs/windows支持.md)。

## 架构

- `src/stacklog.cpp` 是平台无关核心：日志格式、级别、轮转、trace、写失败重试/降级、
  文件变化检查；**不包含任何 `_WIN32` 平台宏**。
- `src/platform.hpp` 定义内部端口：路径存在/大小/逐级建目录/删除/改名、追加写文件句柄、
  UTF-8 路径预校验、路径拼接、本地时间戳。端口只在 `src/` 内使用，不安装、不进公开头。
- `src/platform_posix.cpp` / `src/platform_win32.cpp` 是端口的真实适配器：Windows 用
  Win32 原生 API 与宽字符路径，POSIX 用 `stat`/`mkdir`/`rename`。两平台共享的遍历/
  格式化/拼接/流句柄在 `src/platform_dirs.hpp`、`platform_time.hpp`、`platform_paths.hpp`、
  `platform_stream.hpp`（内部头，不安装）。
- 单元测试目标 = `src/stacklog.cpp` + `tests/support/fake_platform.cpp`，**不链接真实
  平台适配器**：假端口可编排成功/失败/写失败/rename 失败/时间/UTF-8 错误，并记录调用与
  内存文件内容。集成/冒烟目标链接真实适配器与真实文件系统。

## 目录结构

```text
stacklog/
├── include/stacklog/stacklog.hpp     # 公开接口（唯一安装的头）
├── src/
│   ├── stacklog.cpp                  # 平台无关核心（无平台宏）
│   ├── platform.hpp                  # 内部平台端口
│   ├── platform_posix.cpp            # POSIX 适配器
│   ├── platform_win32.cpp            # Windows 适配器
│   ├── platform_dirs.hpp             # 共享：逐级建目录
│   ├── platform_time.hpp             # 共享：本地时间戳
│   ├── platform_paths.hpp            # 共享：路径拼接
│   └── platform_stream.hpp           # 共享：ofstream 追加写句柄
├── CMakeLists.txt
├── stacklog.pri
├── check.sh
├── .clang-format
├── .clang-tidy
├── ci.toml
├── standardize.toml
├── audit.toml
├── LICENSE
├── examples/
│   ├── cpp_demo/
│   ├── qt_demo/
│   └── qt_worker_demo/
├── benchmarks/
├── docs/
└── tests/
    ├── unit/test_stacklog.cpp        # 假平台端口，公开 API/分支
    ├── integration/test_<流程>.cpp    # 真实文件系统复合流程
    ├── smoke/test_<业务流程>.cpp      # 完整业务线
    ├── support/                       # test_harness / fake_platform / 真实文件系统辅助
    └── coverage/run_coverage.py       # gcov JSON 公开接口/分支门禁
```

## 快速开始

```cpp
#include <stacklog/stacklog.hpp>

int main()
{
    stacklog::config cfg;
    cfg.dir = "logs";
    cfg.base_name = "app";
    cfg.min_level = stacklog::level::info;

    if (!stacklog::init(cfg))
    {
        return 1;
    }

    STACKLOG_INFO() << "application started";

    {
        STACKLOG_TRACE("load-config");
        STACKLOG_INFO() << "loading config";

        {
            STACKLOG_TRACE("parse-file");
            STACKLOG_INFO() << "parsing file";
        }
    }

    stacklog::shutdown();
    return 0;
}
```

对应日志：

```text
2026-11-10 16:56:38.485|INFO|t-...-1|t-...-1-t1-1|-|load-config|main.cpp:15|loading config
2026-11-10 16:56:38.485|INFO|t-...-1|t-...-1-t1-2|t-...-1-t1-1|parse-file|main.cpp:18|parsing file
```

`parse-file` 的 `parent_id` 就是 `load-config` 的 `node_id`。

## 日志格式

```text
时间|级别|trace_id|node_id|parent_id|scope|file:line|message
```

空字段使用 `-` 占位。消息中的 `|`、反斜杠、回车和换行会转义，保证一行一条日志。

## 日志级别

```cpp
STACKLOG_DEBUG() << "debug";
STACKLOG_INFO() << "info";
STACKLOG_WARN() << "warn";
STACKLOG_ERROR() << "error";

stacklog::set_level(stacklog::level::debug);
```

初始化配置中的 `min_level` 是运行期最低级别。`level::off` 关闭日志。

## 文件轮转

默认：

```text
单文件上限 5 MiB
保留 5 份历史文件
```

文件命名：

```text
app.log
app.log.1
app.log.2
...
app.log.5
```

超过上限时关闭当前文件，依次重命名归档，再重新打开 `app.log`。

运行期每写入 1024 条会检查一次当前文件是否仍在原路径上。文件被外部删除、截断或重命名后，会重新打开并继续写；归档重命名失败时不会丢弃当前文件，而是继续追加，并在文件再增长一个 `max_file_size` 后重试轮转。Windows 上库以默认共享模式打开日志文件，外部进程无法删除、替换或重命名仍在打开的文件，因此该恢复机制仅在 POSIX 下可用。

配置：

```cpp
stacklog::config cfg;
cfg.max_file_size = 1024 * 1024;
cfg.max_files = 10;
```

只做按大小轮转，不做按天轮转、压缩或多进程协调。

## 追踪作用域

```cpp
STACKLOG_TRACE("name");
```

- 构造时入栈，析构时出栈
- 同一线程内自动形成父子关系
- 根节点 `parent_id` 为 `-`
- 子节点 `parent_id` 为父节点 `node_id`
- 同一行不能重复使用；不同行可正常嵌套

## 跨线程追踪

显式捕获与恢复：

```cpp
const stacklog::trace_frame frame = stacklog::capture_trace();

// 把 frame 传给工作线程或线程池任务
QThreadPool::globalInstance()->start([frame] {
    STACKLOG_RESTORE_TRACE(frame, "worker-task");
    STACKLOG_INFO() << "worker doing task";
});
```

`STACKLOG_RESTORE_TRACE(frame, name)` 等价于：

```cpp
restore_scope restore(frame);
trace_scope scope(name);
```

要求：

- 必须显式把 `trace_frame` 传到目标线程
- 每个线程池任务各自 capture/restore
- `restore_scope` 必须是作用域最外层
- 不自动传播，不依赖 TLS 继承

## 初始化失败

`init()` 返回 `false` 时，日志降级到 stderr。初始化失败的具体错误会立即打印；运行期降级提示只打印一次。日志库不会因为初始化失败而终止进程。

## CMake 集成

作为子目录：

```cmake
add_subdirectory(stacklog)
target_link_libraries(your_target PRIVATE stacklog::stacklog)
```

```cpp
#include <stacklog/stacklog.hpp>
```

安装后使用：

```bash
cmake -S . -B build
cmake --build build
cmake --install build --prefix /usr/local
```

```cmake
find_package(stacklog 0.2 REQUIRED)
target_link_libraries(your_target PRIVATE stacklog::stacklog)
```

版本宏：

```cpp
STACKLOG_VERSION_STRING
STACKLOG_VERSION_MAJOR
STACKLOG_VERSION_MINOR
STACKLOG_VERSION_PATCH
```

## qmake 集成

在 `.pro` 文件中：

```qmake
include(path/to/stacklog/stacklog.pri)
```

`stacklog.pri` 已包含头文件路径、`src/stacklog.cpp`、对应平台适配器（`win32: platform_win32.cpp`，
否则 `platform_posix.cpp`）、内部端口头与 C++11 配置；pthread 只在 POSIX 下链接，Windows 不需要。

## 构建与测试

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

要求：C++11、CMake 3.16+；qmake 集成使用 Qt 5/6 的 qmake。

Windows（MSYS2 UCRT64，cmd；PATH 中只放 UCRT64 一套环境，不要与 mingw64 混用）：

```bat
set "PATH=C:\msys2\ucrt64\bin;C:\msys2\usr\bin;%PATH%"
cmake -S . -B build-win -G Ninja -DCMAKE_CXX_COMPILER=C:/msys2/ucrt64/bin/g++.exe
cmake --build build-win
ctest --test-dir build-win --output-on-failure
```

有 Qt 6 qmake 时 CTest 会自动追加 `stacklog_qmake`；该测试内部固定使用 qmake 同目录的
Qt/编译器运行库和同一环境的 make，不依赖调用者 shell 的 PATH 顺序。

MSVC 使用 Visual Studio 生成器（多配置），构建和测试都要指定配置：

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

测试按三级组织，CTest 带 `LABELS`（`ctest -L unit|integration|smoke`）：

单元（`tests/unit/`，假平台端口，覆盖全部公开 API 与分支）：

```text
init 全部失败路径（空 dir/base、零上限、UTF-8 非法、make_dirs/open 失败）与成功/重复 init
shutdown 未初始化/已初始化/重复调用；set_level 过滤与 off
log_line 启用/禁用、字段格式、文件名为空/反斜杠、流操纵符重载
嵌套 trace、capture_trace、restore_scope、restore_trace_scope（有效/无效帧）
写失败降级与 32 次后重开、轮转/remove 失败/rename 失败、外部删除与截断检测
```

集成（`tests/integration/`，真实文件系统，复合流程）：

```text
格式/转义/级别切换、轮转与 max_files=1、超长单行、归档内容完整
trace 父子/多根/作用域结束、多线程并发写、跨线程 capture/restore
非法配置与降级、外部删除/轮转失败/写失败重试、UTF-8 中文路径、并发轮转压力
CMake install + find_package consumer、qmake + Qt 工作线程 trace/轮转断言
```

冒烟（`tests/smoke/`，完整业务线）：

```text
init → 各级别日志 → 嵌套 trace → 跨线程接续 → 小文件轮转 → shutdown → 校验文件/trace
非法配置失败 → 恢复初始化 → 外部删除后重建 → 重复 shutdown
```

当前 CTest 共 14 项（unit 1、integration 11 = 9 个流程 + install + qmake、smoke 2）；
没有 qmake 的环境为 13 项。

## Qt 工作线程示例

```bash
cd examples/qt_worker_demo
qmake qt_worker_demo.pro
make -j
./qt_worker_demo
```

示例使用：

- 主线程捕获 `trace_frame`
- `QThread` 工作线程用 `restore_scope` 接续同一条 trace
- 工作线程内部继续创建 trace_scope
- 用户可写日志目录
- 小文件上限触发轮转

日志位于：

```text
QStandardPaths::AppLocalDataLocation/stacklog/stacklog-qt-worker-demo/
```

## 工程检查

项目根目录提供 `ci.toml`、`standardize.toml` 和 `audit.toml`，用外部工具替代托管 CI。排版和命名规则与 C++ 标准库一致：类型、函数和变量全部 lower_snake_case，私有成员使用尾随下划线，宏使用 UPPER_CASE。clang-tidy 开启 `modernize-*`，但排除会建议 C++14/17 设施、宏转 enum 及大范围风格改写的检查。

```bash
# 构建 + 测试（ci.toml 同时构建 benchmark；cxx_ci 的跨平台门禁入口）
python3 ../cxx_ci/check.py ci --root .

# 排版 + clang-tidy 检查
python3 ../cxx_ci/check.py style --root .

# 或一次执行 ci + style
python3 ../cxx_ci/check.py all --root .

# cxx_audit：clang-tidy / CPD / ASan / Valgrind
PYTHONPATH=../cxx_audit/src python3 -m cpp_audit -c audit.toml all

# 公开接口 + 公开分支覆盖率（gcov JSON；要求 100%，否则非零退出）
python3 tests/coverage/run_coverage.py
# 或
./check.sh coverage
```

覆盖率脚本用 `--coverage -O0` 构建 unit 目标并实测 `include/stacklog/stacklog.hpp` 的
公开函数与公开分支（gcov throw 分支除外）：当前 **16/16 公开函数、58/58 公开分支**。
白名单为空：`log_line` 析构 `catch(...)` 的异常路径由 fake 端口注入 `write_line`
抛异常的单测覆盖（`tests/unit/test_stacklog.cpp`），不需要豁免。平台适配器是内部
端口，不在公开接口统计内，由集成/冒烟测试覆盖。

Windows（cmd / PowerShell；`cxx_ci`、`cxx_audit` 与 stacklog 同级检出）：

```bat
python C:\path\to\cxx_ci\check.py ci --root .
python C:\path\to\cxx_ci\check.py style --root .
set "PYTHONPATH=C:\path\to\cxx_audit\src"
python -m cpp_audit -c audit.toml all
```

Windows 上 `check.py ci` 跑完整 CTest（含 install / qmake）；`check.py style` 用
clang-format / clang-tidy 检查排版与静态问题；`cpp_audit all` 在 Windows 上按平台跳过
ASan / Valgrind，tidy 与 CPD 正常执行。style 门禁对 clang-format / clang-tidy 大版本敏感：
本次 Linux 实测 18.1.3、Windows 实测 23.1.5，源码已对齐到两个版本都无改动的固定点；
升级到其它大版本后需要重跑 `check.py style` 复核。

`audit.toml` 的 ASan 默认启用。2026-11-17 在普通 Linux（Ubuntu 24.04 x86_64 /
GCC 15.2）上用 `-fsanitize=address` 构建并跑通 12 个 unit/integration/smoke 测试；
当前 honor 的 PRoot 容器里 ASan 运行期被地址空间限制阻塞
（`sanitizer_allocator_primary64.h` 的 CHECK 失败，见 `reports/asan.txt`），属容器
特例，不是代码问题。仅在该受限容器临时验证时可把 `[asan].enabled` 改为 `false`，
不要提交为默认值；Windows 上 `cpp_audit` 仍按平台跳过 ASan / Valgrind。

Linux 日常构建不会编译 `#ifdef _WIN32` 分支，因此额外提供一个可选的交叉编译检查
（需要 mingw-w64；未安装时打印跳过信息并返回 0，不计入失败，也不在 `all` 里）：

```bash
./check.sh windows
```

它用与项目一致的严格警告集（含 `-Werror`）交叉编译库和全部测试，只证明 Windows 分支
“能编译且无警告”，不证明运行期正确；运行期由 Windows 上的真实构建和 CTest 验证。

## 性能基准

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTACKLOG_BUILD_BENCHMARKS=ON
cmake --build build -j --target stacklog_bench
./build/benchmarks/stacklog_bench 200000 1
./build/benchmarks/stacklog_bench 200000 4
```

同步写入仍是默认方案；当前基准没有证明需要引入异步日志。方法与样例结果见 `docs/性能基准.md`。

## 项目设计

当前项目设计快照见：

```text
docs/项目设计.md
```

## 版本规划

后续版本范围和发布条件见：

```text
docs/版本规划.md
```

运行示例：

```bash
./build/examples/cpp_demo/stacklog_cpp_demo
```

qmake 示例：

```bash
cd examples/qt_demo
qmake qt_demo.pro
make -j
./qt_demo
```

工作线程示例：

```bash
cd examples/qt_worker_demo
qmake qt_worker_demo.pro
make -j
./qt_worker_demo
```

## 当前版本

```text
0.2.0
```

当前开发分支已包含 0.4 可靠性增强（外部删除恢复、归档失败处理、写失败重试、压力测试和基准），但尚未作为 0.4.0 发布；0.3 Qt 适配器仍未开始。

当前版本支持显式跨线程追踪：

- `trace_frame` 帧快照
- `capture_trace()` 捕获
- `restore_scope` RAII 接续
- `STACKLOG_RESTORE_TRACE` 宏
- 跨线程节点 ID 唯一性

不支持自动跨线程传播；跨线程必须显式传递帧。

## 设计边界

明确不做：

- 异步日志
- 网络日志
- 多进程文件锁
- 按天轮转
- 日志压缩
- Qt 翻译
- 默认 QDebug 接管（可选适配器见分析文档）
- 跨线程自动追踪恢复
- 错误码体系

这些不属于当前轻量日志库的核心需求。

完整设计说明见：

```text
docs/项目设计.md
docs/跨线程追踪与QDebug接管分析.md
docs/版本规划.md
```

## 许可证

MIT，见 `LICENSE`。
