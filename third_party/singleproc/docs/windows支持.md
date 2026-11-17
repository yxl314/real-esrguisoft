# Windows 支持

## 1. 验证矩阵

| 能力 | Linux | Windows | 说明 |
|---|---|---|---|
| 构建 | 支持（Ubuntu 24.04.5 / GCC 13.3.0 / Clang 18.1.3 + CMake 3.28.3） | 支持（Windows 10 22H2 / MSYS2 UCRT64 g++ 16.2.0 / CMake 4.5.0；MSVC 分支本轮未复验） | 同一套 CMake 工程，按平台选择 `platform_posix.cpp` / `platform_win32.cpp` |
| 单元/集成/冒烟测试 | 支持（ctest LABELS：unit/integration/smoke） | 支持（同一套三级测试；Windows 集成测试为 `singleproc_win32_integration` / `singleproc_win32_connect_race_integration`） | 单元测试用假平台端口、两平台共用；Windows 测试默认参与构建；多配置生成器必须用 `ctest -C <Config>`，MinGW 单配置直接 `ctest` |
| 集成测试 | 支持（多进程并发、SIGKILL、fork/exec、install consumer） | 支持（真实两进程锁/门铃、TerminateProcess、install consumer） | Windows 没有 fork，测试用 `CreateProcess` 重新执行测试程序 |
| 静态检查 | 支持（clang-tidy、CPD、standardize） | 支持（`cxx_ci check.py style` + `cpp_audit` 的 tidy / CPD；`./check.sh windows` 交叉编译严格告警仍可选） | 平台专属文件加平台守卫，Windows 侧 clang-tidy / CPD 直接覆盖 Windows 代码 |
| sanitizer | 支持（ASan 默认启用；普通 Linux 实测通过） | 跳过（ASan 无 Windows 运行期实现，Valgrind 无 Windows 实现） | Windows 靠原生 tidy、CPD 与契约/压力测试兜底 |
| 包管理器集成 | 支持（`cmake --install` + 独立 consumer） | 支持（install consumer；MSVC 多配置本轮未复验） | 下游通过 `find_package(singleproc)` 消费 |

## 2. 已验证环境

验证日期：2026-11-17（以当时 HEAD 为准），构建、CTest 与门禁在同一轮里
分别跑通。

- Linux：Ubuntu 24.04.5 LTS x64（8 核）；GCC 13.3.0；Clang 18.1.3；
  CMake 3.28.3；clang-format / clang-tidy 18.1.3；Valgrind 3.22.0；
  cppcheck 2.13.0。`cmake` + `ctest` 7 个测试（unit/integration/smoke
  三级）全过；`./check.sh coverage` 输出 `public functions: 9/9`、
  `public branches: 64/64`；`./check.sh ci`、
  `style` 通过；`audit` 的 tidy（7 个 TU，0 错 0 警）、CPD、Valgrind 通过，
  ASan 默认启用；本次 honor 的 PRoot 容器因地址空间限制无法初始化（见第 5 节），普通 Linux 已实测通过。
- Windows：Windows 10 22H2（10.0.19045.6466，x64）；MSYS2 UCRT64
  g++ 16.2.0；CMake 4.5.0；Ninja 1.13.2；clang-format / clang-tidy 23.1.5
  （MSYS2 mingw64）；cppcheck 2.22.0；PMD 7.28.0 + Temurin JRE 21.0.13；
  Python 3.12.10。`cmake` + `ctest` 6 个测试（unit/integration/smoke
  三级，Windows 测试真实运行）全过；
  `python ..\cxx_ci\check.py ci|style` 通过（standardize 15 个文件
  0 改动 0 失败）；`cpp_audit` 的 tidy（5 个 TU，0 错 0 警）与 CPD 通过，
  ASan/Valgrind 在 Windows 上按平台跳过；`audit.toml` 中 ASan 保持默认启用。
- 门禁工具版本：cxx_ci `47aa09a`、cxx_audit `04bf3a1`（两端用同一份）。

## 3. 平台实现要点

1. **平台差异集中收敛。** 公开头文件 `include/singleproc/singleproc.hpp` 两个
   平台完全一致；`src/singleproc.cpp` 只放业务逻辑（key/路径校验、状态机、
   门铃分发），不出现平台宏；`src/platform.hpp` 是内部接口（不安装），
   `src/platform_posix.cpp` 与 `src/platform_win32.cpp` 各自实现。
2. **文件锁映射：`flock` → `CreateFileW` + `LockFileEx`。** POSIX 继续用
   `flock(LOCK_EX | LOCK_NB)`；Windows 用 `CreateFileW(OPEN_ALWAYS)` 打开后以
   `LockFileEx(EXCLUSIVE | FAIL_IMMEDIATELY)` 锁整个文件范围，冲突时得到
   `ERROR_LOCK_VIOLATION` 并报告 `AlreadyRunning`（同进程第二个句柄同样冲突）。
   锁文件不删除；进程退出/句柄关闭后系统释放锁。Windows 在 `CloseHandle`
   前显式 `UnlockFileEx`（MS 文档建议），关闭仍作为兜底。
3. **门铃映射：Unix domain socket → 命名管道。** POSIX 监听
   `<runtime_dir>/singleproc-<uid>-<key>.sock`；Windows 监听
   `\\.\pipe\singleproc-<SID>-<key>.doorbell`，两者都是“连接即通知”。Windows
   用重叠式 `ConnectNamedPipe` + manual-reset 事件，不创建后台线程；
   单实例管道 + `WaitNamedPipeW` 排队（总等待约 100ms），与 POSIX 队列一样
   属于 best-effort，多个通知允许合并。
4. **两个门铃访问器的平台语义（不泄漏平台类型）。**
   `doorbell_fd()`：POSIX 返回监听 fd，**Windows 恒返回 -1**；
   `doorbell_handle()`：**Windows 返回连接就绪的 manual-reset 事件 HANDLE，
   POSIX 恒返回 nullptr**。该 HANDLE 不是命名管道句柄：调用方不能
   `ReadFile` / `CloseHandle` / `SetEvent` / `ResetEvent`；事件有信号时调用
   `process_doorbell(0)`，由库内部收取通知、重新挂起监听并复位事件
   （误用 `ReadFile` 会立即返回 FALSE + `ERROR_INVALID_HANDLE`，不会挂起）。
5. **errno 跨平台一致。** `error_code()` 在两个平台都返回 POSIX errno；
   Windows 把 `GetLastError()` 映射为 `ENOENT` / `ENOTDIR` / `EACCES` /
   `EINVAL` / `ENAMETOOLONG` / `EISDIR` / `ELOOP` / `EIO` 等。锁文件位置是
   目录、路径中间级是文件、锁文件是符号链接等场景与 POSIX 同值。
6. **路径与用户标识。** `join_path` 只认平台分隔符（POSIX 上 `\` 是合法
   文件名字符，不能当分隔符）；Windows 用用户 SID 代替 UID 生成锁文件名与
   管道名；超过 `MAX_PATH`（260）的路径直接返回 `ENAMETOOLONG`，不启用
   `\\?\` 长路径前缀。

7. **跨平台门禁：平台守卫 + 工具版本对齐。** 两个平台的专属源文件都整文件
   加平台守卫（POSIX 用 `#ifndef _WIN32`，Windows 用 `#ifdef _WIN32`），
   任一平台的 clang-tidy 扫描全仓库时，另一平台的专属文件是空翻译单元，
   不会因为缺少对方平台头文件而报 `clang-diagnostic-error`。Windows 门禁用
   MSYS2 的 clang-tidy 23.1.5（Linux 基线是 18.1.3）：23 新增检查的意见已
   落到代码（枚举尾逗号、`#ifndef` 守卫、lambda 空参数列表、Win32 `BOOL`
   显式转 `bool`、C++11 下 `std::wstring/std::string::data()` 返回 const
   的 NOLINT 说明、单字符字符串改用字符重载等）；只有 `cert-dcl16-c` 与
   `readability-uppercase-literal-suffix` 会在 libstdc++ 系统头文件上产生
   一条不带文件位置的裸 warning（cxx_audit 按行计数会误算成项目告警），已在
   `.clang-tidy` 中关闭并写明原因。Windows 测试默认参与构建
   （顶层构建时 `SINGLEPROC_BUILD_TESTS` 默认 `ON`），ctest 会真实运行它们。

## 4. 如何验证

Linux：

```bash
./check.sh all
```

Windows，MinGW-w64（MSYS2 UCRT64；显式指定 UCRT64 的 g++，门禁需要
`compile_commands.json`）：

```powershell
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Debug `
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON `
    -DCMAKE_CXX_COMPILER=C:/msys2/ucrt64/bin/g++.exe
cmake --build build-win
ctest --test-dir build-win --output-on-failure
```

Windows 上的门禁（PowerShell；cxx_ci / cxx_audit 放在相邻目录，PATH 里补上
MSYS2 mingw64 以提供 clang-format / clang-tidy / cppcheck）：

```powershell
$env:PATH = "C:\msys2\ucrt64\bin;C:\msys2\usr\bin;C:\msys2\mingw64\bin;" + $env:PATH
python ..\cxx_ci\check.py ci --root .
python ..\cxx_ci\check.py style --root .
$env:PYTHONPATH = "..\cxx_audit\src"
python -m cpp_audit -c audit.toml all
```

Windows 的 MSVC 路径（多配置生成器，构建与 ctest 必须带配置）本轮没有可用
环境、未复验，命令保留供有 Visual Studio 的机器使用：

```powershell
cmake -S . -B build-msvc -G "Visual Studio 18 2026"
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

Linux 上的 Windows 交叉编译检查（可选，不进入 `all`；未安装 mingw-w64 时
打印跳过信息并返回 0）：

```bash
./check.sh windows
```

该阶段用严格告警集交叉编译并链接 Windows 平台实现与 Windows 测试，**只证明
可编译、可链接且无警告，不证明运行期正确**；运行期行为必须用真实 Windows
构建 + ctest 验证。Windows 原生门禁（`cxx_ci check.py style` 与
`cpp_audit` 的 tidy / CPD）现已直接覆盖 Windows 平台文件，交叉编译检查只作为
可选的补充。

## 5. 已知边界与限制

- 本轮只验证 Windows 10 22H2 x64 + MinGW-w64（MSYS2 UCRT64）g++ 16.2.0；
  Windows 11、32 位构建、MSVC 分支（本轮无环境，未复验）未验证。
- Windows 门铃是单实例命名管道：二次实例用 `WaitNamedPipeW` 排队，总等待
  约 100ms；极端并发下通知可能合并或丢弃（属既有 best-effort 语义，不是
  可靠消息队列）。
- Windows 没有 fd、没有 `0600`/属主字段：锁文件访问控制依赖运行时目录 ACL，
  命名管道用显式 DACL 只允许当前用户 SID。
- 不支持超过 `MAX_PATH`（260）的路径（返回 `ENAMETOOLONG`），未启用
  `\\?\` 长路径前缀。
- Windows 符号链接锁文件的 `ELOOP` 代码路径存在，但创建符号链接需要额外
  权限，未纳入自动测试（较早版本用人工探针验证过返回 `ELOOP`）。
- Windows 分支没有 ASan/Valgrind 覆盖：`cpp_audit` 会在 Windows 上以明确原因
  SKIPPED（缺 MinGW 运行期库、LSan 没有 Windows 实现）；Valgrind 没有 Windows
  版本，同样跳过。`audit.toml` 的 ASan 默认启用；当前 honor 的 PRoot 容器因
  shadow 影射申请失败无法初始化（容器地址空间限制，不是代码问题），普通 Linux
  主机已实测 ASan 通过。仅在该受限容器临时验证时可把 `enabled` 改为 `false`，
  不要提交为默认值。

- MSVC 分支使用 `/W4` + `/utf-8` 但未启用 `/WX`：当前 0 告警，告警不会阻断
  构建。

## 6. 审计修复记录（2026-11）

| 问题 | 机制（为什么错） | 修法 | 提交号 |
|---|---|---|---|
| POSIX `join_path` 回归 | `join_path` 同时把 `/` 和 `\` 当分隔符。POSIX 上 `\` 是合法文件名字符，`runtime_dir` 以反斜杠结尾时锁文件会落到与旧版本不同的路径，新旧版本同时运行会各自加锁（可双开） | 只按平台分隔符 `path_separator()` 判断；新增写死期望路径的对拍用例，修复前实现会失败 | `6b5676a` |
| 门铃 `ERROR_NO_DATA` 被当失败 | 客户端在主实例 `CreateNamedPipeW` 与 `ConnectNamedPipe` 之间连上并立刻关闭时，`ConnectNamedPipe` 返回 FALSE + `ERROR_NO_DATA`（232，“连过又断开”）。原实现只识别 `ERROR_IO_PENDING`/`ERROR_PIPE_CONNECTED`，按失败处理 → `start_doorbell` 失败 → 门铃在该实例生命周期内永久禁用。自然竞态实测 95/2000 次失败 | 识别 `ERROR_NO_DATA`：记一次通知、`DisconnectNamedPipe` 后重新投递监听（有界重试）；`wait` 侧优先消费该通知；新增宏钩子确定性回归测试 | `6b5676a` |
| `CancelIoEx` 后立即释放 `OVERLAPPED` | `CancelIoEx` 只请求取消、不等待完成，内核可能在取消真正完成前仍然写 `OVERLAPPED`。原实现取消后立即 `CloseHandle` 并 `delete Doorbell`（内含 `OVERLAPPED`），违反 Windows 取消契约，存在 UAF 风险 | 取消请求成功后用 `GetOverlappedResult(..., TRUE)` 等 IO 真正完成，再关闭句柄/释放结构；新增取消风暴压力测试 | `6b5676a` |
| errno 不一致：锁路径是目录 | Windows 打开目录失败给 `ERROR_ACCESS_DENIED` → `EACCES`（13），POSIX `open` 给 `EISDIR`（21）；调用方按 `error_code()` 分派的代码无法跨平台一致 | Windows 增加已存在路径类型预检（`FILE_READ_ATTRIBUTES` + `BACKUP_SEMANTICS` + `OPEN_REPARSE_POINT`）：目录→`EISDIR`、重解析点→`ELOOP`；`validate_lock_file` 同步；两平台用例断言同一数值 | `6b5676a` |
| errno 不一致：路径中间级是文件 | `map_path_error` 只检查最近一级父目录；更深的某一级是文件时 Windows 给 `ENOENT`（2），POSIX 给 `ENOTDIR`（20） | 从最近父路径向上找第一个存在的祖先：存在但不是目录→`ENOTDIR`，全部不存在→`ENOENT` | `6b5676a` |
| `GetOverlappedResult` 失败未收尾 | 事件已 signaled 但 `GetOverlappedResult` 失败时原实现只 `break`，事件保持 signaled，事件驱动调用方会被反复唤醒（忙循环），且门铃状态已不可用 | 失败时按门铃失效处理（`abandon_doorbell`），不再把 stale 事件留给调用方 | `6b5676a` |
| 释放锁缺 `UnlockFileEx` | 只 `CloseHandle` 依赖系统释放锁；MS 文档建议显式解锁，资源紧张时锁释放时机可能滞后 | `CloseHandle` 前先 `UnlockFileEx`（失败由关闭兜底） | `6b5676a` |
| 异常路径句柄泄漏 | 取得锁后 `start_doorbell` 仍可能因内存分配抛 `bad_alloc`；构造函数未完成时析构函数不会执行，锁句柄/管道/token 句柄会泄漏且锁不释放 | 构造函数内 try/catch 先释放锁再传播；POSIX 门铃路径改定长缓冲（取锁后不再分配）；token 与 `LocalAlloc` 内存用 RAII 守卫 | `6b5676a`、`9423613` |
| MSVC 下 install consumer 多配置问题 | 多配置生成器下 `install_test` 使用默认生成器与固定 Debug：consumer 可能落到没有 MSVC 开发环境的生成器（找不到 `rc`），或 Debug consumer 链接 Release 库（LNK2038） | 把父工程生成器、`$<CONFIG>`、多配置标志、可执行后缀传入脚本；install/configure/build/run 四处使用同一配置 | `0507133` |
| Windows 分支静态检查缺口 | Windows 平台文件不参与 Linux 构建，clang-tidy/ASan/Valgrind 都不覆盖，Windows 代码静态质量此前只靠人工 | 新增可选 `./check.sh windows`：严格告警集交叉编译并链接库与 Windows 测试；不进入 `all`，无 mingw 时跳过并返回 0 | `0507133` |

## 7. 2026-11-17 回归修复与门禁补全

1. **回退未完成的 Windows 结构改造。** 提交 `726c49e` 处于半成品状态：
   公开头文件改成直接成员，而 `singleproc.cpp` 仍按 pimpl（`Guard::Impl`）
   实现，`doorbell_handle()` 只有声明没有定义，Windows 测试还调用了旧结构里
   不存在的接口，Windows 侧直接编译失败。按“恢复到上一验证版本”的方针
   `git revert 726c49e`，不触碰门禁提交 `4dbf183` 的内容。
2. **Windows 测试真实运行。** 回退后 Windows 默认构建三个测试
   （unit + `singleproc_win32_integration` /
   `singleproc_win32_connect_race_integration` / `singleproc_install_consumer`
   + 两条 smoke），`ctest` 6/6 全过。
3. **Windows 静态门禁补齐。** `python ..\cxx_ci\check.py ci|style` 通过
   （standardize 15 个文件 0 改动 0 失败）；`cpp_audit` 的 tidy（5 个有编译
   命令的 TU，0 错 0 警）与 CPD 通过，ASan 默认启用（本 PRoot 容器环境阻塞）、Valgrind 跳过。
4. **平台专属文件加守卫。** POSIX 专属实现/测试整文件包 `#ifndef _WIN32`，
   与 Windows 专属文件既有的 `#ifdef _WIN32` 对称，保证任一平台静态检查扫描
   全仓库时不会因缺对方平台头文件误报。
5. **工具版本差异处理。** 见第 3 节第 7 条：新版检查意见已落到代码，仅
   `.clang-tidy` 关闭两个会在系统头文件上产生裸告警的字面量后缀检查。
6. **Linux 侧验证。** Ubuntu 24.04.5 + GCC 13.3.0 + CMake 3.28.3 +
   clang-tidy 18.1.3：`cmake` + `ctest` 7 个测试全过；`./check.sh ci`、
   `style` 通过；`audit` 的 tidy（7 个 TU，0 错 0 警）、CPD、Valgrind 通过，
   ASan 默认启用（本 PRoot 容器环境阻塞，见第 5 节）。
