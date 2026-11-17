# 质量门禁（cxx_ci + cxx_audit）

本文件是 real-esrguisoft 的质量基建说明：构建/测试流水线、排版规范化、静态检查。
三套工具都**不随本仓库提交**，而是与工具仓库同级放置、按相对路径调用：

```text
<工作区>/
├── real-esrguisoft/        # 本仓库（所有门禁命令都在仓库根执行）
├── cxx_ci/                 # 构建/测试流水线 ci + 排版 standardize
└── cxx_audit/              # 静态检查 cpp_audit（Python 包名 cpp_audit）
```

需要的运行时：

| 工具 | 入口 | Python | 主要外部依赖 |
| --- | --- | --- | --- |
| cxx_ci/ci | `../cxx_ci/check.py ci --root .`（配置由 `ci.toml` 承载） | 3.4+ | cmake、ninja、ctest、编译器（Qt/FFmpeg 见构建文档） |
| cxx_ci/standardize | `../cxx_ci/check.py style --root .`（配置由 `standardize.toml` 承载） | 3.4+ | clang-format（Windows 需自行安装，见 §5） |
| cxx_audit | `../cxx_audit/src` 的 `cpp_audit` 模块 | 3.11+ | compat 无依赖；tidy/cpd/asan/valgrind 各需对应工具（见 §6） |

三份配置都在仓库根：`ci.toml`、`standardize.toml`、`audit.toml`；排版规则在
`.clang-format`，tidy 规则在 `.clang-tidy`。所有路径都相对**仓库根**解析，命令也
一律在仓库根执行——不要在 `src/` 或构建目录里跑这些命令。

---

## 1. 一键命令

### 1.1 Windows（PowerShell + MSYS2 UCRT64，目标环境）

```powershell
# 0) 工具链进 PATH（按本机 MSYS2 安装位置调整）
$env:Path = 'C:\msys64\ucrt64\bin;' + $env:Path

# 1) 构建 + 测试流水线：clean -> configure -> build -> test
python ..\cxx_ci\check.py ci --root .

# 2) 排版检查（有 diff 时退出码 1，只报告、不改文件）
python ..\cxx_ci\check.py style --root .

# 3) 静态检查（本机可跑的部分：compat）
$env:PYTHONPATH = '..\cxx_audit\src'
python -m cpp_audit -c audit.toml all
```

### 1.2 Linux（本机开发/评审环境）

```bash
# 1) 构建 + 测试流水线
python3 ../cxx_ci/check.py ci --root .

# 2) 排版检查
python3 ../cxx_ci/check.py style --root .

# 3) 静态检查
PYTHONPATH=../cxx_audit/src python3 -m cpp_audit -c audit.toml all
```

`cxx_ci` 与 `cxx_audit` 都支持只校验、不产生副作用的预检（推荐先跑）：

```bash
PYTHONPATH=../cxx_ci/src python3 -m cxx_ci.ci_tool -c ci.toml --check-config   # 配置/路径/工具
PYTHONPATH=../cxx_ci/src python3 -m cxx_ci.ci_tool -c ci.toml --dry-run        # 打印执行计划
PYTHONPATH=../cxx_audit/src python3 -m cpp_audit -c audit.toml check-config
PYTHONPATH=../cxx_audit/src python3 -m cpp_audit -c audit.toml --dry-run all
```

> Linux 上同一条命令也会跑通 `test` 阶段：`resr_tests` 中依赖 Windows 版
> realesrgan 引擎 exe 的 4 条原生链路用例会自动 `QSKIP`（测试内注明“Windows PE
> 无法在当前平台运行”），其余用例与 `resr_media_tests` 全部通过；ci 阶段整体
> 退出码仍为 0。只跑媒体层测试可用：
>
> ```bash
> ctest --test-dir build/ci -R resr_media_tests --output-on-failure
> ```
>
> **引擎用例默认不跑；显式开启时必须串行**：默认 `ctest` 中真实引擎用例因未设置
> `RESR_TEST_ENGINE=1` 全部 `QSKIP`（§2.1）。只有在显式
> `-DRESR_BUILD_ENGINE_TESTS=ON` + `RESR_TEST_ENGINE=1` 时才跑；此时不要用
> `ctest -j` / 并行跑多个引擎用例：`ExeRunner` 全局串行 gate 只覆盖同一进程，
> 不覆盖并行 ctest 进程，为降低 TDR 风险测试/验收脚本应一次只跑一个引擎用例。
>
> TDR 加固默认参数（task-34）：图块总是显式 `-t`（自动=256，范围 32–1024）、
> 线程默认 `1:1:2` 且总是显式 `-j`、引擎崩溃时服务层用 `-t 128 -j 1:1:1`
> 自动重试一次；**视频帧按 50 帧/批启动独立引擎进程**（对齐旧 Python 版
> `image_batch.py`），批间走同一全局串行 gate；不要改回整段帧目录一次性调用。
> 详见 `.cxx-review/out/real-esrguisoft/windows-code-review/engine-tdr-mitigation.md`。

---

## 2. `ci.toml`：构建与测试流水线

流水线固定为 `clean -> configure -> build -> test` 四个阶段，任何阶段失败即退出
非零；`test` 阶段会尽量跑完所有用例再汇总。

| 配置 | 值 | 说明 |
| --- | --- | --- |
| `build_system` | `cmake` | 唯一构建入口是根 `CMakeLists.txt` |
| `build_dir` | `build/ci` | clean 阶段整个删除重建；与手工 `build/` 隔离 |
| `configure_args` | `-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` | 显式 Ninja：Windows 上 CMake 默认可能落到 nmake/VS 生成器（交接文档 §8.1-1 的坑）；导出编译数据库供 IDE 与 clang-tidy 复用（见 §6.1） |
| `[env] CMAKE_PREFIX_PATH` | `C:/msys64/ucrt64` | Windows UCRT64 的 Qt/依赖前缀；Linux 上该路径不存在会被 CMake 忽略（已实测） |
| `[tools] make` | `mingw32-make` | CMake+Ninja 自动构建不使用 make；显式写掉 Windows 默认的 `nmake` 坑 |
| `[test] framework` | `ctest` | 测试由根 `CMakeLists.txt` 的 `RESR_BUILD_TESTS` 注册（默认 ON）；真实 GPU 引擎用例默认不注册/不运行（见下方“引擎用例”）；`ci.toml` 的 `args = ["-LE", "engine"]` 再排除 LABELS engine |
| `[output].dir` | `build/ci-logs` | 完整日志落盘；控制台按 `warnings`/`failures` 降噪 |

### 2.1 真实 GPU 引擎用例（默认不跑）

- `resr_tests` 中的真实引擎/视频链路用例默认 **QSKIP**：需要 `RESR_TEST_ENGINE=1`
  显式开启；Windows 上还要求最低 Vulkan/GPU 能力探测通过（加载 `vulkan-1.dll`
  并枚举物理设备，不启动引擎），否则给出明确跳过原因（静态审查 A1/B4）。
- 默认 ctest 不注册 `LABELS engine` 用例；`ci.toml` 的 `args = ["-LE", "engine"]`
  是第二道保险。**CI/普通开发机不要跑真实引擎**（本机环境曾因引擎触发 nvlddmkm
  153/TDR 风暴）。
- 确实需要跑引擎冒烟时（换机器/环境隔离后），显式构建并运行：
  ```bash
  cmake -S . -B build/engine-tests -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DRESR_BUILD_TESTS=ON -DRESR_BUILD_ENGINE_TESTS=ON
  cmake --build build/engine-tests --parallel
  RESR_TEST_ENGINE=1 ctest --test-dir build/engine-tests -L engine --output-on-failure
  ```
  或直接 `RESR_TEST_ENGINE=1 build/engine-tests/tests/resr_tests <测试函数名>`；
  仍须遵守 `engine-tdr-mitigation.md` 的串行/计数/停止协议。

构建/测试依赖：**FFmpeg 开发库是必需项**（`cmake/MediaFFmpeg.cmake` 中
`find_package(FFmpeg REQUIRED)`），缺失时 configure 阶段直接失败；源码中
`!RESR_HAS_MEDIA` 的占位实现属历史遗留、当前不可达（静态审查 C4，清理需改 src）。

常用变体：

```bash
PYTHONPATH=../cxx_ci/src python3 -m cxx_ci.ci_tool -c ci.toml --steps clean,configure,build  # 只构建
PYTHONPATH=../cxx_ci/src python3 -m cxx_ci.ci_tool -c ci.toml --jobs 8                      # 覆盖并行度
```

失败处理：

- 退出码 `2`：配置/用法/工具缺失（Qt 找不到通常就是这一档），先看控制台预检信息。
  确认已把 `C:\msys64\ucrt64\bin` 加进 PATH，或按本机安装位置修改 `ci.toml` 的
  `[env] CMAKE_PREFIX_PATH`。
- 退出码 `1`：编译或测试失败。完整输出在 `build/ci-logs/*.log`（configure/build/test）。
- 换生成器或 Qt 版本后 configure 报缓存冲突：clean 阶段会删 `build/ci`，直接重跑即可。
- 只想保留一次现场排查：`--steps build` 之前先手工 `cmake -S . -B build/ci ...`。

`docs/ci.md` 之外不再维护第二份构建命令；手工构建（IDE、`cmake --preset` 等）见
`README.md` 与交接文档 §4。

---

## 3. `standardize.toml` + `.clang-format`：排版

- 扫描范围：`main.cpp`、`src/`、`tests/`；排除 vendored 的 `third_party/`、
  资源、素材与所有构建目录。
- 只做 clang-format 排版（`tidy = false`）；clang-tidy 归 `cxx_audit` 管（§4），
  职责不重叠，也避免 Windows 上因缺 tidy/编译数据库而误报。
- 排版规则从现有源码反推：Tab 缩进（显示宽度 4）、Allman 大括号、返回值独占
  一行、`const` 在类型前且 `*`/`&` 靠变量名、80 列换行、二元运算符换行前置、
  不自动排 include、不重排注释。详见 `.clang-format` 内注释。
- 中间产物（diff、暂存区）写在 `build/standardize/` 下，不污染仓库根。
- **全仓排版已收口**（2026-11-14）：104 个源文件全部满足 `.clang-format`，
  `--check` 应为 `changed: 0` / rc 0。若再次出现 diff，说明有新代码未按
  规则格式化，按下面工作流处理即可。收口时对 `.clang-format` 做过一处微调：
  `BreakStringLiterals: false`（不把超长中文字符串自动拆成相邻字面量拼接，
  避免格式化改动字符串字面量）。

工作流：

```bash
# 1) 检查：有 diff 退出码 1（CI/门禁用这一条，只读源码）
python3 ../cxx_ci/check.py style --root .

# 2) 生成 diff 供复核（默认只生成、不修改源码）
PYTHONPATH=../cxx_ci/src python3 -m cxx_ci.standardize_tool
#    -> build/standardize/standardize.diff

# 3) 复核后应用；应用时会检测源文件是否在暂存后被改过
PYTHONPATH=../cxx_ci/src python3 -m cxx_ci.standardize_tool --apply
```

退出码：`0` 无需修改；`1` `--check` 模式存在 diff；`2` 工具/配置/文件处理失败；
`130` 用户中断。

失败处理：

- 退出码 `2` 且提示找不到 `clang-format`：Windows 上安装 MSYS2 的 clang 工具
  （`pacman -S mingw-w64-ucrt-x86_64-clang`，见 §5），或把 `standardize.toml` 的
  `clang_format` 指向绝对路径。
- 退出码 `1`：说明仓库出现了未格式化的改动（收口后正常应为 0）。先跑第 2 步
  看 diff、确认只是排版后 `--apply`；不要只对单个文件 `--apply`，避免制造
  半格式化状态。
- `--apply` 冲突：说明暂存后源文件又被修改，重新跑第 2 步再 apply。

---

## 4. `audit.toml` + `.clang-tidy`：静态检查

`cpp_audit` 的检查项与当前状态（详见 `audit.toml` 内逐项注释）：

| 检查 | 本仓库状态 | 含义 | Windows 不可用原因 |
| --- | --- | --- | --- |
| `compat` | ✅ 启用 | 文本级兼容性规则（本项目保留 `variadic_template` 作回归护栏） | — |
| `tidy` | ⛔ `enabled=false` | clang-tidy 检查（规则在根 `.clang-tidy`） | MSYS2 UCRT64 默认不带 clang-tidy |
| `cpd` | ⛔ `enabled=false` | PMD CPD 重复代码检查 | 需要 npm/Java 工具链 |
| `asan` | ⛔ `enabled=false` | AddressSanitizer Debug 构建 + 运行 | MSYS2 GCC libasan 与 Qt/FFmpeg DLL 混用不稳定 |
| `valgrind` | ⛔ `enabled=false` | Valgrind Memcheck/Helgrind 运行 | Windows 没有 valgrind |

`all` 只跑启用的检查；被禁用的检查仍可用**显式子命令**强制运行（Linux/WSL，
见 §6），所以 `enabled=false` 不会把工具能力关死，只是不进入 Windows 门禁。

`compat` 规则选型说明：交接文档 §8.1-1 建议 `rules = ["std_concurrency"]`，但该
规则当前会命中 4 条既有代码：

```text
src/infrastructure/media/ffmpegutils.cpp:3   #include <atomic>
src/model/videoprocessservice.cpp:25         #include <atomic>
src/model/videoprocessservice.cpp:652/653    std::atomic<bool> 成员
```

在把它们换成 `QAtomicInt`（或去掉 `<atomic>`）之前启用会让门禁永久变红，因此
当前改为启用 0 命中的 `variadic_template` 作回归护栏。清理上述 3 处后，把
`audit.toml` 的 `rules` 改为 `["variadic_template", "std_concurrency"]` 即可。

失败处理：

- 退出码 `2`：配置/路径/工具问题（例如 `sources` 路径不存在、显式跑 tidy 时
  缺 `.clang-tidy`）。先跑 `check-config` 看预检信息。
- 退出码 `1`：确有检查失败。报告在 `build/audit/reports/`：
  `summary.txt` / `summary.json` 是汇总，`compat.txt`、`clang-tidy.txt`、
  `cpd.txt`、`asan.txt`、`valgrind.txt` 是各检查正文（启用哪项就有哪个文件）。
- `SKIPPED` 不是失败：机器上缺该检查的可选工具（如没装 pmd/valgrind），本次没跑成。

---

## 5. Windows 与 Linux 的差异、降级

| 能力 | Windows 10/11 + MSYS2 UCRT64（目标） | Linux（本机） | 降级/替代 |
| --- | --- | --- | --- |
| `ci_tool` clean/configure/build | 支持（Ninja + UCRT64） | 支持（系统 Qt6/FFmpeg） | — |
| `ci_tool` test | 全部用例可跑（`resr_tests` + `resr_media_tests`） | 跑通；`resr_tests` 中 4 条依赖 Windows 引擎的链路用例 `QSKIP` 并注明原因，其余通过 | 只跑媒体层：`ctest -R resr_media_tests` |
| `standardize_tool` | 需自行安装 clang-format（见下） | 系统 clang-format 即可 | MSYS2：`pacman -S mingw-w64-ucrt-x86_64-clang`（clang-format 随 clang 包提供；若包名随镜像不同，用 `pacman -F clang-format` 查） |
| `cpp_audit compat` | 支持（纯 Python 文本扫描） | 支持 | — |
| `cpp_audit tidy` | 默认关闭；装 `clang-tools-extra` 后可显式跑 | 可用 | 见 §6.1 |
| `cpp_audit cpd` | 默认关闭（缺 PMD） | 可用（需 `pmd-bin`） | 见 §6.2 |
| `cpp_audit asan` | 默认关闭 | 可用（需 clang++ + libasan） | 见 §6.3 |
| `cpp_audit valgrind` | 不可用（工具不存在） | 可用 | 见 §6.4 |

**降级原则**：Windows 门禁只跑“本机确实能跑真结果”的检查，缺工具的检查显式
`enabled=false` 并注明原因，而不是留在 `all` 里靠 `SKIPPED` 蒙混——这样门禁
全绿时能确定它真的检查过东西（`all` 一个检查都没启用会以退出码 2 报错，不会
空跑通过）。

---

## 6. Linux/WSL 补跑 Windows 上缺的检查

被 `enabled=false` 的检查可以用显式子命令强制运行。下面命令都在本仓库根执行，
非门禁项（结果供评审参考；如需纳入 CI，见每节的“纳入门禁”说明）。

### 6.1 clang-tidy

```bash
PYTHONPATH=../cxx_audit/src python3 -m cpp_audit -c audit.toml tidy
```

- 需要：`clang-tidy`（Ubuntu：`sudo apt install clang-tidy`；MSYS2：
  `pacman -S mingw-w64-ucrt-x86_64-clang-tools-extra`，若包名不同用
  `pacman -F clang-tidy` 查）。
- 首次运行会在 `build/audit/` 自动生成 `compile_commands.json`（CMake 项目，
  工具自动跑 configure，不会完整编译）。
- 规则来自根 `.clang-tidy`（`bugprone-*` + `performance-*`，`HeaderFilterRegex`
  只诊断 `src/`）。报告：`build/audit/reports/clang-tidy.txt`。
- 已在 Linux 实测可跑（clang-tidy 21）：直接按默认路径跑会报 1 个 error 与
  数十条 warning。error 来自 `src/model/videoprocessservice.cpp` 的
  `#include "videoprocessservice.moc"`——它是 CMake AUTOMOC 的构建产物，
  仅 configure 不生成，于是找不到该文件。先让 CI 完整构建一次
  （`ci.py --steps clean,configure,build`，`ci.toml` 已开
  `CMAKE_EXPORT_COMPILE_COMMANDS=ON`），再临时给 `audit.toml` 的 `[tidy]` 段加：

  ```toml
  compile_commands = "build/ci/compile_commands.json"
  ```

  之后 tidy 结果为 **0 error、41 warning**（实测），warning 属待清理项，未纳入门禁。
  用完删掉这行即可回到默认（`build/audit`）路径。
- 纳入门禁：把 `audit.toml` 的 `[tidy] enabled` 改为 `true`；若暂时只想看不
  阻断，把 `fail_on_warnings = false`，先清 error 级问题。

### 6.2 PMD CPD（重复代码）

```bash
# 需要 pmd（npm install -g --allow-scripts=pmd-bin pmd-bin，或发行版的 pmd 包）
PYTHONPATH=../cxx_audit/src python3 -m cpp_audit -c audit.toml cpd
```

- 报告：`build/audit/reports/cpd.txt`。当前 `min_tokens` 为默认 70，UI 面板
  代码重复度需先标定阈值，再考虑纳入门禁（`[cpd] enabled = true`）。
- 已在 Linux 实测可跑（本机装了 pmd）：按默认阈值会报出重复（退出码 1），
  属“待标定阈值 / 待去重”的后续清理项，不是配置问题。

### 6.3 ASan（内存错误）

`asan` 的默认 target 是 GUI 主程序，在 Linux 上需要能“正常退出”才能拿到泄漏
汇总。最省事的做法是让它跑测试可执行文件：临时在 `audit.toml` 的 `[asan]` 段加

```toml
targets = ["resr_media_tests"]   # CMake target 名
compiler = "clang++"
build_dir = "build/asan"
```

然后：

```bash
PYTHONPATH=../cxx_audit/src python3 -m cpp_audit -c audit.toml asan
```

- 需要：clang++、libasan（Ubuntu：`sudo apt install clang libasan...` 或直接用
  `g++` + `-fsanitize=address`；确认 `clang++ --version` 可用）。
- 报告：`build/audit/reports/asan.txt`。ASan 需要进程正常退出，不要 `kill -9`。
- 受限环境（容器/PRoot）里 ASan 运行期可能报 `AddressSanitizer: CHECK failed`，
  属环境限制，可改用 valgrind。

### 6.4 Valgrind

```bash
PYTHONPATH=../cxx_audit/src python3 -m cpp_audit -c audit.toml valgrind
```

- 与 asan 同理，建议临时把 `[valgrind].targets` 指向测试可执行文件
  （`resr_media_tests`），避免 GUI 生命周期 hook 的复杂度。
- 报告：`build/audit/reports/valgrind.txt`。数据竞争用
  `[valgrind] tool = "helgrind"`（或 `"drd"`）。
- WSL2 上直接 `valgrind ./build/ci/tests/resr_media_tests` 也可以，不经过
  `cpp_audit`；但 `cpp_audit` 会统一汇总报告，推荐前者。

---

## 6.5 Windows 打包（cxx_pkg 唯一入口）

打包统一由工作区同级的 **cxx_pkg**（0.3+，Windows/MSYS2 能力）完成，项目内不再维护
自写打包脚本（原 `tools/deploy.ps1` 已删除）。项目根目录的 `cxx_pkg.ini` 描述参数：
CMake 构建目录 `build-win`、target `real-esrguisoft`、`zip` 格式、`dist` 输出、附加
载荷 `bin/realesrgan`（引擎 exe + models）与 README/LICENSE、Qt 前缀
`C:/msys64/ucrt64`。

```bash
# Windows MSYS2 UCRT64，项目根目录；cxx_pkg 路径按本机实际位置调整
python C:/Users/yxl/Desktop/code/cxx_pkg/cxx_pkg.py -c cxx_pkg.ini --check-config
python C:/Users/yxl/Desktop/code/cxx_pkg/cxx_pkg.py -c cxx_pkg.ini --dry-run
python C:/Users/yxl/Desktop/code/cxx_pkg/cxx_pkg.py -c cxx_pkg.ini --list
python C:/Users/yxl/Desktop/code/cxx_pkg/cxx_pkg.py -c cxx_pkg.ini
```

- 产物：`dist/real-esrguisoft-<version>-win64.zip` + `SHA256SUMS`（`dist/` 已被
  `.gitignore`）；zip 自包含 Qt 运行时/插件、MinGW 与 FFmpeg/第三方 DLL（PE 导入闭包）、
  引擎与模型。
- 验收：zip 解压到**仓库外**（避开仓库目录受限令牌）后，`PATH` 仅含
  `C:\Windows\System32;C:\Windows` 也能启动并离屏截图；真实引擎链路需在机器环境
  隔离时另行验证。
- cxx_pkg 的 Linux 配置仍然有效（POSIX ELF 打包）；本项目的 Windows 产物不适用
  Linux 侧打包，Linux 构建/测试不受影响。
- 若 cxx_pkg 暴露通用缺口（DLL 闭包/插件/zip），优先在 cxx_pkg 修复并记录到
  `.cxx-review/out/real-esrguisoft/windows-code-review/cxx-pkg-gaps.md`，不在本仓库
  workaround。

---

## 7. 已知问题与本次未验证项

1. **Windows 侧已实测（2026-11-15）**：Windows + MSYS2 UCRT64（Qt 6.12.0、GCC 16.2、
   CMake 4.5.0、Ninja 1.13.2、FFmpeg 9.0 dev）上 `ci --check-config`/`--dry-run` 与
   `audit check-config`/`all` 通过；`standardize --check` 因 clang-format 版本差
   （Windows 23.1.5 vs Linux 21.1.8）报 4 个**未改动基线文件**差异，修复文件本身无差异
   ——见下方第 4 条。Linux 上完整 `clean/configure/build/test` 流水线与全量 ctest 均通过。
2. **`std_concurrency` 规则未启用**：见 §4 的 4 条既有命中；属于后续代码清理项。
3. **`tidy/cpd/asan/valgrind` 未纳入门禁**：Windows 缺工具，Linux 上为可选补跑
   （§6）；等 CI 环境固定后再决定是否纳入。tidy 的 AUTOMOC 前置条件见 §6.1。
4. **clang-format/tidy 版本（需固定）**：`.clang-format` 按 clang-format 20/21 编写
   （Linux 开发机 21.1.8，Windows MSYS2 为 23.1.5）。换用更老版本时
   `BreakAfterReturnType` 需改回旧名 `AlwaysBreakAfterReturnType`；不同大版本之间个别
   换行/对齐决策有差异——2026-11-15 Windows 真机实测 `--check` 在 4 个未改动基线文件上
   报 diff（修复文件本身 0 diff），属版本差而非代码问题。**建议 CI 与本地统一固定
   clang-format 21.x**（Windows 可 `pacman -S mingw-w64-ucrt-x86_64-clang-tools-extra`
   后降级对应版本，或用脚本固定下载）。
5. **2026-11-17 复核（内嵌库更新后）**：Linux/x86_64 开发机（Qt 6.10.2、FFmpeg 8、
   CMake 4.2、Ninja 1.13、GCC 15）上 `check.py ci` 全绿：configure/build/test 通过，
   ctest 3/3（`resr_tests` 内 4 条依赖 Windows 引擎的链路用例 `QSKIP`、其余通过）；
   `cpp_audit all` 0 命中。`check.py style` 用默认 clang-format 22.1.2 时在 4 个
   **未改动基线文件**上报 diff，按第 4 条换成建议的 clang-format 21.1.8 后
   `changed: 0`。cxx_pkg 的 `--check-config`/`--dry-run` 在 Linux 上通过（退出 0，
   只校验/打印计划，未产物）；因为 `cxx_pkg.ini` 不再写死 `qt.prefix`，改由 PATH 中的
   `qmake -query` 定位 Qt，兼容 `C:/msys2`、`C:/msys64` 两种 MSYS2 安装布局。
6. **2026-11-17 Windows 复核**（测试机 MSYS2 UCRT64：Qt 6.12.0、GCC 16.2、
   CMake 4.5.0、Ninja 1.13.2、FFmpeg 9.0 dev）：`check.py ci` 全绿
   （clean/configure/build/test，106s；ctest 3/3 通过，`-LE engine` 排除真实 GPU
   引擎用例）。构建仅 `src/infrastructure/exerunner.cpp` 2 条既有
   `-Wcast-function-type` 警告（FARPROC 强转，非内嵌库改动引入）；`cpp_audit all`
   0 命中；`cxx_pkg --check-config`/`--dry-run` 通过（prefix 不再写死后由
   `qmake -query` 自动定位 Qt）。**真实打包成功**：`cxx_pkg` 全流程产出
   `dist/real-esrguisoft-1.0.0-win64.zip` + `SHA256SUMS`（PE 导入闭包从
   `[build] lib_dirs` 补齐 102 个 DLL、31 个 PE 文件，全部解析；dist/ 被忽略，
   不入库）。打包配置现用 `install = false` 跳过安装（远程 7d46922），不再依赖
   install 收编第三方库；同时根 `CMakeLists.txt` 保留占位前缀适配：上游 singleproc
   的 `install()/configure_package_config_file()` 未按“仅顶层项目”收窄
   （stacklog 已收窄），任何以 `-DCMAKE_INSTALL_PREFIX=/` 配置的场景仍会触发
   `CMakePackageConfigHelpers` 的 `file RELATIVE_PATH must be passed a full path`，
   故引入 singleproc 期间临时使用占位前缀、随后恢复（防御性，不影响正常构建）。
   另：远程 774bc84 的 jpg/webp 收尾在写临时文件后立即 `QFile::rename`，Windows
   上 `QImageWriter` 仍持有句柄会让 rename 失败（`image_engine_crash_retry` 报
   “无法写入输出文件”）；已把 writer 收进作用域、析构关闭句柄后再 rename，编码
   语义不变，仅修 Windows 句柄顺序。
   `check.py style` 未跑：MSYS2 环境未装 clang-format（见第 4 条）。

---

## 8. 退出码速查

| 工具 | 0 | 1 | 2 | 130 |
| --- | --- | --- | --- | --- |
| `ci_tool` | 全部阶段成功 | 某阶段失败（configure/build/test 未通过） | 配置/用法错误、工具缺失 | 用户中断 |
| `standardize_tool` | 无需修改（非 `--check` 时生成 diff 也返回 0） | `--check` 模式存在 diff | 工具/配置/文件处理失败 | 用户中断 |
| `cpp_audit` | 检查通过（含 SKIPPED） | 检查执行完成但有失败项 | 配置/用法错误（或 `build` 子命令构建失败） | 用户中断 |
