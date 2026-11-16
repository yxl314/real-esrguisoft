# 架构说明（Layered MVC）

本应用按照 **分层 MVC（Model-View-Controller）** 架构组织，依赖方向为：

```
┌─────────────────────────────────────────────────────────────┐
│  View 层   src/view/                                        │
│  界面组件与渲染（不包含业务规则，只做展示与用户手势转发）          │
│  MainWindow → panels/*（FunctionModeGroup / ProcessModeGroup │
│  / MediaPreviewPanel / MediaPlayerWidget / BasicSettingsPanel│
│  / VideoSettingsGroup / AdvancedSettingsPanel / ControlPanel /│
│  / ProgressPanel ...）+ Settings / About 页面（设置页、关于页）│
│  对外契约：IMainView（信号=用户手势，虚方法=状态下发）           │
└──────────────────────────┬──────────────────────────────────┘
                           │ 仅通过 IMainView / IDialogService
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  Controller 层  src/controller/                             │
│  AppController：会话状态（SessionState）、流程决策、状态渲染分发 │
│  IDialogService / QtDialogService：对话框抽象                │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  Model 层  src/model/                                      │
│  ProcessService（任务编排 / 状态广播 / 分派原生处理服务）        │
│  ImageProcessService（图片原生链路：单文件 / 批量）             │
│  VideoProcessService（视频原生链路：工作线程 + 三阶段状态机）    │
│  OutputPathResolver / InputScanner / BatchProgressWatcher   │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  Infrastructure 层  src/infrastructure/                     │
│  ExeRunner（外部进程生命周期：合并输出 / 暂停恢复 / 进程树终止） │
│  ExeCommand（引擎定位：用户设置优先 → 自动发现回退；参数构造）  │
│  ExeOutputParser（stderr 进度百分比 / 失败标记解析）           │
│  media/（libav* 封装：MediaProbe / FrameDecoder / VideoEncoder）│
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  Domain 层  src/domain/                                    │
│  MediaSettings（值对象与枚举）/ ProcessStatus（状态快照）      │
│  FileService（纯 QtCore 文件工具：扩展名/扫描/大小格式化）      │
│  无任何 Qt 界面依赖                                           │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  Common 层  src/common/                                    │
│  Constants（共享常量）/ Logger（跨层日志兼容层）               │
│  AppSettings（QSettings 用户设置：engine/path 引擎路径覆盖）   │
│  Logger 底层是 third_party/stacklog：保留 LOG_* 宏与          │
│  Logger::instance() 接口，日志带调用点 文件:行号并按大小轮转     │
│  被所有上层共享，自身不依赖任何层                               │
└─────────────────────────────────────────────────────────────┘
```

## 第三方库（third_party/）

| 库 | 用途 | 接入点 |
| --- | --- | --- |
| `stacklog` | 日志落盘、级别过滤、按大小轮转 | `src/common/logger.cpp` 兼容层；`resr_core` 链接 |
| `singleproc` | 单实例守卫与二次启动门铃（POSIX + Windows） | `main.cpp`；可执行程序链接 |

两个库都以 `add_subdirectory(... EXCLUDE_FROM_ALL)` 方式内嵌（见根
`CMakeLists.txt`），各自保留独立的 CMakeLists、测试、LICENSE 与文档；
另外各带一份 `.pri`，保留 qmake 可选接入能力。`stacklog` 在内嵌构建中沿用
工程的 C++17 标准，启用 `std::filesystem` 以正确处理中文日志路径。

## 设置页与测试入口

- **设置页**（`MainWindow::create_settings_page`）包含 5 张卡：外观（浅色 /
  深色 / 跟随系统）、超分引擎路径（选择文件 / 选择目录 / 恢复自动发现，
  覆盖值经 `Common/AppSettings` 持久化到 QSettings `engine/path`）、日志
  （展示 `<程序目录>/logs/` 并提供打开按钮）、处理默认值（恢复出厂参数）、
  运行环境（Qt 版本 / 引擎 / 单任务串行说明 / 图标许可）。关于页展示版本
  （由 CMake `project(VERSION ...)` 经 `RESR_APP_VERSION` 注入，不硬编码）、
  许可对话框与致谢。
- **引擎路径优先级**：设置页手动指定（bin 目录 / bin/realesrgan 目录 /
  可执行文件本身）→ 合法则优先使用；无效则回退「从程序目录向上查找
  bin/realesrgan」并在设置页提示。每次开始处理时重新解析，改设置无需重启。
- **测试入口**：`tests/` 下两个 ctest 目标（`resr_tests` 编排/参数级用例、
  `resr_media_tests` 媒体层）和手工验收工具 `resr_pipeline_verify`
  （不注册 ctest，从任意嵌套构建目录运行，会自动向上查找工程根）。
  测试工作目录在构建目录下按 PID 隔离并自清理。

## 分层原则

1. **依赖单向向下**：View → Controller → Model → Infrastructure → Domain。
   Domain 不依赖任何上层；Model 不依赖 View/Controller。
2. **View 不感知业务规则**：MainWindow 只是面板装配器；联动逻辑（模型
   列表随图片类型变化、输出格式随媒体模式变化等）归属对应面板，业务决策
   全部由 Controller 完成。
3. **Controller 面向接口**：只依赖 `IMainView` 与 `IDialogService`，
   可替换视图实现或对话框实现，便于测试。
4. **状态通过快照下发**：处理管线只发一个 `ProcessStatus` 结构化快照
   （生命周期 + 进度 + 当前文件 + 描述文案），取代原先 7 个松散信号，
   下游一次性重建界面，避免状态错序。

## 重构前 → 重构后对照

| 关注点 | 重构前 | 重构后 |
| --- | --- | --- |
| MainWindow | 1217 行上帝类（UI + 视频播放 + 预览 + 状态） | 纯装配器；预览 / 视频 / 设置各自成面板 |
| 设置类型 | 位于 `view/settings.h`，Model 反向依赖 View | 收敛为 `domain/MediaSettings`，单一结构体 |
| ImageSettings/VideoSettings | 继承 + 向下转型 | 合并为统一 `MediaSettings`（视频字段默认值） |
| 输入/输出视频逻辑 | 两份几乎相同的 QMediaPlayer 代码 | 复用 `MediaPlayerWidget` |
| 进度广播 | progressChanged/stateChanged/statusMessageChanged/currentFileChanged… | 单一 `ProcessStatus` 快照信号 |
| 用户取消 | 与失败路径混淆（取消也会弹错误框） | 独立 `stopped()` 信号，取消不算失败 |
| 对话框 | QFileDialog/QMessageBox 散落控制器 | `IDialogService` 抽象 |
| 命令构造 | ExeRunner 内混着 700 行 | `ExeCommand`（纯函数）+ `ExeOutputParser` 拆分 |
| 图片 / 视频编排 | 调用 4 个 Python 打包 exe，参数以命令行字符串二次解析 | 全部 C++ 原生：`ImageProcessService` / `VideoProcessService` 直接驱动引擎 |
| 视频处理 | ffmpeg.exe 拆帧 / 合成，ffprobe 探测 | 进程内 libav*（`src/infrastructure/media/`），不再依赖外部 FFmpeg 可执行文件 |
| 进度来源 | 解析 Python stdout 的 `PROGRESS:` | 解析引擎 stderr 的 `xx.xx%` + 输出目录文件数轮询 + 媒体层帧计数 |
| 成功判定 | 只看子进程退出码（引擎失败时仍为 0） | 退出码 + 失败标记 + 产物存在且非空三重校验 |

## 关键流程

```
用户点击「开始处理」
  → ControlPanel::startRequested
  → IMainView::startRequested         (View 手势)
  → AppController::onStartRequested   (Controller 决策)
  → ProcessService::start(request)    (Model 编排)
      ├─ 图片：ImageProcessService
      │    ├─ 单文件：OutputPathResolver → ExeRunner(realesrgan，直接写目标文件)
      │    └─ 批量：  引擎目录模式一次处理 + BatchProgressWatcher 轮询输出目录
      └─ 视频：VideoProcessService（独立工作线程）
           ├─ MediaProbe 探测 → FrameDecoder 拆帧为 PNG 序列
           ├─ ExeRunner(realesrgan 目录模式) 批量超分
           └─ VideoEncoder 编码合成（原音频优先 stream copy）
  → status_changed(ProcessStatus)     (单一快照)
  → AppController::onStatusChanged → IMainView::applyStatus
  → ProgressPanel::applyStatus        (View 渲染)
```

## 引擎与媒体层边界

- `realesrgan-ncnn-vulkan.exe` 仍是外部进程（算法引擎，Vulkan 实现），由
  `ExeRunner` 管理生命周期；它的进度与错误全部走 stderr，因此 QProcess 使用
  `MergedChannels` 并按行解析。
- 视频不再调用 `ffmpeg.exe` / `ffprobe.exe`：探测、拆帧、编码都由
  `src/infrastructure/media/` 直接链接系统 FFmpeg 开发库完成（详见
  `docs/design/ffmpeg-integration.md`），媒体层是阻塞式同步接口，因此
  `VideoProcessService` 在独立 `QThread` 中调用它并通过信号回到 UI 线程。
- 引擎「退出码不可信」：realesrgan 处理失败时仍返回 0，只打印
  `decode/encode image ... failed`。模型层统一按「进程正常 + 无失败标记 +
  产物存在且非空」判定成功（`ExeOutputParser` 负责提取失败标记）。