# Real-ESRGUISoft

基于 **C++17 + Qt 6 Widgets** 的 Real-ESRGAN 图形界面工具，为图片和视频提供超分辨率放大处理。
图片逐张超分、视频拆帧超分后重新编码，全部编排放进进程内完成（媒体层直接链接 libav*，
不再调用 ffmpeg.exe / ffprobe.exe）。

## 功能特性

- **图片处理**：单张 / 批量图片超分辨率放大
- **视频处理**：单个 / 批量视频超分辨率放大（拆帧 → 超分 → 编码，原音频优先 stream copy）
- **多模型支持**：真实照片、动漫图片、动漫视频专用模型
- **TTA 模式**：测试时增强，提升输出质量
- **多 GPU 支持**：自动 / GPU 0/1/2
- **实时进度 / 暂停 / 恢复 / 停止**：进度、当前文件与失败聚合统一由状态快照广播；
  开始 / 暂停 / 继续合并为进度条右侧的**纯图标主按钮**（tooltip 提示当前动作），
  停止按钮同区域，无输入时主按钮灰色禁用；换输入会复位旧结果，取消/停止收尾
  会在进度卡显示明确状态；`Ctrl+Enter` 开始/暂停/继续、`Ctrl+Shift+X` 停止
- **线程配置**：高级参数可填 Real-ESRGAN 的 `-j`（`load:proc:save`），合法值才会传给引擎
- **双主题**：浅色 / 深色 / 跟随系统，选择持久化（`QSettings`）
- **中英双语**：界面语言 中文 / English / 跟随系统；空闲时切换**立即生效**（无需重启）
  并自动记忆（`QSettings` `ui/language`）；任务运行中语言控件暂时禁用，任务结束后可切换；
  英文翻译资源随程序内置
- **设置页**：超分引擎路径覆盖（选择文件 / 目录 / 恢复自动发现）、日志目录展示与打开、
  处理默认值重置、运行环境信息
- **关于页**：版本号（由 CMake 注入，不硬编码）、开源许可、致谢与图标许可
- **单实例**：二次启动唤醒主窗口；运行日志按大小轮转

## 支持的格式

### 图片格式

| 格式 | 输入 | 输出 |
|------|------|------|
| JPG/JPEG | ✓ | ✓ |
| PNG | ✓ | ✓ |
| WebP | ✓ | ✓ |
| BMP | ✓ | 自动转 PNG |
| GIF（静态） | ✓ | 自动转 PNG |

### 视频格式

| 格式 | 输入 | 输出 |
|------|------|------|
| MP4 | ✓ | ✓（H.264 / H.265 / VP9 / AV1） |
| MKV | ✓ | ✓ |
| AVI | ✓ | ✓ |
| MOV | ✓ | ✓ |
| WebM | ✓ | ✓ |
| WMV / FLV / TS | ✓（可探测即可拆帧） | 「原格式」自动回退 MP4 |
| GIF（动态） | ✓（拆帧后可处理） | 自动输出 MP4（输出下拉不含 gif） |

> 「原格式」= 输出沿用输入容器的扩展名；受支持的输出容器为 mp4 / mkv / avi / mov / webm，
> 输入 WMV/FLV/TS/GIF 等不在白名单时自动回退为 MP4 输出（并记录日志）。

## AI 模型

| 模型 | 适用场景 | 放大倍数 |
|------|----------|----------|
| `realesrgan-x4plus` | 真实照片 | 4x（固定） |
| `realesrgan-x4plus-anime` | 动漫图片 | 4x（固定） |
| `realesr-animevideov3` | 动漫视频 | 2x / 3x / 4x 可选（GUI 默认 4x） |

> 说明：旧 CLI 的 `anime_video` + 2x 默认值是命令行工具的默认，**不适用于 GUI**。
> GUI 视频模式不再显示「图片类型」，模型下拉直接提供全部模型，默认选中
> `realesr-animevideov3`（4x；可选 2x/3x/4x），并给出视频推荐提示；图片/视频
> 两种模式的基本设置分别保存，切换模式不会互相覆盖。

## 系统要求

### Windows（完整功能，目标环境）

- Windows 10/11 64 位
- 支持 Vulkan 的显卡（推荐独显）；引擎 `realesrgan-ncnn-vulkan.exe` 依赖 Vulkan 运行时
- 运行时：`bin/realesrgan/`（引擎 exe + `models/`）随程序分发

### Linux（开发 / 构建 / GUI + 媒体层可运行）

- 可以构建并运行 GUI 与媒体层（libav* 链接），但**超分引擎当前只随包提供 Windows PE**：
  - Linux 上 GUI 可启动、可浏览媒体、可跑 `resr_media_tests`；
  - 依赖引擎的处理链路无法执行（`bin/realesrgan/realesrgan-ncnn-vulkan.exe` 是 Windows
    可执行文件），自动化测试会 `QSKIP` 这 4 条引擎用例；
  - 如确需在 Linux 跑超分，需要自行提供 Linux 版 realesrgan-ncnn-vulkan，并在设置页
    「超分引擎路径」中指定；当前版本未对 Linux 引擎做适配与验证。

### 构建依赖

- Qt 6（Core / Gui / Widgets / Multimedia / MultimediaWidgets）
- FFmpeg 开发库：libavformat / libavcodec / libavutil / libswscale / libswresample
  （Windows 用 MSYS2 UCRT64 包或内嵌源码构建；Linux 用系统 pkg-config）
- C++17 编译器（GCC / Clang）、CMake ≥ 3.21、Ninja

## 项目结构

```text
real-esrguisoft/
├── bin/
│   └── realesrgan/               # 运行时唯一外部依赖：引擎 exe + models/
├── cmake/                        # CMake 查找模块（FindFFmpeg / MediaFFmpeg）
├── third_party/                  # 内嵌第三方资产
│   ├── stacklog/                 # 日志库（src/common/logger.* 为兼容层）
│   ├── singleproc/               # 单实例库（POSIX + Win32 后端）
│   └── ffmpeg/ffmpeg-9.0.2/      # FFmpeg 源码 + build-ffmpeg.sh/.ps1
├── resources/
│   ├── resources.qrc             # 资源定义
│   ├── app.png / app.ico / app.rc.in   # 应用图标（含 Windows exe 图标）
│   ├── icons/                    # 35 个 Lucide SVG + 38 个主题/描边 PNG（resources.qrc 收录 38 个
│   │                             #   PNG；generate_icons.py 为一次性图标生成脚本）
│   └── theme/                    # 主题模板（app.qss.in / panels.qss.in，{{token}} 占位）
├── src/                          # C++ 源码（分层 MVC，见 ARCHITECTURE.md）
│   ├── common/                   # 常量 / 日志兼容层 / AppSettings（QSettings 用户设置）
│   ├── controller/               # AppController（会话状态 / 流程决策）+ 对话框抽象
│   ├── domain/                   # MediaSettings / ProcessStatus / FileService（无界面依赖）
│   ├── infrastructure/           # execommand / exeoutputparser / exerunner + media/（libav* 封装）
│   ├── model/                    # ProcessService + Image/VideoProcessService + 路径/扫描/进度
│   ├── view/                     # mainwindow + panels/ + widgets/ + theme/
│   └── utils/                    # 工具提示等
├── tests/
│   ├── test_pipeline.cpp         # 编排/参数/引擎链路测试（resr_tests，注册 ctest）
│   ├── test_media.cpp            # 媒体层测试（resr_media_tests，注册 ctest）
│   └── pipeline_verify.cpp       # 四条链路人工验收工具（不注册 ctest）
├── tools/                        # Windows 辅助脚本（deploy / 提权截图等，见 tools/*.ps1）
├── input/                        # 验收素材（9 张图片 + 3 个视频）
├── docs/
│   ├── ci.md                     # 质量门禁（构建/排版/静态检查一键命令与平台差异）
│   └── design/                   # 设计与改造文档（交接文档 / 改造总览 / UI 规范 / 移植规格 / FFmpeg 接入）
├── ci.toml                       # cxx_ci 构建/测试流水线配置
├── standardize.toml + .clang-format   # cxx_ci 排版门禁配置
├── audit.toml + .clang-tidy      # cxx_audit 静态检查配置
├── main.cpp                      # 程序入口（版本注入 / 单实例 / 门铃 / 主窗口）
└── CMakeLists.txt                # 唯一构建入口（CMake + Ninja）
```

> 产品代码为纯 C++17（无 Python 运行时、无 PyInstaller 打包产物）；仓库里唯一的 Python
> 文件是 `resources/icons/generate_icons.py`，仅用于一次性生成图标 PNG，不参与构建与运行。

## 架构设计

项目采用 **分层 MVC（Model-View-Controller）** 架构，依赖单向向下：

```
View（界面/主题/控件）
  → Controller（会话状态、流程决策、状态下发）
    → Model（ProcessService 编排 + ImageProcessService / VideoProcessService 原生链路）
      → Infrastructure（ExeRunner / ExeCommand / ExeOutputParser + media 层 libav* 封装）
        → Domain（MediaSettings / ProcessStatus / FileService）
          → Common（Constants / Logger / AppSettings）
```

- 处理任务只通过一个 `ProcessStatus` 结构化快照向下游广播（生命周期 + 进度 +
  当前文件 + 描述文案），视图一次性重建界面；
- 图片链路：C++ 解析命名规则 → 直接调用 `bin/realesrgan/realesrgan-ncnn-vulkan.exe`
  → 按产物与失败标记判定成功；
- 视频链路：`MediaProbe`（libavformat 探测）→ `FrameDecoder`（拆帧为 PNG 序列）
  → realesrgan 目录批量超分 → `VideoEncoder`（libavcodec 编码 + 原音频 stream copy）。
  全程在进程内完成，不再启动 ffmpeg.exe / ffprobe.exe。

更详细的模块职责、依赖约束与设置页/测试入口见 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 构建方法

### Windows + MSYS2 UCRT64（推荐 / 完整功能）

环境：MSYS2 UCRT64（Qt 6.11.x、GCC 16.x、CMake、Ninja、FFmpeg 9.x 开发包）。

```powershell
# PATH 需包含 C:\msys64\ucrt64\bin（按本机安装位置调整）
$env:Path = 'C:\msys64\ucrt64\bin;' + $env:Path

cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64 -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8

# 运行
build\real-esrguisoft.exe
```

打包可独立运行的 zip（**唯一入口**：工作区同级的 `cxx_pkg` 项目，0.3+，TOML 配置）：

```powershell
# 项目根目录；cxx_pkg 路径按本机实际位置调整（未安装时用 PYTHONPATH 指向 cxx_pkg/src）
$env:PYTHONPATH = 'C:\Users\yxl\Desktop\code\cxx_pkg\src'
python -m cxx_pkg -c cxx_pkg.toml --check-config
python -m cxx_pkg -c cxx_pkg.toml --dry-run
python -m cxx_pkg -c cxx_pkg.toml    # 构建+部署+打包
```

产物：`dist/real-esrguisoft-<version>-win64.zip`（自包含：Qt/FFmpeg/第三方 DLL
闭包 + `bin/realesrgan` 引擎与模型 + SHA256SUMS）；项目内不再维护自写打包脚本。

FFmpeg 查找顺序：系统 pkg-config → `-DFFMPEG_ROOT=<prefix>` →
`third_party/ffmpeg` 源码构建产物；详见 [docs/design/ffmpeg-integration.md](docs/design/ffmpeg-integration.md)。

### Linux（开发 / 验证）

需要 Qt 6 开发包（Widgets / Multimedia / MultimediaWidgets）与系统 FFmpeg 开发库
（pkg-config 可查到 libavformat 等）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# 运行 GUI（需要显示环境）
./build/real-esrguisoft

# 测试（见「测试与质量门禁」）
ctest --test-dir build --output-on-failure
```

> Linux 上超分引擎不可执行（只有 Windows 版 exe），依赖引擎的功能会失败或 QSKIP；
> 构建、GUI、媒体层探测/拆帧/编码不受影响。

### 使用 Qt Creator

1. 打开 `CMakeLists.txt`（Qt Creator 会识别为 CMake 工程），选择 Qt 6.x 套件；
2. 构建并运行（本工程已不再维护 qmake `.pro`，CMake 是唯一构建方式）。

## 测试与质量门禁

### 单元 / 集成测试

```bash
ctest --test-dir build --output-on-failure
```

- `resr_media_tests`：媒体层（探测 / 拆帧 / 编码）回归，Linux 与 Windows 均可通过（当前 10 个用例，含 webm+音频转码、时长、中文路径）；
- `resr_tests`：编排与参数级用例 + 4 条真实引擎链路用例。
  - **Linux**：4 条引擎用例自动 `QSKIP`（`bin/realesrgan` 只有 Windows PE，测试内注明原因），
    其余全部通过（实测 12 passed / 4 skipped / 0 failed）；
  - **Windows（2026-11-15 真机实测）**：4 条引擎用例真实执行（Vulkan GPU + `bin/realesrgan/`
    完整），`resr_tests` 16 passed / 0 skipped、`resr_media_tests` 10 passed，
    `ctest` 3/3 通过（`resr_i18n_tests` 为新增语言测试）。

### 四条链路人工验收（真实素材）

```powershell
# Windows（需要引擎与 Vulkan GPU；输出默认 build/verify）
build\tests\resr_pipeline_verify.exe --out build\verify
```

从任意嵌套构建目录（如 `build/ci/tests/`）运行也可以：工具会向上查找含
`CMakeLists.txt` 与 `input/` 的工程根。Linux 上该工具会因引擎不可执行而失败，
只建议用来验证参数构造 / 工程根解析。

### 质量门禁（cxx_ci + cxx_audit）

门禁工具不随本仓库提交，需在工作区同级放置 `cxx_ci`、`cxx_audit` 两个工具仓库
（详见 [docs/ci.md](docs/ci.md)）。

仓库根提供 `./check.sh`（POSIX 薄壳，`all|ci|style|audit`）统一入口；也可直接跑
`cxx_ci/check.py` 与 `cxx_audit`：

```bash
# Linux / WSL：在仓库根执行
./check.sh                    # ci + style + audit
./check.sh ci                 # 构建+测试流水线
./check.sh style              # 排版检查（应为 0；clang-format 版本见 docs/ci.md §7）
./check.sh audit              # 静态检查
```

```powershell
# Windows PowerShell（无 check.sh；直接用 cxx_ci/check.py 与 cxx_audit）
python ..\cxx_ci\check.py ci --root .
python ..\cxx_ci\check.py style --root .
$env:PYTHONPATH = '..\cxx_audit\src'; python -m cxx_audit -c audit.toml all
```

门禁含义、Windows/Linux 差异与降级、失败处理、Linux/WSL 补跑 tidy/cpd/asan/valgrind
的方法，统一见 **[docs/ci.md](docs/ci.md)**。

排版约定在根 `.clang-format`（Tab 缩进、Allman、80 列等，从现有源码反推）。全仓已完成
一次格式化收口；日常改动后跑 `cxx_ci/check.py style --root .`，有 diff 时先看
`build/standardize/standardize.diff`，确认只是排版再 `--apply`。

## 使用说明

### 图片处理

1. 选择功能类型：图片
2. 选择处理模式：单文件或批量处理
3. 选择输入文件/文件夹（批量模式可多选）
4. 配置处理参数：
   - 图片类型：真实图片 / 动漫图片（决定可选模型）
   - 模型：真实照片 `realesrgan-x4plus` / 动漫图片 `realesrgan-x4plus-anime`
     或动漫视频模型 `realesr-animevideov3`
   - 放大倍数：按模型提供（动漫视频模型可选 2x/3x/4x，其余固定 4x）
   - 输出格式：原格式 / JPG / PNG / WebP
   - TTA 模式：提升质量但速度较慢
   - GPU 设备：自动 / GPU 0/1/2
   - 图块大小：处理大图显存不足时调整
   - 线程数：Real-ESRGAN 的 `-j`，格式 `load:proc:save`（默认 `1:1:2`）
5. 点击开始处理

### 视频处理

1. 选择功能类型：视频（右侧「视频设置」卡出现编码格式 / 编码质量）
2. 选择处理模式：单文件或批量处理
3. 选择输入文件/文件夹
4. 配置处理参数：
   - 「基本设置」卡在视频模式隐藏「图片类型」，模型下拉直接列出
     `realesr-animevideov3`（默认推荐，4x）以及 `realesrgan-x4plus` /
     `realesrgan-x4plus-anime`；动漫视频模型可选 2x/3x/4x
   - 视频设置：编码格式（H.264 / H.265 / VP9 / AV1）、编码质量（快速 / 均衡 / 质量）
   - 高级参数：输出格式（原格式 / MP4 / MKV / AVI / MOV / WebM）、GPU、图块大小、
     TTA、线程数
5. 点击开始处理

### 设置页

设置页（左侧栏「设置」）包含：

- **外观**：浅色 / 深色 / 跟随系统，切换即时生效并记住；
- **超分引擎路径**：默认自动发现（从程序目录向上查找 `bin/realesrgan`）；可手动选择
  `realesrgan-ncnn-vulkan.exe`、`bin/realesrgan` 目录或 `bin` 目录，改动在下次开始
  处理时生效；「恢复自动发现」清除手动设置。无效路径会提示并回退自动发现；
- **日志**：展示当前日志目录并提供「打开日志目录」按钮；
- **处理默认值**：把图片 / 视频参数恢复为出厂默认；
- **运行环境**：Qt 版本、引擎说明、**单任务串行、无可配置并发数**、图标许可。

### 关于页

展示应用图标、版本号（由 CMake 的 `project(VERSION ...)` 注入）、简介、技术标签、
开源许可对话框与致谢。

### 输出命名规则

**单文件输出**：

```
原文件名_原扩展名_模型名_x（TTA时）.输出扩展名
```

示例：

- `photo.jpg` + `-m real` → `photo_jpg_real.png`
- `anime.png` + `-m anime -x` → `anime_png_anime_x.png`

**批量处理输出**：

```
原文件夹名_模型名_x（TTA时）/
```

## 界面语言（中英双语）

- 设置 →「外观」卡 →「界面语言」：**中文 / English / 跟随系统**。
  - 空闲时切换后界面**立即刷新**（页头、侧栏、卡片、按钮、下拉、提示、对话框等），无需重启；
  - 处理任务运行中语言控件暂时禁用（视频 worker 的翻译器装卸需延迟到任务结束），
    任务结束后恢复可切换；避免“控件已切换但界面未刷新”的不一致；
  - 选择立即写入 `QSettings`（键 `ui/language`，值 `zh`/`en`/`system`），重启后保持；
  - `跟随系统` 按系统 locale 解析（中文系→中文，其它→English）。
- 翻译覆盖：视图、设置页/关于页、工具提示、控制器对话框与文件过滤器、处理状态与
  错误文案、领域显示名；**日志、CLI 验收工具与测试输出不翻译**。
- 英文翻译资源在 `resources/i18n/app_en.ts`（翻译源，可编辑）与 `app_en.qm`
  （运行时以 qrc 嵌入），源码中文文案作为翻译 key 保持不变。
- 更新翻译（需要 Qt Linguist 工具；Linux 无 Qt6 linguist 工具时请在 Windows/MSYS2 跑）：

  ```powershell
  # Windows（MSYS2 UCRT64，Qt 6.12 自带 lupdate/lrelease）
  tools\update-translations.ps1
  ```

  ```bash
  # Linux（若已安装 Qt6 linguist 工具）
  tools/update-translations.sh
  ```

  脚本只更新 `.ts/.qm`，不自动提交；新增/修改文案后跑一遍、补齐英文翻译并提交。
- 已知限制：Qt 内建对话框（`QFileDialog`/`QMessageBox` 的标准按钮如“打开/取消”）
  跟随 Qt/系统语言，不随应用语言切换（本工程未内置 qtbase 翻译文件）；处理中任务
  已显示的“最后一条详细消息”在下次状态更新前保持旧语言，生命周期标签与计数会即时刷新。

## 视频质量参数说明

### 编码格式

| 格式 | 说明 | 优点 | 缺点 |
|------|------|------|------|
| H.264 | 最广泛兼容的视频编码格式 | 兼容性好，支持广泛 | 压缩效率相对较低 |
| H.265 | 高效视频编码（HEVC） | 压缩效率高，相同质量文件更小 | 兼容性稍差，编码较慢 |
| VP9 | Google 开发的开源编码格式 | 开源免费，压缩效率高 | 兼容性一般，硬件支持有限 |
| AV1 | 下一代开源编码格式 | 压缩率极高，未来趋势 | 编码速度慢，硬件支持有限 |

### 质量级别

| 质量级别 | 说明 | 编码速度 | 输出质量 | 推荐场景 |
|---------|------|---------|---------|---------|
| 快速/fast | 优先处理速度 | 最快 | 中等 | 快速预览、临时文件 |
| 均衡/balanced | 速度和质量平衡 | 中等 | 良好 | 日常使用（默认） |
| 质量/quality | 优先输出质量 | 较慢 | 优秀 | 高质量存档、重要内容 |

### CRF 值

CRF（Constant Rate Factor）控制视频质量，范围 0-51：

| CRF 值 | 质量 | 文件大小 |
|--------|------|----------|
| 0-18 | 极高/无损 | 非常大 |
| 18-23 | 高质量 | 较大 |
| 23-28 | 中等质量 | 适中 |
| 28-51 | 低质量 | 较小 |

编码格式 / 质量档 → CRF / preset 的实际映射：

| 质量档 | H.264 / H.265 | VP9 / AV1 |
| --- | --- | --- |
| 快速（fast） | CRF 28，preset ultrafast | CRF 35，preset fast |
| 均衡（balanced，默认） | CRF 23，preset medium | CRF 30，preset medium |
| 质量（quality） | CRF 18，preset slow | CRF 25，preset slow |

### 编码预设

| 预设 | 编码速度 | 压缩效率 |
|------|----------|----------|
| fast | 快 | 一般 |
| medium | 中等 | 中等（默认） |
| slow | 慢 | 较高 |

## 处理流程（原生实现）

四个入口（单文件图片 / 批量图片 / 单文件视频 / 批量视频）全部编排在 C++ 内完成：

### 图片链路

```
输入图片 → [BMP/GIF 先用 QImage 转临时 PNG] → realesrgan-ncnn-vulkan 超分
        → 按目标格式写回（JPG/PNG/WebP）→ 校验产物存在且非空
```

- 单文件：逐张调用引擎，输出 `{stem}_{原扩展名}_{模型名}[_x].{格式}`；
- 批量：引擎目录模式一次处理整批，输出目录 `{输入目录名}_{模型名}[_x]/`；
- 成功判定 = 进程正常退出 + stderr 无 `decode/encode ... failed` + 产物存在且非空
  （realesrgan 失败时退出码仍可能是 0，不能只看退出码）。

### 视频链路

```
探测（libavformat）→ 拆帧为 PNG 序列 → realesrgan 按 50 帧/批分段超分
        → libavcodec 编码合成（原音频优先 stream copy）→ 校验产物
```

- 帧率用探测到的有理数（如 1199/50）原样传给编码器，避免浮点漂移；
- 线程数（`-j`）**总是显式传给引擎**：格式合法（`load:proc:save`）时用用户值，
  空/非法值回退安全默认 `1:1:2`（TDR 加固，不再依赖引擎默认 `1:2:2`）；
- 图块大小（`-t`）**总是显式传给引擎**：自动/0/非法值用安全默认 `256`，显式值
  限制在 32–1024（越界按边界值并记日志）；引擎异常崩溃（`0xC0000409`）时服务层
  会用更保守的 `-t 128 -j 1:1:1` 自动重试一次，再失败才报错（不写回用户设置）；
- **视频帧按 50 帧/批调用引擎**：拆帧后每 50 帧复制到一个临时批目录，**每批独立
  启动一次引擎进程**（对齐旧 Python 版 `image_batch.py` 的 `DEFAULT_BATCH_SIZE=50`），
  批间由全局串行 gate 保证 ≥500ms 冷却；避免单个引擎进程一次性处理整段视频帧
  （长时连续 GPU dispatch）触发驱动 TDR；
- 临时帧目录在任务结束后自动清理；取消时终止引擎子进程并清理残留；
- 批量视频逐个串行处理，总进度按视频数加权，失败聚合后统一上报；
- 视频输出容器为 原格式/mp4/mkv/avi/mov/webm（不含 gif）；GIF 输入会自动按 MP4 输出；
- 输出格式与编码格式不兼容的组合（如 webm+H.264、mov+VP9、avi+H.265）在开始前即明确拒绝，避免写头失败；

### 设置项说明

| 设置项 | 说明 | 默认值 |
| --- | --- | --- |
| 图片类型 | 真实图片 / 动漫图片（决定可选模型） | 真实图片 |
| 模型 | `realesrgan-x4plus` / `realesrgan-x4plus-anime` / `realesr-animevideov3` | `realesrgan-x4plus` |
| 放大倍数 | 按模型：动漫视频 2x/3x/4x；其余 4x | 4x |
| 输出格式 | 图片：原格式 / jpg / png / webp；视频：原格式 / mp4 / mkv / avi / mov / webm | 原格式 |
| TTA 模式 | 多次增强推理，质量更高、速度更慢 | 关闭 |
| GPU 设备 | 自动 / GPU 0 / 1 / 2 | 自动 |
| 图块大小 | 大图显存不足时调小；自动 = 安全默认 256（总是显式 `-t`，范围 32–1024） | 自动（256） |
| 线程数 | Real-ESRGAN `-j`（`load:proc:save`），总是显式传给引擎 | `1:1:2` |
| 视频编码 | H.264 / H.265 / VP9 / AV1 | H.264 |
| 视频质量档 | 快速 / 均衡 / 质量 | 均衡 |
| 超分引擎路径 | 手动覆盖自动发现；空 = 自动发现（设置页） | 自动发现 |
| 主题 | 浅色 / 深色 / 跟随系统（设置页） | 跟随系统 |

### 交互行为说明

- **刷新**：页头刷新按钮 / `F5` = 重置会话（清空输入/输出并恢复默认参数）；
  视频设置卡内的刷新按钮只恢复视频编码/质量默认值。处理中禁止重置。
- **批量模式**：输出由「设置输出目录」指定，「另存为」仅单文件模式可用。
- 处理中，工作台输入/输出/参数控件与模式切换自动禁用；关闭窗口会先确认并停止
  任务再退出；「设置输出目录」在输出设定后仍保留，可随时更改。

## 依赖项

### 运行时依赖

| 依赖项 | 路径 | 用途 |
|--------|------|------|
| realesrgan-ncnn-vulkan | `bin/realesrgan/`（含 `models/`，官方 v0.2.0） | 图片 / 视频帧的超分辨率引擎（Windows PE） |
| Vulkan 驱动 | 系统 | 引擎运行所需（推荐独显） |
| Qt 6 运行库 / FFmpeg 运行库 | `cxx_pkg` 打包进 `dist/`（zip） | GUI 与媒体层 |

### 构建依赖

| 依赖项 | 说明 |
|--------|------|
| Qt 6 | Core / Gui / Widgets / Multimedia / MultimediaWidgets |
| FFmpeg 开发库 | libavformat / libavcodec / libavutil / libswscale / libswresample（MSYS2 UCRT64 包或 third_party 源码构建；Linux 用 pkg-config） |
| C++17 编译器 | GCC（MSYS2 UCRT64）/ Clang / MSVC 均可 |
| CMake ≥ 3.21 + Ninja | 构建系统 |

## 开发

### 代码风格

- 缩进：Tab（显示宽度 4）；大括号独占一行（Allman）；
- 命名：函数 / 变量 / 成员统一 snake_case，成员变量带 `m_` 前缀，类型用
  CamelCase（如 `ImageProcessService`）；参数带 `v_` 前缀，成员参数按项目既有约定；
- 注释：中文分节注释，说明「为什么」而不是复述代码；
- 排版由根 `.clang-format` 定义（返回值独占一行、指针/引用靠变量名、80 列换行、
  二元运算符换行前置、不重排 include / 注释），用 `cxx_ci/check.py style --root .` 检查；
- 分层依赖只允许向下（View → Controller → Model → Infrastructure → Domain → Common），
  跨层反向依赖需先重构接口。

### 日志

日志底层是内嵌的 `third_party/stacklog`，`src/common/logger.h/.cpp` 只是兼容层：
保留原有 `LOG_DEBUG/INFO/WARNING/ERROR` 宏与 `Logger::instance()` / `set_log_level()`
接口，调用点无需改动。

- 日志目录：`<程序所在目录>/logs/`（设置页展示并可一键打开）；
- 日志文件：`real-esrguisoft.log`；单文件超过 5 MB 后轮转为
  `real-esrguisoft.log.1` … `real-esrguisoft.log.5`，最旧的一份被删除。
- 行格式：`时间|级别|trace_id|node_id|parent_id|scope|文件:行号|消息`，
  其中 `文件:行号` 是调用点（例如 `appcontroller.cpp:33`）。
- 保留旧行为：同时在控制台（`std::cout`）输出一份。
- 主实例退出前调用 `Logger::instance()->shutdown()` 关闭日志文件，确保落盘。

### 单实例

`main.cpp` 在 `QApplication` 构造后立即用内嵌的 `third_party/singleproc` 判定单实例：

- 已有一个实例时，二次启动通过门铃通知主实例，主实例把窗口
  `showNormal / raise / activateWindow` 前置，二次实例记录日志后以退出码 0 结束。
- 门铃由 `QTimer` 每 200 ms 轮询 `process_doorbell(0)`；POSIX 与 Windows 共用同一套代码。
- 单实例锁优先使用库默认运行目录（POSIX `XDG_RUNTIME_DIR`；Windows `%TEMP%`），
  不可写时退回程序目录下的 `singleproc/`。
- 自动化截图/并行验收时注意：同一 `XDG_RUNTIME_DIR` 下不能同时跑多个实例，
  否则第二个实例会静默退出（退出码 0、不产截图）；每个并行任务应使用独立
  `XDG_RUNTIME_DIR`。

## 已知限制

- **Linux 上超分引擎不可用**：`bin/realesrgan/realesrgan-ncnn-vulkan.exe` 是 Windows PE；
  Linux 可构建并运行 GUI/媒体层，但超分链路与 4 条引擎测试用例不可执行（QSKIP）。
  如需 Linux 超分，需自备 Linux 版引擎并在设置页指定，当前未验证。
- **视频输出不含 GIF**：输出容器为 mp4/mkv/avi/mov/webm；GIF 输入会自动回退 MP4。
  旧版打包工具的 GIF 输出能力未移植，如需要可作为后续需求补 GIF 编码。
- **失败/取消可能留下半成品文件**：引擎/编码失败或任务取消时，已写出的产物会留在
  最终路径（覆盖模式下旧成品会先被截断）；后续可改为 `.part` 写入 + 成功后原子
  rename，彻底避免半成品。
- **Windows 中文/非 ASCII 路径**：媒体层已改为向 FFmpeg 传 UTF-8 路径（原为本地
  ACP）；Linux 非 ASCII 用例与 2026-11-15 Windows 真机全链路实测均通过。
  （本机若从仓库目录内启动 exe 会受“受限令牌”环境策略限制，见下方条目。）
- **动态链接 FFmpeg**：开发构建依赖 MSYS2 的 FFmpeg DLL；分发请用 `cxx_pkg`
  （`cxx_pkg.toml`，PE 依赖闭包会自动补齐第三方 DLL），或按
  `third_party/ffmpeg/build-ffmpeg.*` 构建静态库。
- **视频拆帧占磁盘**：1080p 长视频拆帧可占数 GB 临时空间（与旧 Python 方案同量级）；
  无断点续传，中断后临时目录由任务清理逻辑回收。
- **超分引擎仍是外部 exe**：官方 v0.2.0；退出码不可信（失败也返回 0），成功判定依赖
  产物存在 + stderr 关键字；进度输出全在 stderr。
- **本机需管理员启动**：本机 Windows 环境以普通权限启动无法打开窗口，自动化验收可用
  `tools/capture_admin.ps1`（提权内部完成启动/截图/结束）。
- **点击级回归**：Linux 下已用 Xvfb + xdotool 做自动化点击/键盘回归（本轮新增 60+
  控件矩阵）；Windows 真机非引擎交互回归已完成（task-29 A 组：PASS 45/59、0 产品缺陷，
  含 UIA/真实鼠标键盘 + 离屏视觉 16/16）；剩余 DPI/系统语言/原生对话框深度等 MANUAL 项
  与 engine-gated 36 条待环境隔离后补。
- **单任务串行**：当前版本同一时刻只处理一项任务，没有可配置的并发数（设置页如实说明）。
- **Windows 真机已验收（2026-11-15）**：真实构建（84 目标 0 警告）、ctest 2/2（当时；
  当前全量 3/3，新增 i18n 测试）、
  `resr_tests` 16/16 + `resr_media_tests` 10/10（0 skip，真实 PE 引擎 + Vulkan）、
  四条真实链路、真实引擎暂停/停止/关闭、Win32 singleproc、出包（打包方式已切换为
  cxx_pkg 自包含 zip，仓库外解压可启动）均已实测通过；详见 `docs/design/交接文档.md` §7.3 与
  `docs/design/windows-build-env.md`。task-29 非引擎交互回归已完成（PASS 45/59、
  0 产品缺陷）；剩余 DPI/系统语言/原生对话框深度等 MANUAL 项与 engine-gated 36 条
  （真实引擎四链路/编码矩阵/暂停停止/性能）待环境隔离后补。
- **Windows 仓库目录受限令牌（本机环境现象）**：本机对从仓库目录内启动的 exe 施加
  受限令牌（只能写仓库内路径），会让引擎在“中文/外部路径”上失败；复制到仓库外或使用
  dist 即正常，非产品缺陷。
- **clang-format 版本差**：Windows MSYS2 自带 23.1.5、Linux 开发机 21.1.8，对少量基线
  文件换行/对齐决策不同，Windows 上 `standardize --check` 可能报 4 个未改文件差异；
  建议 CI 固定同一版本（21.x）。

## 许可证

本项目仅供学习和研究使用。第三方组件许可见 `third_party/*/LICENSE` 与关于页
「开源许可」对话框（含 Lucide 图标 ISC 许可、FFmpeg LGPL 等）。

## 致谢

- [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) - 超分辨率算法
- [Real-ESRGAN-ncnn-vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan) - Vulkan 实现
- [FFmpeg](https://ffmpeg.org/) - 视频处理
- [Qt](https://www.qt.io/) - GUI 框架
- [Lucide](https://lucide.dev/) - 图标（ISC）
