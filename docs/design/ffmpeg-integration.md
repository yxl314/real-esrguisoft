# FFmpeg 集成说明（libav* 媒体基础设施）

> 本文说明工程如何用 **内嵌 FFmpeg 9.0.2 源码 + 系统/自建 libav\* 库** 取代旧
> `bin/ffmpeg/`（142MB、Windows-only 预编译 exe/DLL），以及 `src/infrastructure/media/`
> 媒体封装层的 API、线程模型、取消语义、许可证与已知限制。
>
> 相关代码：
> [`cmake/FindFFmpeg.cmake`](../../cmake/FindFFmpeg.cmake)、
> [`cmake/MediaFFmpeg.cmake`](../../cmake/MediaFFmpeg.cmake)、
> [`src/infrastructure/media/`](../../src/infrastructure/media/)、
> [`tests/test_media.cpp`](../../tests/test_media.cpp)。

---

## 1. 背景与目标

旧工程通过 `bin/ffmpeg/bin/{ffmpeg.exe,ffprobe.exe}` 启动外部进程完成视频拆帧 / 合成，
由 `bin/src/video.py`、`video_batch.py`、`core.py` 拼命令行并解析 `PROGRESS:` 输出。
这带来三个问题：

1. **平台绑定**：预编译包只有 Windows x64 的 exe/DLL，Linux 无法运行；仓库体积 +142MB。
2. **进度粗**：跨进程只能拿到文本进度，拆帧/合成阶段没有细粒度进度。
3. **颜色/参数不可控**：命令行模板固定，bt709/bt601 标签依赖 ffmpeg 默认行为。

本任务的做法：

- **移除** `bin/ffmpeg/` 整个目录（已删除，仓库不再含任何预编译 FFmpeg 二进制）；
- 在 `third_party/ffmpeg/ffmpeg-9.0.2/` **内嵌 FFmpeg 9.0.2 官方源码**，提供
  Windows/Linux 静态库构建脚本，可复现地自建依赖；
- 默认仍**优先使用系统 FFmpeg 开发包**（pkg-config），零构建即可开发；
- 新增 `src/infrastructure/media/` 封装层，用 **libavformat/libavcodec/libswscale/
  libswresample** 直接实现探测、拆帧、编码，替代 `ffprobe.exe` / `ffmpeg.exe` 调用。

---

## 2. 目录结构

```text
real-esrguisoft/
├── cmake/
│   ├── FindFFmpeg.cmake              # 查找 libav*（pkg-config → third_party 构建产物）
│   └── MediaFFmpeg.cmake             # 定义 resr_media_ffmpeg / resr_media 并挂到 resr_core
├── third_party/ffmpeg/
│   ├── README.md                     # 版本策略 / 裁剪说明 / 许可证摘要
│   ├── build-ffmpeg.sh               # 跨平台静态库构建脚本（MSYS2 / Linux）
│   ├── build-ffmpeg.ps1              # Windows PowerShell 包装器（发现 MSYS2 bash）
│   ├── .gitignore                    # 排除 build-ffmpeg-*/
│   └── ffmpeg-9.0.2/                 # 官方源码（去掉 tests/doc/tools/.forgejo）
│       ├── configure
│       ├── libavformat/ libavcodec/ libavutil/ libswscale/ libswresample/
│       ├── libavfilter/ libavdevice/ ffbuild/ compat/ presets/
│       └── COPYING.LGPLv2.1 / COPYING.LGPLv3 / COPYING.GPLv2 / COPYING.GPLv3
├── src/infrastructure/media/
│   ├── mediaerror.h/.cpp             # 统一错误码 + MediaError
│   ├── mediaframe.h/.cpp             # frame%08d.png 命名/列举约定
│   ├── ffmpeg_raii.h                 # libav* 资源 RAII（extern "C" 包含收口）
│   ├── ffmpegutils.h/.cpp            # 初始化/错误文本/帧率/色彩空间换算
│   ├── mediaprobe.h/.cpp             # MediaProbe：容器/视频流/音频流探测
│   ├── framedecoder.h/.cpp           # FrameDecoder：逐帧解码为 PNG 序列
│   └── videoencoder.h/.cpp           # VideoEncoder：PNG 序列 + 音频 → 目标容器
└── tests/test_media.cpp              # Qt Test 冒烟：探测 → 拆帧 30 帧 → 编码 → 再探测
```

已删除：`bin/ffmpeg/`（原 `bin/`、`include/`、`lib/` 共 142MB 预编译包）。
`bin/realesrgan/`（超分引擎）与媒体层无关，保留。

---

## 3. 依赖查找与链接

### 3.1 `cmake/FindFFmpeg.cmake` 的解析优先级

1. **pkg-config（首选）**：`pkg_check_modules(RESR_FFMPEG_PC ... libavformat
   libavcodec libavutil libswscale libswresample)`。
   - 头文件取 `RESR_FFMPEG_PC_INCLUDE_DIRS`；
   - 链接库优先取带路径的 `RESR_FFMPEG_PC_LINK_LIBRARIES`（CMake ≥3.12），
     回退 `RESR_FFMPEG_PC_LIBRARIES` + `LIBRARY_DIRS`；
   - 版本取 `libavcodec` 的版本（本机 FFmpeg 9.0.2 → `63.1.102`）。
   - MSYS2 安装：`pacman -S mingw-w64-ucrt-x86_64-ffmpeg`。
2. **手动安装前缀（其次）**，候选根目录按顺序：
   - `-DFFMPEG_ROOT=<install 目录>`；
   - `third_party/ffmpeg/build-*/install`、`build/install`、`install/`。
   每个根目录：
   - 若存在 `<root>/lib/pkgconfig` → 执行 `pkg-config --static --libs <5 个模块>`，
     自动带上静态库全部依赖（zlib、lzma、ws2_32 等）；成功后直接采用；
   - 否则用 `find_path` 找 `libavcodec/avcodec.h`、`find_library` 逐个找 5 个库
     （适用于共享库或依赖简单的静态库）。
3. 都找不到时给出**可执行的修复提示**并 `FATAL_ERROR`：
   安装系统 FFmpeg 开发包 / 运行 `build-ffmpeg.ps1|sh` 并设置 `FFMPEG_ROOT`。

输出变量：`FFMPEG_FOUND`、`FFMPEG_INCLUDE_DIRS`、`FFMPEG_LIBRARIES`、
`FFMPEG_LIBRARY_DIRS`、`FFMPEG_VERSION`；并创建导入目标 **`FFmpeg::FFmpeg`**。

### 3.2 `cmake/MediaFFmpeg.cmake` 与根 CMakeLists

根 `CMakeLists.txt` 在 `find_package(Qt6 ...)` 之后：

```cmake
# 必须 PREPEND：Qt6Config 会把 Qt 自带模块目录（其中也有一个 FindFFmpeg.cmake）
# 追加进 CMAKE_MODULE_PATH，若用 APPEND，Qt 的模块会先命中，只找到 3 个 libav 库。
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
```

在 `resr_core` 定义之后：

```cmake
include(MediaFFmpeg)   # 找到 FFmpeg + 构建媒体层 + 挂到 resr_core
```

`MediaFFmpeg.cmake` 做三件事：

1. `find_package(FFmpeg REQUIRED)`；
2. 建 **`resr_media_ffmpeg`**（INTERFACE）：对外只暴露 FFmpeg 头文件与 libav\* 链接；
3. 建 **`resr_media`**（STATIC）：编译 `src/infrastructure/media/*.cpp`，
   `PUBLIC` 链接 `resr_media_ffmpeg`、`Qt6::Core`、`Qt6::Gui`，
   并 `target_link_libraries(resr_core PUBLIC resr_media)`。上层（Model/测试）
   通过 `resr_core` 即可使用媒体能力，无需单独写链接。

### 3.3 Windows 运行时注意

用系统 pkg-config 的 **动态** FFmpeg 时，程序/测试运行时需要 `avcodec-63.dll`、
`avformat-63.dll` 等：确保 `C:\msys64\ucrt64\bin` 在 `PATH`（本仓库的构建/测试命令
均如此），或发布时把 DLL 拷到 exe 同目录。使用 3.4 的静态库构建产物则无此依赖。

---

## 4. 内嵌源码与构建脚本

### 4.1 版本与获取

| 项目 | 值 |
| --- | --- |
| 版本 | **FFmpeg 9.0.2**（与本机 MSYS2 开发包 9.0.2 一致） |
| 来源 | <https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz> |
| tar.xz 大小 / SHA256 | 11.5 MB / `8C3850283EB25FA026482078A04051E0BE17347B09EF81A0849BEC15A96E002E` |
| 目录 | `third_party/ffmpeg/ffmpeg-9.0.2/`（不含 `.git`） |
| 库版本 | libavcodec 63.1.102 / libavformat 63.1.102 / libavutil 61.1.102 / libswscale 10.1.102 / libswresample 7.1.102 |

裁剪掉与库构建无关的目录以控制仓库体积：`tests/`、`doc/`、`tools/`、`.forgejo/`
（恢复完整上游源码的方法见 [`third_party/ffmpeg/README.md`](../../third_party/ffmpeg/README.md)）。
`libavfilter/`、`libavdevice/`、`fftools/` 虽不参与本次构建，仍保留以保证 `configure`
能正常解析依赖（删除它们 configure 会找不到 `libavfilter/allfilters.c` 等文件）。

### 4.2 构建脚本

**Linux / MSYS2**：

```bash
cd third_party/ffmpeg
bash build-ffmpeg.sh                 # 默认构建 9.0.2 静态库（含 x264/x265 → GPL）
bash build-ffmpeg.sh --lgpl-only     # LGPL 组合（无 H.264/H.265 编码器）
bash build-ffmpeg.sh --clean --jobs 8
bash build-ffmpeg.sh --help
```

**Windows（PowerShell）**：

```powershell
cd third_party\ffmpeg
.\build-ffmpeg.ps1                  # 自动定位 C:\msys64\usr\bin\bash.exe 后调用 .sh
.\build-ffmpeg.ps1 -LGPLOnly -Jobs 8
.\build-ffmpeg.ps1 -Version 9.0.2 -Clean
```

脚本行为：

- 自动进入 `ffmpeg-<version>/` 的 **out-of-tree** 构建（源码树保持干净）；
- MSYS 环境下自动 `export MSYSTEM=UCRT64` 并前置 `/ucrt64/bin`（FFmpeg `configure`
  拒绝 MSYS 原生环境，必须走 MINGW/UCRT64 目标）；
- 默认 `configure` 参数：`--disable-shared --enable-static --disable-programs
  --disable-doc --disable-avdevice --disable-avfilter --disable-network --disable-debug
  --disable-autodetect`，按 pkg-config 探测结果按需追加 `--enable-libvpx
  --enable-libsvtav1`，非 `--lgpl-only` 时再追加 `--enable-gpl --enable-libx264
  --enable-libx265`；未安装 `nasm` 时自动追加 `--disable-x86asm`；
- 产物：`third_party/ffmpeg/build-ffmpeg-<version>-<platform>/install/`
  （`include/`、`lib/*.a`、`lib/pkgconfig/*.pc`），已被 `.gitignore` 排除；
- 构建完成后用 `cmake -S . -B build -DFFMPEG_ROOT=<install 目录>` 即可替换系统依赖。

### 4.3 本次对脚本的验证（未真正编译整个 FFmpeg）

按任务要求不进行完整源码编译（耗时过长），已完成：

| 验证项 | 命令 | 结果 |
| --- | --- | --- |
| 脚本帮助 | `bash build-ffmpeg.sh --help` | 正确打印用法 + 许可证提示，退出码 0 |
| 非法参数 | `bash build-ffmpeg.sh --nope` | 打印中文错误，退出码 **2** |
| PowerShell 语法 | `[Parser]::ParseFile(...)` | 无语法错误 |
| 内嵌源码可配置 | 在临时目录对 `third_party/ffmpeg/ffmpeg-9.0.2/configure` 执行最小化参数 | **`CONFIGURE_EXIT=0`**，输出 `License: LGPL version 2.1 or later` |

完整构建命令（需要时执行，产物不提交）：

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
cd third_party\ffmpeg; .\build-ffmpeg.ps1 -Jobs 8
cd ..\..; cmake -S . -B build -DFFMPEG_ROOT=third_party/ffmpeg/build-ffmpeg-9.0.2-windows-x86_64/install
cmake --build build -j8
```

---

## 5. 媒体封装层 API（给上层调用者）

头文件位于 `src/infrastructure/media/`（已在 `RESR_INCLUDE_DIRS` 中，直接
`#include "mediaprobe.h"` 等）。**无命名空间、全局类**；依赖方向：Infrastructure → Domain
（仅引用 `mediasettings.h` 的 `VideoEncoding` 枚举）/ Qt Core+Gui。

### 5.1 错误模型（`mediaerror.h`）

不抛异常、不让异常穿过 Qt 事件循环。所有操作返回带 `MediaError` 的结果结构：

```cpp
enum class MediaErrorCode {
    NONE, INVALID_ARGUMENT, FILE_NOT_FOUND, OPEN_FAILED, STREAM_NOT_FOUND,
    UNSUPPORTED, PROBE_FAILED, DECODE_FAILED, ENCODE_FAILED, MUX_FAILED,
    IO_FAILED, CANCELLED, INTERNAL
};
struct MediaError {
    MediaErrorCode m_code = MediaErrorCode::NONE;
    QString m_message;                       // 可直接展示的中文文案
    bool is_ok() const;
    static MediaError make(MediaErrorCode, const QString &);
};
QString media_error_code_to_string(MediaErrorCode);
```

### 5.2 `MediaProbe`（`mediaprobe.h`）

替代旧 `ffprobe -v quiet -print_format json -show_format -show_streams`。

```cpp
struct MediaProbeResult { bool m_ok; MediaInfo m_info; MediaError m_error; };
class MediaProbe {
  public:
    static MediaProbeResult probe(const QString &v_path);   // 阻塞
};
```

`MediaInfo` 字段：

| 字段 | 含义 |
| --- | --- |
| `m_format_name` / `m_format_long_name` | 容器短名 / 长名 |
| `m_duration_seconds` | 容器时长（`format->duration` 缺失时取各流最大时长兜底） |
| `m_bit_rate` | 容器总码率（可能 0） |
| `m_has_video` / `m_has_audio` | 是否含对应流 |
| `m_video`（`VideoStreamInfo`） | `m_index`、`m_codec_name`、`m_width`、`m_height`、`m_pixel_format`、`m_frame_rate`（double）、`m_frame_rate_num/_den`（精确有理数，如 1199/50）、`m_frame_count`（`nb_frames` 元数据优先，否则时长×帧率估算）、`m_frame_count_estimated`、`m_bit_rate`、`m_color_space`（`bt709`/`bt601`/`bt2020`/`unknown`） |
| `m_audio`（`AudioStreamInfo`） | `m_index`、`m_codec_name`、`m_sample_rate`、`m_channels`、`m_channel_layout`、`m_bit_rate` |

> 帧数为**估算值**：容器有 `nb_frames`（如 MP4）时精确，否则按 `duration × fps`
> 四舍五入，`m_frame_count_estimated = true`。用于进度分母，不用于逐帧对齐。

### 5.3 `FrameDecoder`（`framedecoder.h`）

替代旧 `ffmpeg -i <输入> -qscale:v 1 ... <帧目录>/frame%08d.jpg`；改为无损 PNG。

```cpp
class FrameDecoder {
  public:
    using ProgressCallback = std::function<void(int v_frames_done, int v_frames_total)>;
    using CancelChecker    = std::function<bool()>;

    struct Options {
        int     m_max_frames = 0;                       // 0=全部；>0 只解码前 N 帧
        QString m_frame_prefix    = QStringLiteral("frame");
        QString m_frame_extension = QStringLiteral("png");
    };
    struct Result {
        bool      m_ok;
        int       m_frames_written;
        int       m_frames_total;    // 源视频总帧数（探测值）
        MediaInfo m_source;          // 探测结果，可直接喂给 VideoEncoder
        MediaError m_error;
    };

    void set_progress_callback(ProgressCallback);
    void set_cancel_checker(CancelChecker);
    Result decode_to_png_sequence(const QString &v_input_path,
                                  const QString &v_output_dir);
    Result decode_to_png_sequence(const QString &v_input_path,
                                  const QString &v_output_dir,
                                  const Options &v_options);
};
```

行为细节：

- 逐包解码、每帧立即写盘；输出目录不存在会自动 `mkpath`；
- **色彩正确**：按流的 `color_space`/`color_range`（帧级标注优先）选择 swscale 矩阵：
  `bt709 → ITU709`、`bt601 → ITU601`、`bt2020 → BT2020`，未标注时按画面高度猜
  （≥720 行用 BT.709，否则 BT.601），源有限/全范围按标签，目标固定全范围 RGBA8888；
- 输出 PNG 与 `ffmpeg` 自己解码的结果 **PSNR = inf（逐像素一致）**（见 §8 验证）；
- 进度：每写出 1 帧回调 `(已写帧数, 总帧数估算)`，开始时回调 `(0, total)`；
- 已知：不处理 rotate/display matrix 元数据（与旧 Python 行为一致）；10bit/HDR
  源会被 swscale 转成 8bit sRGB PNG。

### 5.4 `VideoEncoder`（`videoencoder.h`）

替代旧 `ffmpeg -framerate <fps> -i <帧目录>/frame%08d.jpg [-i <原视频>] ...` 合成命令。

```cpp
class VideoEncoder {
  public:
    using ProgressCallback = std::function<void(int v_frames_done, int v_frames_total)>;
    using CancelChecker    = std::function<bool()>;

    struct Options {
        VideoEncoding m_encoding = VideoEncoding::H264;  // 来自 domain/mediasettings.h
        int     m_crf   = 23;                            // 0-51（vp9/av1 允许 >51）
        QString m_preset = QStringLiteral("medium");     // 仅 x264/x265 生效
        double  m_frame_rate = 30.0;
        int     m_frame_rate_num = 0;                    // >0 时优先于 m_frame_rate
        int     m_frame_rate_den = 1;
        QString m_audio_source_path;   // 音频来源（通常是被拆帧的原视频）；空=无音频
        bool    m_copy_audio = true;   // 优先 stream copy；false 强制重编码
        double  m_audio_max_seconds = 0.0; // >0 时截断音频到该时长
    };
    struct Result {
        bool m_ok;
        int  m_frames_encoded;
        int  m_audio_packets;
        bool m_audio_copied;     // true=直通，false=重编码
        MediaError m_error;
    };

    void set_progress_callback(ProgressCallback);
    void set_cancel_checker(CancelChecker);
    Result encode_png_sequence(const QString &v_frames_dir,
                               const QString &v_output_path);
    Result encode_png_sequence(const QString &v_frames_dir,
                               const QString &v_output_path,
                               const Options &v_options);

    static QString encoder_name(VideoEncoding);            // libx264/libx265/libvpx-vp9/libsvtav1
    static bool is_encoder_available(VideoEncoding);       // 当前 FFmpeg 构建是否带该编码器
};
```

**容器由输出扩展名决定**（`avformat_alloc_output_context2` 自动推断）：
`.mp4` / `.mkv` / `.webm` / `.avi` / `.mov` 均支持；`.gif` 不支持（返回
`UNSUPPORTED`，上层需单独处理 GIF 分支）。

编码参数矩阵（与旧 Python `video.py:255-285 / 288-405` 行为对齐）：

| 编码 | 编码器名 | CRF 选项 | preset | 说明 |
| --- | --- | --- | --- | --- |
| h264 | `libx264` | `crf` | `preset` | 默认 |
| h265 | `libx265` | `crf` | `preset` | |
| vp9 | `libvpx-vp9` | `crf` | 不传（与旧行为一致） | `bit_rate=0` + CRF 模式 |
| av1 | `libsvtav1` | `crf` | 不传 | `bit_rate=0` + CRF 模式 |

**颜色**：输出统一 `yuv420p`；BT.709（≥720 行）/ BT.601（SD）按高度选择，
RGB→YUV（swscale，源全范围 → 目有限范围）与写出流的 `color_primaries/
color_trc/color_space/color_range(tv)` 标注一致。宽/高为奇数时补一像素黑边
（Y=16/U=V=128），保证 yuv420p 合法。

**音频策略**（两段式）：

1. `m_copy_audio == true` 且容器接受源音频编码（`avformat_query_codec` 判定，
   matroska 系放宽）→ **stream copy**：只重写时间戳（对齐 0 起点）并 mux；
2. 否则 → **重编码**：解码 → `libswresample` 重采样到编码器要求（采样率/
   采样格式/声道布局按 `avcodec_get_supported_config` 协商，失败回退立体声）→
   编码：webm 用 `libopus`，其余用 `aac`（128 kbps）。

音频包在视频包全部写完后写入（同一文件内合法，`av_write_frame` 不做跨流缓冲）；
输出时长为音频/视频实际内容较长者，调用方可用 `m_audio_max_seconds` 截断音频到
视频时长。

### 5.5 线程模型、进度与取消

| 关注点 | 约定 |
| --- | --- |
| 阻塞性 | `MediaProbe::probe` / `FrameDecoder::decode_to_png_sequence` / `VideoEncoder::encode_png_sequence` **全部同步阻塞**，禁止在 UI 线程直接调用；请放到 `QThread` / `QThreadPool` / 工作线程 |
| 回调线程 | 进度 / 取消回调在**调用线程同步触发**（不排队、不跨线程、不依赖事件循环）；回调内不要做长耗时操作，也不要直接操作 GUI |
| 进度语义 | `(已处理帧数, 总帧数)`：拆帧为「已写 PNG 数 / 源总帧数估算」，编码为「已编码帧数 / PNG 文件数」，均从 0 开始、成功时到达 `(N, N)`；上层自行映射百分比（`done*100/total`） |
| 取消 | `set_cancel_checker(std::function<bool()>)`，每帧/每包轮询一次（也可在外部用 `std::atomic_bool` 捕获）。取消后返回 `m_ok=false, m_error.m_code=CANCELLED`，**已写文件保留**，临时目录由上层负责清理 |
| 异常 | 任何路径都不抛异常；RAII 保证 `AVFormatContext/AVCodecContext/AVFrame/AVPacket/SwsContext/SwrContext` 必定释放 |

### 5.6 帧序列与临时目录约定

- 帧文件名固定 `frame%08d.png`，序号从 **1** 起（`frame00000001.png`），
  `FrameDecoder` 输出与 `VideoEncoder` 输入约定一致（等价旧 `frame%08d.jpg`）；
- 帧序列**必须尺寸/格式一致**；编码时按文件名字典序（8 位补零即帧序）；
- 建议临时目录布局（与旧 Python 一致，由上层 Model 层创建/删除）：

```text
<临时根>/frames/         # FrameDecoder 输出 PNG
<临时根>/processed/      # 超分引擎输出（上层负责）
<临时根>/processed/frames_<model>[_x]/  # 与旧 image_batch 输出目录规则一致
```

- 媒体层自身只创建传入的输出目录，不删除任何目录；崩溃/取消后的残留由上层清理。

### 5.7 集成示例（Model 层视频链路）

```cpp
// 1) 探测（工作线程）
MediaProbeResult probe = MediaProbe::probe(input_path);
if (!probe.m_ok) { /* probe.m_error.m_message 上报 */ }

// 2) 拆帧：30 帧预览或全部
FrameDecoder decoder;
decoder.set_progress_callback([&](int done, int total) { /* 10% 权重段 */ });
decoder.set_cancel_checker([&] { return cancelled.load(); });
FrameDecoder::Result split = decoder.decode_to_png_sequence(input_path, frames_dir);
// ... 调 realesrgan-ncnn-vulkan 处理 frames_dir → processed_dir（上层负责）...

// 3) 合成
VideoEncoder encoder;
encoder.set_progress_callback([&](int done, int total) { /* 15% 权重段 */ });
encoder.set_cancel_checker([&] { return cancelled.load(); });
VideoEncoder::Options options;
options.m_encoding     = settings.m_video_encoding;
options.m_crf          = crf;
options.m_preset       = preset;
options.m_frame_rate   = probe.m_info.m_video.m_frame_rate;
options.m_frame_rate_num = probe.m_info.m_video.m_frame_rate_num;
options.m_frame_rate_den = probe.m_info.m_video.m_frame_rate_den;
options.m_audio_source_path = input_path;   // 保留原音频
options.m_copy_audio = true;
VideoEncoder::Result merged = encoder.encode_png_sequence(processed_dir, output_path, options);
```

### 5.8 旧 Python / ffmpeg 命令行的行为映射

| 旧行为（`bin/src/*.py`） | 新实现 |
| --- | --- |
| `ffprobe -show_format -show_streams` JSON 解析宽高/帧率/音频 | `MediaProbe::probe()` 直接读 `AVFormatContext` |
| 帧率取 `avg_frame_rate`，分母 0 回退 30 | `avg_frame_rate → r_frame_rate → av_guess_frame_rate`，拿不到时调用方回退 |
| `ffmpeg -i in -qscale:v 1 -qmin 1 -qmax 1 -vsync 0 frame%08d.jpg` | `FrameDecoder` 逐帧解码为无损 `frame%08d.png`（`-vsync 0` 丢弃时间戳语义等价于逐帧输出） |
| `ffmpeg -framerate fps -i frames/frame%08d.jpg -c:v libx264 -crf N -preset P -pix_fmt yuv420p -r fps` | `VideoEncoder` + `EncoderChoice{libx264, crf, preset}`，`pix_fmt=yuv420p`，time_base=`1/fps` |
| `-c:a copy`（失败无回退） | `m_copy_audio` 优先 copy；容器不接受时自动重编码（aac/libopus） |
| `.gif` 分支 `-vf fps -loop 0` | **未实现**，返回 `UNSUPPORTED`（见 §7） |
| `PROGRESS:xx` 文本解析 | 替换为 `(done,total)` 细粒度回调 |
| `bin/ffmpeg` 外部 exe | 进程内 libav\* 链接，无外部进程 |

---

## 6. 许可证与分发注意事项

- FFmpeg 本体：**LGPL v2.1 或更高**（`third_party/ffmpeg/ffmpeg-9.0.2/COPYING.LGPLv2.1`）。
- **默认构建脚本会启用 `--enable-gpl --enable-libx264 --enable-libx265`**：x264/x265
  是 GPL，产出的静态库整体构成 **GPL 组合作品**。对外分发包含该产物的程序时，
  整个程序须按 GPL 提供源码/相应许可。
- 需要 **LGPL 闭源分发**时：用 `build-ffmpeg.sh --lgpl-only` 构建（或系统装的
  LGPL-only 版本）。此时 **H.264/H.265 编码器不可用**（`VideoEncoder` 返回
  `UNSUPPORTED`，上层应提示用户或降级），VP9/AV1（libvpx/SVT-AV1，BSD 系）仍可用。
  只做解码、不做 H.264/H.265 编码的部署可直接用 LGPL-only。
- 使用 **系统 FFmpeg（MSYS2 包）**时许可证取决于该包编译选项：MSYS2 的
  `mingw-w64-ucrt-x86_64-ffmpeg` 是含 GPL 组件（x264/x265）的构建。闭源分发请查阅
  该包的具体 configure 参数。
- **静态链接 LGPL 库**的分发义务：需提供重新链接（relink）所需的目标文件或源码，
  并在分发物中附带 LGPL 许可文本与所用库版本/修改说明。动态链接（系统 pkg-config
  方案）则要求随程序提供 DLL 与许可文本。
- 本仓库**只内嵌源码与脚本，不提交任何预编译 FFmpeg 二进制**；构建产物
  `third_party/ffmpeg/build-ffmpeg-*/` 已被 `.gitignore` 排除。

---

## 7. 已知限制

| 项 | 状态 | 说明 / 后续 |
| --- | --- | --- |
| 音频 stream copy 失败回退 | **已实现** | `m_copy_audio=true` 但容器拒绝源音频编码时自动重编码（webm→libopus，其余→aac）；`m_copy_audio=false` 强制重编码。音频包在视频之后写入（同一文件合法，但非严格交错，长文件对边下边播不友好，需要时可引入交叉写入） |
| bt709/bt601 色彩 | **已实现并验证** | 解码按流/帧标注选矩阵、范围，未标注按分辨率猜；与 ffmpeg 原生解码结果 PSNR=inf。HDR/10bit 源降为 8bit sRGB，不保留 HDR 元数据（与旧 Python 相同） |
| 取消语义 | **已实现** | 每帧/包轮询 `CancelChecker`，返回 `CANCELLED`；已写 PNG/半成品容器**保留**，由上层清理临时目录（媒体层不删目录）；取消后不会调用 `av_write_trailer`，半成品文件不可播放 |
| Windows 长路径 | **未特殊处理（已知限制）** | 路径用 Qt `QString`/`QFile::encodeName` 传给 FFmpeg 的 UTF-8 接口，>260 字符路径在未开启 `\\?\` 长路径策略的 Windows 10/11 上可能失败；上层建议在临时目录选择短路径（如 `%TEMP%`，上层自行管理） |
| GIF 输出 | **不支持** | 返回 `UNSUPPORTED`；旧 Python 的 gif 分支需由上层另实现（本工程 UI 目前未提供 gif 视频输出） |
| 奇数宽高 | **已处理** | yuv420p 要求偶数，编码前补黑边（1 像素），不改变可见内容 |
| 旋转/Display Matrix | **未处理** | 手机竖拍视频的 rotate 元数据未应用，输出方向与旧 Python 行为一致（同样未应用） |
| 多视频流/字幕/章节 | **不支持** | 只处理「最佳」视频流与「最佳」音频流；不复制字幕/章节/附件，与旧 Python 一致 |
| 硬件加速 | **未使用** | 纯 CPU 软解/软编（与旧 ffmpeg.exe 默认行为一致，编码速度取决于 x264 preset） |
| 音频交叉写入 | **已知限制** | 见第 1 行说明（先视频后音频，不严格 interleave） |

---

## 8. 验证记录（本机 Windows 11 + MSYS2 UCRT64, FFmpeg 9.0.2）

测试文件：[`tests/test_media.cpp`](../../tests/test_media.cpp)，素材
`input/video/1.mp4`（640×480、H.264、23.976fps、181 帧、7.549s、AAC 48kHz 立体声）。

命令：

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
cmake --build build -j8
ctest --test-dir build -R media --output-on-failure
```

实际输出（`resr_media_tests.exe`）：

```text
PASS   : MediaLayerTest::initTestCase()
PASS   : MediaLayerTest::probe_real_video()
PASS   : MediaLayerTest::decode_first_frames()
PASS   : MediaLayerTest::encode_mp4_with_audio_copy()
PASS   : MediaLayerTest::encode_mkv_with_audio_transcode()
PASS   : MediaLayerTest::cancel_during_decode()
PASS   : MediaLayerTest::cleanupTestCase()
Totals: 7 passed, 0 failed, 0 skipped
```

覆盖的断言：

1. **探测**：`m_ok`、有视频/音频流、`h264`、640×480、`1199/50`（≈23.98fps）、
   帧数 181 且非估算、时长 7.0–8.0s、音频 AAC 48000Hz/2 声道；
2. **拆帧**：只解码前 30 帧 → 目录内恰好 30 个 `frame*.png`、首尾文件名正确、
   首帧 PNG 640×480 可读、进度回调单调且 `(30, 181)`、绿色通道无异常（色彩矩阵
   sanity）；
3. **合成（音频 copy）**：30 帧 + 原音频 → `out_h264_copy.mp4`，`m_audio_copied=true`、
   音频包 >0、产物 >1KB；再探测：640×480、30 帧、时长≈1.25s（±0.5）、含音频；
4. **合成（音频重编码）**：`m_copy_audio=false` → `out_h264_transcode.mkv`，
   `m_audio_copied=false`、有音频包；再探测：分辨率/时长正确、含音频；
5. **取消**：`CancelChecker` 在第 6 次轮询返回 true → `m_ok=false`、
   `m_error.m_code==CANCELLED`、已写帧数在 1..19 之间。

额外客观验证：

- **色彩**：用系统 ffmpeg 解出第 1 帧作参考，对 `frame00000001.png` 计算 PSNR：
  `PSNR r:inf g:inf b:inf a:inf average:inf`（逐像素一致，证明 YUV→RGB 矩阵/范围正确）；
- **输出容器**（`ffprobe`）：`out_h264_copy.mp4` → `h264, 640x480, 30 帧,
  color_space=smpte170m(bt601), color_range=tv, 帧间隔 0.041701s(23.976fps)`，
  音频 `aac 48000Hz 2ch`；`out_h264_transcode.mkv` 同样含视频+音频；
- **构建脚本**：见 §4.3（`--help`、非法参数、PS1 语法、内嵌源码 configure 退出码 0）；
- **仓库清理**：`bin/ffmpeg/`（186 文件 / 152MB）已删除，`git status` 不再出现该目录；
  工程根 `build-*/` 为本地构建产物，不应提交。

---

## 9. 常见问题（FAQ）

| 现象 | 处理 |
| --- | --- |
| CMake 报「未找到 FFmpeg 开发库」 | 安装 `mingw-w64-ucrt-x86_64-ffmpeg` 并确保 `pkg-config --modversion libavcodec` 可用；或先 `build-ffmpeg.ps1` 再 `-DFFMPEG_ROOT=<install>` |
| `VideoEncoder` 返回 `UNSUPPORTED`：缺少 libx264/libx265 | 系统 FFmpeg 是 LGPL-only 构建。改用含 GPL 组件的 FFmpeg，或用 `build-ffmpeg.sh`（默认含 x264/x265）自建 |
| 运行时报找不到 `avcodec-63.dll` 等 | 把 `C:\msys64\ucrt64\bin` 加入 `PATH`；发布时复制所需 DLL 到 exe 同目录，或改用静态库构建 |
| 配置阶段 Qt 的 `FindFFmpeg.cmake` 抢先命中（只找到 3 个库） | 确认根 CMakeLists 用 `list(PREPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)` |
| `build-ffmpeg.sh` 报 `Native MSYS builds are discouraged` | 脚本已自动处理（设置 `MSYSTEM=UCRT64` 并前置 `/ucrt64/bin`）；直接运行脚本即可 |
| 内嵌源码 configure 报缺 `libavfilter/allfilters.c` | 源码树被过度裁剪；恢复 `libavfilter/`、`libavdevice/` 目录（见 `third_party/ffmpeg/README.md`） |
| 测试临时目录 / 输出目录创建失败 | 媒体层用 `QDir().mkpath` 创建输出目录；若运行环境禁止写系统 TEMP，请把临时目录放到工程构建目录（`test_media.cpp` 即放在测试 exe 同目录） |
