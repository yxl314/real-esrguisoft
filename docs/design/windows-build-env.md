# Windows 真机构建与验收环境（调研快照）

> 用途：在 Linux 开发机上通过 SSH 驱动 Windows 真机做构建/测试/出包验收。
> 最后调研：2026-11-15（Windows 11 24H2+，MSYS2 UCRT64）。本文只记录环境事实与
> 操作方法，不重复构建原理；构建原理见交接文档 §4。

## 1. 连接方式

Windows 已配置 OpenSSH Server（纯密钥、管理员令牌、免 UAC），从本机 Ubuntu：

```bash
# 注意 1：本机 /etc/ssh/ssh_config.d 权限损坏，必须 -F /dev/null 绕过系统配置
# 注意 2：地址随网络模式变化——NAT 模式为 192.168.220.1，桥接模式为 192.168.3.19
#        （2026-11-15 重启后 VM 处于桥接网段，使用 192.168.3.19；~/.ssh/config 里
#         有 windows-nat / windows-bridge 别名，但建议显式 IP 以免误连）
ssh -F /dev/null -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
    -i ~/.ssh/id_ed25519 yxl@192.168.3.19 '<PowerShell 命令>'

scp -F /dev/null -i ~/.ssh/id_ed25519 <本地文件> yxl@192.168.3.19:C:/Users/yxl/
```

`scp` 在部分情况下会因系统 ssh 配置卡住，可靠替代：把脚本通过 stdio 传给远端
`powershell -NoProfile -ExecutionPolicy Bypass -Command -`：

```bash
ssh ... yxl@192.168.3.19 "powershell -NoProfile -ExecutionPolicy Bypass -Command -" < probe.ps1
```

- 默认 shell：**Windows PowerShell 5.1**（不要用 `&&`；用 `;`；`$` 在部分嵌套场景需转义）。
- 管理员令牌：`IsInRole(Administrator)=True`，`Stop-Service` 等无需 UAC；密钥勿泄露。
- 会话事实（真机实测）：`yxl` 在 **console session 1 处于 Active**（已登录桌面）；
  sshd 进程在 **session 0**，因此 ssh 启动的 GUI 不可见/`CreateWindow` 可能报“拒绝访问”；
  `QTEST_GUILESS_MAIN` 测试与离屏渲染不受影响。真实窗口交互/系统截屏可用
  `schtasks /create ... /IT`（以登录用户交互会话运行）或 `tools/capture_admin.ps1`。
- Qt 部署形态：MSYS2 UCRT64，插件目录 `C:/msys64/ucrt64/share/qt6/plugins`；
  平台插件含 `qwindows.dll` / `qdirect2d.dll` / `qminimal.dll` / `qoffscreen.dll`；
  **multimedia 后端只有 `ffmpegmediaplugin.dll`**（Qt Multimedia 走 FFmpeg 插件，
  与媒体层 libav* 是两套实现，注意区分）。`windeployqt.exe` 版本 6.12.0 可用。
- Qt Linguist：`Qt6LinguistTools` + `lupdate/lrelease 6.12.0` **在 Windows 侧可用**
  （Linux 侧缺 Qt6 linguist 工具）——i18n 翻译资产的生成/编译放在 Windows 侧执行。
- 未安装 MSVC（无 `cl.exe`/`rc.exe`）；项目的 `resources/app.rc` 由 MSYS2 `windres` 编译。

## 2. 版本矩阵

| 组件 | Windows 真机 | 本机 Ubuntu（对照） |
| --- | --- | --- |
| OS | Windows 11 专业版（10.0.26300） | Ubuntu 26.04 |
| Qt | **6.12.0**（MSYS2 UCRT64，`C:/msys64/ucrt64`） | 6.10.2（系统包） |
| 编译器 | GCC 16.2.0（UCRT64）；**未装 MSVC** | GCC 15 / Clang 21 |
| CMake / Ninja | 4.5.0 / 1.13.2 | 4.x / 同 |
| FFmpeg | **9.0**（libavcodec 63.1.102、libavformat 63、libavutil 61、libswscale 10、libswresample 7，pkg-config 可见） | 8.0（libavcodec 62.11） |
| Vulkan | 运行时 1.4.357 + `vulkaninfo.exe`；有 NVIDIA Optimus/AMD switchable 层 | 有 dev 头文件（无 GPU 测试） |
| CPU / RAM | AMD Ryzen 9 9955HX（16C/32T）/ 31.2 GB | — |
| GPU | NVIDIA RTX 5070 Ti Laptop（驱动 32.0.16.2124，Vulkan 1.4.357，含 NV_optimus 层）+ AMD Radeon 610M（switchable graphics 层） | 无独立 GPU |
| Git / Python | git（hermes 版）/ Python 3.14 | git / Python 3.14 |
| Qt Linguist | **lupdate / lrelease 可用**（6.12） | **缺 Qt6 linguist 工具**（只有 Qt5.6 的 `/opt/qt-5.6.3/bin` 与运行期 `qt6-translations-l10n`） |
| clang-format | UCRT64 自带（版本随 MSYS2） | 21.1.8 |
| clang-tidy | 未安装（`audit.toml` 在 Windows 本就禁用） | 21.1.8 |

> 结论：Windows 的主工具链是 **MSYS2 UCRT64 + GCC 16.2 + Qt 6.12 + Ninja + FFmpeg 9**，
> 与交接文档 §4 的假设一致（Qt 小版本比文档中的 6.11 略新）。项目未使用 MSVC，
> 打包由工作区同级 `cxx_pkg`（0.3+）负责：MSYS2 `windeployqt` 部署 Qt 插件 +
> 纯 Python PE 导入闭包补齐第三方 DLL，产出 zip；项目内不再维护自写打包脚本。

## 3. 代码与仓库位置

- Windows 仓库：`C:/Users/yxl/Desktop/code/real-esrguisoft`（git 远端 GitHub：
  `git@github.com:yxl314/real-esrguisoft.git`，可直接 `git fetch/pull`）。
- 工具仓库同级：`C:/Users/yxl/Desktop/code/{cxx_ci,cxx_audit}`。
- 引擎与模型：`bin/realesrgan/realesrgan-ncnn-vulkan.exe` + `models/` 共 10 个文件。
- 验收脚本：`tools/{deploy,capture_admin,capture_ui,screenshot,screenshot_ui}.ps1`。

## 4. 构建与测试命令（在 Windows 上执行）

```powershell
# 1) 进入 MSYS2 UCRT64 环境（PATH 含 ucr64/bin 后 cmake/ninja/gcc/pkg-config 才可见）
C:/msys64/usr/bin/bash -lc "export MSYSTEM=UCRT64; cd /c/Users/yxl/Desktop/code/real-esrguisoft; \
  cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release; \
  cmake --build build-win --parallel"

# 2) ctest（Windows 上 4 条引擎用例应真实执行，不 QSKIP）
C:/msys64/usr/bin/bash -lc "export MSYSTEM=UCRT64; cd /c/Users/yxl/Desktop/code/real-esrguisoft; \
  ctest --test-dir build-win --output-on-failure"

# 3) 四条真实链路验收（真实引擎 + 真实素材）
build-win/tests/resr_pipeline_verify.exe --out build-win/verify
```

注意：`ctest` 与直接运行测试可执行文件在 ssh 会话下的差异见 §6；离屏截图可用项目自带的
`RESR_UI_PAGE/RESR_UI_THEME/RESR_UI_SHOT` 钩子（`QT_QPA_PLATFORM=offscreen`）生成 PNG 后
`scp` 回 Ubuntu 查看。

## 5. 质量门禁（Windows 侧适配）

```powershell
# 在 UCRT64 PATH 下（cmake/ctest/clang-format 可见）
cd C:/Users/yxl/Desktop/code/real-esrguisoft
python ..\cxx_ci\check.py ci --root .
python ..\cxx_ci\check.py style --root .                   # 应 changed 0
$env:PYTHONPATH='..\cxx_audit\src'; python -m cxx_audit -c audit.toml --check-config
$env:PYTHONPATH='..\cxx_audit\src'; python -m cxx_audit -c audit.toml all
```

已知差异：Windows 无 clang-tidy（`audit.toml` 已禁用 tidy/cpd/asan/valgrind）；
`ci.toml` 的 `[tools].make=mingw32-make` 只在 qmake/自定义构建时需要，CMake+Ninja 不依赖。

## 6. 已知坑（真机验证会遇到）

1. **ssh 非交互会话**：`CreateWindow() for QEventDispatcherWin32 internal window failed
   (拒绝访问)` 是 Qt 在 session 0 建窗口失败的提示，QTEST_GUILESS 测试与离屏渲染不受影响；
   需要可见窗口/点击级交互时用 `schtasks /create ... /IT` 或 `tools/capture_admin.ps1`。
2. **中文路径与外部引擎**：`realesrgan-ncnn-vulkan.exe` 对非 ASCII 路径可能报
   `encode image ... failed`（即使 QProcess 以 Unicode 传参）；媒体层本身已改为向 FFmpeg
   传 UTF-8 路径。引擎输入/输出中文路径的中转行为列入 task-29 交互验证（环境隔离后）。
3. **批量「原格式」**：引擎目录模式未传 `-f` 时统一输出 PNG（不保留 jpg/webp 扩展名）；
   成功判定必须按引擎实际输出格式计算（真机已发现并修复中）。
4. **PowerShell 5.1**：不支持 `&&`；`git` 在 hermes 路径；从 bash 调 PowerShell 时注意引号。
5. **控制台编码**：`chcp 65001` 下引擎 stderr 的中文/路径可能显示为乱码，不影响退出码与
   产物判定，但解读日志时注意。
6. **ctest 与直接运行**：曾在 ctest 下出现 `resr_tests` 失败而直接运行 16/16 通过的情况
   （环境/工作目录差异），验收时以直接运行 + ctest 双跑为准，异常时记录 cwd/PATH。

## 7. 环境探针（可复跑）

`.cxx-review/out/real-esrguisoft/win-probe/probe.ps1` 是本轮使用的只读探针脚本，
通过 stdio 传给 Windows PowerShell 执行，输出覆盖：OS/CPU/RAM/GPU/驱动、`quser`/`qwinsta`
会话、MSYS2 工具链、Qt 版本/模块/插件、FFmpeg dev 版本、Vulkan 版本与层、仓库/构建缓存、
引擎与模型、sshd/网络。持续验证时可扩展该脚本而不是手敲命令。

本轮实测补充结论：

- MSYS2 默认 shell 是 **MSYS 环境**（`MSYSTEM=MSYS`），此时 `cmake` 是 MSYS 版；
  项目构建务必显式 `export MSYSTEM=UCRT64` 并使用 `C:/msys64/ucrt64/bin` 的工具链。
- `pkg-config` 可见的 FFmpeg dev 版本：libavcodec/libavformat **63.1.102**、
  libavutil 61.1.102、libswscale 10.1.102、libswresample 7.1.102、libavfilter 12.1.102。
- Vulkan 实例 1.4.357，`VK_LAYER_NV_optimus` 与 `VK_LAYER_AMD_switchable_graphics`
  均在；引擎默认会在混合显卡上选择设备，真机验收需记录 `-g` 选择与耗时差异。

## 8. 真机验收结论摘要（2026-11-15，task-23）

详细报告：`.cxx-review/out/real-esrguisoft/windows-verification.md`；完整证据在
`windows-verify/`（含 8 张 Windows 截图、ctest/链路/harness/门禁/出包日志）。

- 构建 84 目标 0 warning；`ctest` 2/2（当时；现全量 3/3）；`resr_tests` 16/16、
  `resr_media_tests` 10/10，
  **0 QSKIP**（真实 PE 引擎 + Vulkan）。
- 四条真实链路全过：单图 3057ms / 批量图 2213ms / 单视频 16257ms（音频保留）/
  批量视频 48623ms。
- 真实引擎暂停（NtSuspendProcess）/停止（42–73ms，停止后不再启引擎）/关闭（9–10ms）
  /Win32 singleproc（退出码 0 + 门铃激活）/出包（当时 `deploy.ps1` 42 文件 dist
  仓库外可启动）PASS；第四阶段已切换为 cxx_pkg 自包含 zip。
- 修复：图片批量「原格式」成功判定按引擎实际 PNG 对齐（P1）；关闭等待中任务先失败时
  立即 force_close（加固）。
- **仓库目录受限令牌**：本机对仓库目录内启动的 exe 施加受限令牌（只能写仓库内），
  会让引擎处理中文/外部路径失败；复制到仓库外或 dist 即正常，非产品缺陷。做真机
  验收时请使用仓库外副本/dist。
- **clang-format 版本差**：Windows 23.1.5 vs Linux 21.1.8，`standardize --check` 在
  4 个未改动基线文件上有 diff（修复文件 0 diff）；建议固定版本。

## 9. 代码评审工具链（2026-11-15 预备，供 task-28 使用）

`r6-ci-dev` 已在 UCRT64 预装并验证（`.cxx-review/out/real-esrguisoft/windows-code-review/toolchain-prep.md`）：

| 工具 | 版本 | 用途/备注 |
|---|---|---|
| clang / clang++ | 23.1.5 | 可与 GCC 交叉核对；正式构建仍用 GCC 16.2 |
| clang-tidy | 23.1.5 | 直接吃 `build-win/compile_commands.json`（GCC db 可用），~1.6s/文件可并行；**`.clang-tidy` 未设 WarningsAsErrors → 退出码恒 0，必须解析 warning 文本统计** |
| cppcheck | 2.22.0 | 需 `--library=qt -Dslots= -Dsignals=public -DQ_OBJECT=`，并 `-i build-win` 排除 moc/autogen 噪声；仅作辅助线索 |
| clang-format | 23.1.5 | 与 Linux 21.1.8 有版本差（见 §8） |

- `-Wall -Wextra -Wpedantic` 独立构建（`build-win-warn`）**全量成功、0 warning**，可作基线。
- 抽样 clang-tidy 已产生 18 条候选诊断（qsizetype→int 窄化、Win32 `FARPROC` 经 void* 转换、
  throwing-static-initialization、no-automatic-move/use-std-move 等），供 task-28 正式定级。
- **Windows 有两个 git**：hermes git 2.54（`core.autocrlf=true`，仓库状态以此为准）与
  MSYS2 git 2.56（未配 autocrlf）——在 MSYS 里裸 `git status` 会把 154 个文件误报为修改；
  脚本请用 hermes git 全路径或 `git -c core.autocrlf=true` 判断工作区干净度。
- 未装 MSVC/WDK；未做全量 clang++ 替代构建。

## 10. i18n 工具链提示

- Windows MSYS2 自带 Qt 6.12 的 `lupdate`/`lrelease`，可用来生成/编译 `.ts`/`.qm`。
- 本机 Ubuntu 没有 Qt6 linguist 工具（只有 Qt5.6 的旧版），因此翻译产物（`.ts`/`.qm`）
  计划**提交到仓库**、`.qm` 以 qrc 资源方式嵌入，Linux 构建不依赖 lrelease；
  更新翻译时在 Windows 侧执行 `lupdate/lrelease`（脚本见后续 `tools/`）。
