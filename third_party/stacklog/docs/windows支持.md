# Windows 支持

本文记录 stacklog 在 Windows 上的支持范围、验证证据、实现要点和已知限制。Windows 10
22H2 的实测数据截至 2026-11-17；Windows 11 的数据保留为 2026-11-15 的历史记录并单独
标注。之后若有行为变化，需要重新验证并更新本文件。

## 1. 验证矩阵

| 能力 | Linux | Windows | 说明 |
| --- | --- | --- | --- |
| CMake 构建（静态库） | ✅ | ✅ | Win10 22H2：MSYS2 UCRT64 g++ 16.2 与 mingw64（MSVCRT）g++ 16.2 均构建通过；MSVC 19.51 为 2026-11-15 历史记录 |
| 单元测试（CTest） | ✅ 1/1 | ✅ 1/1 | 假平台端口单元测试；Win10 上 UCRT64 与 mingw64 构建均通过 |
| 集成测试（CTest） | ✅ 11/11 | ✅ 11/11 | 9 条真实文件系统复合流程 + `stacklog_install`（install + find_package + consumer）+ `stacklog_qmake`（qmake + Qt 工作线程示例）；Windows 使用 Qt 6.12 |
| 冒烟测试（CTest） | ✅ 2/2 | ✅ 2/2 | `stacklog_smoke_logging_flow`（完整日志/追踪/轮转）与 `stacklog_smoke_recovery_flow`（失败恢复） |
| 严格编译告警（GCC 口径） | ✅ 0 告警 | ✅ 0 告警 | Linux 门禁用 `-Wall -Wextra -Wpedantic`；Windows 用 `./check.sh windows` 的完整严格集 + `-Werror` |
| 静态检查（clang-tidy） | ✅ 0 告警 | ✅ 0 告警 | Windows 上由 `cpp_audit -c audit.toml all` 执行（clang-tidy 23.1.5） |
| Sanitizer（ASan） | ✅ 默认启用；普通 Linux 实测通过 | ❌ 不适用 | 2026-11-17 在 Ubuntu 24.04 x86_64 / GCC 15.2 上以 `-fsanitize=address` 构建并跑通 12 个 unit/integration/smoke 测试；本次 honor 的 PRoot 容器环境阻塞（见 `reports/asan.txt`），属特例 |
| Valgrind | ✅ 通过 | ❌ 不适用 | 仅 Linux；Windows 上 `cpp_audit` 自动跳过 |
| CPD 重复代码检查 | ✅ 通过 | ✅ 通过 | Windows 上由 `cpp_audit` 执行 |
| 格式/命名规范（standardize） | ✅ 0 改动 | ✅ 0 改动 | Windows 上由 `check.py style` 执行（clang-format 23.1.5 + clang-tidy 23.1.5） |
| qmake 集成 | ✅ | ✅ | Win10 UCRT64：Qt 6.12 qmake6 + g++；测试内部隔离 PATH/Qt 运行库；MSVC + qmake 未验证 |
| 包管理器集成（vcpkg/Conan） | ➖ 无 | ➖ 无 | 项目不提供包管理器清单，只有 CMake install 与 qmake `.pri` |
| 可选 Windows 交叉编译检查 | ✅ `./check.sh windows` | ➖ 不适用 | 在 Linux 上用 mingw-w64 交叉编译 Windows 分支（不进入 `all`） |

✅ = 通过；❌ = 未通过或未提供该检查；➖ = 该项不适用或项目不提供；⚠️ = 环境阻塞，未取得通过结论。

## 2. 已验证环境

| 项目 | Linux | Windows |
| --- | --- | --- |
| 操作系统 | Ubuntu 24.04.5 LTS（aarch64） | Windows 10 22H2 10.0.19045（x64） |
| 编译器 | GCC 13.3.0（另验证 Clang 18.1.3） | MSYS2 UCRT64 g++ 16.2.0；MSYS2 mingw64（MSVCRT）g++ 16.2.0 |
| CMake | 3.28.3 | 4.5.0 |
| 其它工具 | Ninja 1.11.1、clang-format/tidy 18.1.3、Valgrind 3.22.0、Qt 6.12.0 qmake、Python 3.12.3 | Ninja 1.13.2、Qt 6.12.0（UCRT64 qmake6）、clang-format/tidy 23.1.5、Cppcheck 2.22.0、Python 3.12.10 |
| 验证日期 | 2026-11-17 | 2026-11-17（Windows 11 记录为 2026-11-15） |

历史记录：2026-11-15 在 Windows 11 专业版 10.0.26300（x64）上验证过 MSYS2 UCRT64
g++ 16.2.0、CMake 4.5.0、Ninja 1.13.2、Qt 6.12.0 与 MSVC 19.51.36256（Visual Studio 18
2026，工具集 14.51.36231）。Windows 生成器：MinGW 用 `Ninja`；MSVC 用 Visual Studio 18
2026（多配置，Release/Debug 分别验证）。项目自身不依赖 Python；`check.sh` 调用的
`cxx_ci` / `cxx_audit` 是仓库外的门禁工具（Python 3）。

ASan 默认启用；2026-11-17 另在普通 Linux（Ubuntu 24.04 x86_64 / GCC 15.2）上以
`-fsanitize=address` 构建并跑通 12 个 unit/integration/smoke 测试（不含需要真实
qmake 的示例构建）。honor 的 PRoot 容器无法初始化 ASan，属容器环境特例。

## 3. 平台实现要点

1. **平台层与核心分离**：`src/stacklog.cpp` 是平台无关核心（不含任何 `_WIN32` 平台宏）。
   内部端口 `src/platform.hpp` + `src/platform_posix.cpp` / `src/platform_win32.cpp` 覆盖：
   路径存在/大小/逐级建目录/删除/改名、追加写文件句柄、UTF-8 路径预校验、路径拼接、
   本地时间戳。Windows 适配器用 Win32 原生 API 与宽字符路径（UCRT/MSVCRT 都支持
   `\\?\` 扩展路径）；POSIX 适配器用 `stat`/`mkdir`/`rename`。两平台共享的遍历、
   时间格式化、路径拼接与 ofstream 句柄放在 `src/platform_dirs.hpp`、`platform_time.hpp`、
   `platform_paths.hpp`、`platform_stream.hpp`（内部头，不安装）。
2. **路径拼接与根路径**：Windows 上 `join_path` 使用反斜杠（`\\?\` 扩展长度路径不做
   `/` 到 `\` 的归一化）；`make_dirs` 显式识别盘符根（`C:`、`\\?\C:`）与 UNC 共享根，
   只校验存在性，不对根调用 mkdir，其余层级逐级创建。
3. **轮转语义对齐**：Windows 的 rename 不允许目标已存在，所以轮转时先删除目标归档再
   改名（`remove_file` → `rename_file`），与 POSIX 的可覆盖语义对齐；归档失败仍保持
   "继续写当前文件、稍后重试"的既有策略。
4. **UTF-8 约定前置校验**：Windows 上 `init()` 在创建目录之前校验整条路径的
   UTF-8→UTF-16 转换，非法 UTF-8 直接返回 false 且不会留下目录。
5. **时间与大小口径**：时间戳在 Windows 用 `localtime_s`、POSIX 用 `localtime_r`；
   文件大小检查在 Windows 使用 `GetFileAttributesExW` 返回的 64 位大小
   （`ULARGE_INTEGER` 组合高低位），避免超过 2 GiB 的日志让检查点失真。
6. **构建与测试适配**：MSVC 目标由 CMake 加 `/W4 /utf-8`（不加 `/WX`，避免第三方头
   文件告警阻断构建）；测试辅助 `tests/support/test_support.hpp` 在 Windows 上用
   `GetTempPathW`、宽字符文件 IO、`GetFileAttributesW` / `CreateDirectoryW` /
   `RemoveDirectoryW` / `DeleteFileW` 做路径查询与清理，路径字符串仍按 UTF-8 解释。
7. **qmake 集成测试自隔离**：`tests/qmake_test.cmake` 从 `QMAKE_EXECUTABLE` 推导 Qt bin
   目录与 MSYS2 前缀，在 qmake / make / 示例运行的子进程里把 Qt bin 目录置顶 `PATH`，
   并优先选同一环境的 `mingw32-make` / `usr/bin/make`；调用者 shell 的 PATH 顺序不再
   决定 qmake 示例使用哪套 Qt/运行库。
8. **测试分层与覆盖**：`tests/unit/test_stacklog.cpp` 只链接平台无关核心与
   `tests/support/fake_platform.cpp`（假端口，可编排失败/写失败/rename 失败/时间/UTF-8
   错误），集成测试链接真实适配器与真实文件系统，冒烟测试跑完整业务线；CTest 用
   `LABELS unit|integration|smoke` 分级。`tests/coverage/run_coverage.py` 用 gcov JSON
   实测公开头中的公开函数与公开分支（当前 16/16、58/58），平台适配器属内部端口，
   不在公开接口统计内。

## 4. 如何验证

Linux 完整门禁，以及可选的 Windows 交叉编译检查：

```bash
./check.sh all
./check.sh windows   # 需要 mingw-w64；未安装时打印跳过信息并返回 0
```

`./check.sh windows` 使用严格警告集
（`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wcast-qual`
加 `-Werror`）交叉编译库和全部测试，不进入 `all`。

覆盖率门禁（Linux，gcov JSON；当前 16/16 公开函数、58/58 公开分支）：

```bash
python3 tests/coverage/run_coverage.py
```


Windows 10/11 + MSYS2 UCRT64（cmd；PATH 只放单一 MSYS2 环境，推荐 UCRT64）：

```bat
set "PATH=C:\msys2\ucrt64\bin;C:\msys2\usr\bin;C:\Users\<user>\AppData\Local\Programs\Python\Python312\Scripts;C:\Users\<user>\AppData\Local\Programs\Python\Python312;%PATH%"
cmake -S . -B build-win -G Ninja -DCMAKE_CXX_COMPILER=C:/msys2/ucrt64/bin/g++.exe
cmake --build build-win
ctest --test-dir build-win --output-on-failure
```

Windows 门禁（`cxx_ci`、`cxx_audit` 与 stacklog 同级检出）：

```bat
python C:\path\to\cxx_ci\check.py ci --root .
python C:\path\to\cxx_ci\check.py style --root .
set "PYTHONPATH=C:\path\to\cxx_audit\src"
python -m cpp_audit -c audit.toml all
```

`check.py ci` 在 Windows 上跑完整 CTest（含 install / qmake）；`check.py style` 执行
clang-format / clang-tidy；`cpp_audit all` 在 Windows 上执行 tidy 与 CPD，ASan / Valgrind
由工具按平台跳过。style 门禁对 clang-format / clang-tidy 大版本敏感，升级后需重跑复核。

Windows + MSVC（Visual Studio 18 2026 生成器，多配置，历史验证路径）：

```powershell
cmake -S . -B build-msvc -G "Visual Studio 18 2026" -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

可选：配置时加 `-DSTACKLOG_BUILD_BENCHMARKS=ON` 会额外构建 `stacklog_bench`。

## 5. 已知边界与限制

- 已实测 Windows 10 22H2（10.0.19045）x64 + MSYS2 UCRT64 / mingw64；Windows 11 x64 的
  MinGW 与 MSVC 记录来自 2026-11-15。未验证 32 位（x86）、其它 MSVC 版本与其它 Windows
  版本。
- 同一个 PATH 不要混用多套 MSYS2 环境（mingw64 + ucrt64）：两套运行库（MSVCRT/UCRT）
  与 Qt 组件混用会出现 `0xC0000139`、堆损坏等启动期错误。`stacklog_qmake` 测试已做
  PATH/Qt 运行库隔离，但其余测试可执行文件仍按调用者 PATH 加载运行库。
- Windows 上“外部删除/替换打开中的日志后自动恢复”不适用：Windows 不允许删除仍被
  打开的文件，该恢复路径仅 POSIX 可用（对应用例在 Windows 上跳过）。
- UNC 共享根路径只做了代码推演，没有真机验证（本机没有可用共享，建共享需要管理员
  权限）。
- Windows 上 ASan / Valgrind 不适用：`cpp_audit` 按平台跳过（ASan 无 Windows 实现路径，
  Valgrind 不支持 Windows）。Windows 的静态质量由 `cpp_audit` 的 clang-tidy + CPD、
  `check.py style` 和 `./check.sh windows` 的编译期严格告警（`-Werror`）共同保证。
- `audit.toml` 的 ASan 默认启用；普通 Linux 已实测通过。当前 honor 的 PRoot 容器
  中 ASan 运行期被地址空间限制阻塞（`reports/asan.txt`），属容器特例，不是代码
  问题；仅在该受限容器临时验证时可把 `[asan].enabled` 改为 `false`，不要提交为
  默认值。Valgrind 正常通过。
- style 门禁结果与 clang-format / clang-tidy 大版本相关：本次 Linux 18.1.3、Windows
  23.1.5。源码是这两个版本的共同固定点；换到其它大版本后可能出现纯格式差异。
- 覆盖率只统计公开头 `include/stacklog/stacklog.hpp` 中的公开函数/方法/类成员；平台
  适配器是内部端口，不在公开接口统计内，由集成/冒烟测试覆盖。白名单为空：
  `log_line` 析构 `catch(...)` 的异常路径由 fake 端口注入 `write_line` 抛异常的
  单元测试覆盖（`tests/unit/test_stacklog.cpp`）。
- `\\?\` 扩展长度路径必须使用反斜杠；未加前缀且超过 `MAX_PATH` 的路径仍受 Windows
  限制。扩展路径的查询/删除/改名/建目录走 Win32 原生 API，UCRT 与 MSVCRT 均支持。
- 路径按 C 字符串处理，含嵌入 NUL 时会在 NUL 处截断（POSIX 与 Windows 行为一致）；
  使用方不应传入含 NUL 的路径。
- Windows 的 qmake 集成只用 MSYS2 UCRT64 的 qmake（Qt 6.12）+ g++ 验证；MSVC + qmake
  组合未验证。mingw64 环境本机没有 Qt6，其 qmake 集成测试实际调用 UCRT64 的 qmake6。
- MSVC Debug 可执行文件依赖本机 Visual Studio 提供的 debug CRT 运行环境；本次验证
  机安装了 VS，未在无 VS 的环境复测。
- 多进程同时写/轮转同一个日志文件不在支持范围（见 README 设计边界），该限制在
  Windows 上同样适用。
- `./check.sh windows` 只证明 Windows 分支“能编译且无警告”，不证明运行期行为；
  运行期正确性由 Windows 真实构建 + CTest 验证。

## 6. 审计修复记录（2026-11）

Windows 支持初版是 `597bbbb`（UTF-8 路径转宽字符、文件轮转、CMake/CTest 适配）。
随后对抗式自审与交叉审计发现以下问题：`bff8c9e` 修自审 P0/P1/P2，`fd37e1f` 补 MSVC
支持，`ae17172` 增加 Windows 交叉编译门禁。

| 问题 | 机制（为什么错） | 修法 | 提交号 |
| --- | --- | --- | --- |
| `base_name` 含非法 UTF-8 时 `init` 失败却残留空目录 | `init()` 先按 `cfg.dir` 调用 `make_dirs`，基名不参与建目录；整条路径直到 `open` 才做 UTF-8→UTF-16 转换并失败，目录已经建好 | 在 `make_dirs` 之前校验 `join_path(dir, base_name + ".log")` 的转换，非法直接返回 false | `bff8c9e` |
| 盘符根 `"C:"` / `"C:\"` 被直接传给 `_wmkdir` | `make_dirs` 去掉末尾分隔符后得到 `"C:"`；`_wstat64("C:")` 返回 ENOENT，于是调用 `_wmkdir("C:")`，仅靠 UCRT 返回的 `EEXIST` 兜底，依赖未文档化行为 | 新增 `is_drive_root` / `is_unc_root`：盘符根和 UNC 共享根只校验存在性，不再调用 mkdir | `bff8c9e` |
| `\\?\` 扩展长度路径能建目录、却打不开日志文件 | `join_path` 固定用 `/` 拼接；`\\?\` 前缀禁用 Windows 路径归一化，`/` 不会被转换，整条路径打开失败，`init` 返回 false 且已留下目录 | Windows 上 `join_path` 改用 `\`；UCRT 上确认 `_wmkdir` / `_wstat64` / `_wrename` / `_wremove` 和 ofstream 宽字符重载都支持 `\\?\` 路径，并用测试覆盖初始化、写入、轮转（2026-11-17 发现 MSVCRT 的 `_wstat64` 不支持，见下节修复） | `bff8c9e` |
| Windows 分支的 clang-tidy 告警在 Linux 门禁里永远不可见 | Linux 的 clang-tidy 只分析当前平台能编译的分支，`#ifdef _WIN32` 代码不参与编译；`&wide[0]`（C++11 的 `data()` 只读，必须取地址）在 Windows clang-tidy 下报 `readability-container-data-pointer` | 行内 `NOLINT(readability-container-data-pointer)` 并注明 C++11 限制；新增 `./check.sh windows` 交叉编译门禁把 Windows 分支纳入告警检查（`-Werror`） | `bff8c9e`、`ae17172` |
| qmake 集成测试在 Windows 上残留空组织目录 | 测试只删除 `%LOCALAPPDATA%\stacklog\stacklog-qt-worker-demo` 的应用目录，Qt 同时创建的组织级目录 `...\stacklog` 留下，污染真实用户目录 | 删除应用目录后，组织目录为空时一并删除 | `bff8c9e` |
| MSVC 直接构建会按系统代码页解析源码 | 源文件是 UTF-8（含中文注释和错误文本）且没有 BOM；MSVC 默认按系统 ACP 解释源码，CP936 机器上会乱码并报 C4819（本机 ACP 恰好是 UTF-8，掩盖了该问题） | MSVC 目标由 CMake 加 `/utf-8`，固定源码与执行字符集 | `fd37e1f` |
| MSVC 多配置生成器下安装/集成测试失败（LNK2038） | `ctest -C Release` 时 `cmake --install` 和 consumer build 没有传配置名，consumer 默认用 Debug 构建（`/MDd`、`_ITERATOR_DEBUG_LEVEL=2`）去链接 Release 的 `stacklog.lib`（`/MD`、0），运行库级别不匹配 | 用 `$<CONFIG>` 把 ctest 选中的配置传给 install 与 consumer build，并区分单配置/多配置生成器 | `fd37e1f` |
| MSVC `/W4` 下基准程序 `std::strerror` 告警 C4996 | `strerror` 是 MSVC 标记的弃用函数，`/W4` 下报警 | 改用 `std::error_code(errno, std::generic_category()).message()`，GCC/Clang 同样可用 | `fd37e1f` |

### 2026-11-17：Windows 10 实测修复

在 Windows 10 22H2 上按 mingw64 / UCRT64 环境复测时，CTest 出现 2 项失败：
`stacklog_utf8_path` 断言 `\\?\` 扩展路径下的日志文件不存在；`stacklog_qmake` 运行
Qt 示例退出 `0xC0000139`（entry point not found）。定位与修复如下：

| 问题 | 机制（为什么错） | 修法 | 证据 |
| --- | --- | --- | --- |
| `\\?\` 扩展路径被 MSVCRT 误判不存在 | UCRT 的 `_wstat64` 支持扩展路径，但 MSVCRT 运行时（mingw64）的 `_wstat64` 对 `\\?\` 路径返回 `ENOENT`；产品 `path_exists`/`regular_file_size` 与测试辅助 `_wstat`/`_wremove`/`_wrename`/`_wmkdir` 都受影响，导致真实文件被当成不存在 | 产品与 `tests/support/test_support.hpp` 改用 Win32 原生 API：`GetFileAttributesW`、`GetFileAttributesExW`、`CreateDirectoryW`、`DeleteFileW`、`MoveFileW`；大小用 `ULARGE_INTEGER` 组合高低位 | 探针：同一扩展路径 `_wstat64=-1 (errno=2)`，`GetFileAttributesExW=1`；修复后 mingw64 构建的 `stacklog_utf8_path` 由失败转为通过 |
| qmake 示例在混合 PATH 下 `0xC0000139` | 调用者 PATH 混用 mingw64/ucrt64 时，`qmake6` 来自 UCRT64（Qt6 为 UCRT），生成 Makefile 后 make/g++ 却可能来自 mingw64（MSVCRT），Qt 与 C++ 运行库不匹配 | `qmake_test.cmake` 从 `QMAKE_EXECUTABLE` 推导 Qt bin 目录，qmake/make/示例子进程都把该目录置顶 PATH，并优先选同一 MSYS2 环境的 make | 修复前：UCRT64 构建 + mingw64 优先 PATH → qmake 测试 `0xC0000139`；修复后同一混合 PATH 下 14/14 通过 |
| style 门禁在 clang-format/tidy 18 与 23 之间漂移 | Linux 为 LLVM 18.1.3，Windows 为 23.1.5；同一 `.clang-format`/`.clang-tidy` 下 23 会补 C++11 枚举尾逗号、去掉空 lambda `()`、把 `&s[0]` 改成 `data()`、把 `"|"` 改成 `'|'` 等，导致 Windows `check.py style` 报 5 个文件改动 | 源码对齐到两个版本都不再改动的固定点：补枚举尾逗号、去掉 lambda 空 `()`、采用 tidy 的 `return {}`/`'|'` 建议；C++11 无可写 `data()` 的两处改用带 `NOLINT(readability-container-data-pointer)` 的 `writable_buffer()` helper；`cpp_audit` 的 `bugprone-signed-bitwise` 用 `ULARGE_INTEGER` 消除 | Linux `check.py style` 0 改动；Windows `check.py style` 由 5 文件改动变为 0 改动 |
| Windows clang-tidy `bugprone-signed-bitwise` | `(uint64)high << 32 | low` 中移位常量 `32` 为有符号，Windows clang-tidy 23 报警 | 改用 `ULARGE_INTEGER` 的 `HighPart`/`LowPart` 组合 64 位大小 | Windows `cpp_audit` tidy：11 个文件 0 错误 0 告警 |

原始 2 项失败在“UCRT64 干净 PATH”下不复现；复现与修复表明根因是**运行库/Qt 混用**
（qmake）与**MSVCRT 的 CRT 对扩展路径的限制**（utf8/test 辅助），不是日志功能本身的
跨平台缺陷。测试未做跳过或弱化：修复后 UCRT64 与 mingw64 构建都真实跑完 14/14 CTest。
