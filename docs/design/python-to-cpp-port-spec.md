# Python 打包 exe → 纯 C++/Qt6 可执行移植规格

> 状态：研究完成稿（只读调研，未改动任何源码）
> 目标：把 `bin\exe\{image,image_batch,video,video_batch}.exe`（PyInstaller 打包，各约 28 MB）替换为
> Qt6 Widgets 工程内联的 C++17 实现，保留对 `realesrgan-ncnn-vulkan.exe` 与 `ffmpeg/ffprobe` 的编排。
> 证据口径：`路径:行号`；标注 **实测** 的结论来自本机实际运行 `bin\realesrgan\realesrgan-ncnn-vulkan.exe`、
> `bin\ffmpeg\bin\ffmpeg.exe`、`bin\ffmpeg\bin\ffprobe.exe` 与工程 `input\` 素材（2026-11-14）。

---

## 0. 结论速览（TL;DR）

| 结论 | 依据 |
| --- | --- |
| 4 个 Python 程序**不含任何算法**，全部是「参数解析 + 路径拼装 + 调 realesrgan/ffmpeg + 扫描目录算进度」的胶水层 | `bin/src/core.py:379-424`、`bin/src/video.py:127-405` |
| 进度协议只有一行：`PROGRESS:<float>`（stdout，flush） | `bin/src/core.py:598-605`；C++ 侧 `src/infrastructure/exeoutputparser.cpp:12-24` |
| 真正的进度有两种来源：① realesrgan 的 `xx.xx%`（**走 stderr**，实测）② 扫描输出目录文件数（图片批量） | 实测；`bin/src/core.py:95`（image.py）、`bin/src/image_batch.py:160-212` |
| **realesrgan 失败时退出码仍是 0**（`decode/encode image ... failed`） | 实测，见 §3.5。这是「不稳定感」的第一来源，移植时必须改为校验产物文件 |
| Python 批量图片会把每批文件**再复制一份**到临时目录，磁盘峰值 ≈ 2× 帧体积 | `bin/src/image_batch.py:51-73, 336` |
| C++ 侧 `OutputPathResolver` 与 Python 命名规则**主体一致，但有 4 处差异**（§4.3） | `src/model/outputpathresolver.cpp:8-68` vs `bin/src/core.py:110-216` |
| C++ 侧 `ExeOutputParser` 只解析 **stdout**，而未来直连 realesrgan 时进度在 **stderr** | `src/infrastructure/exerunner.cpp:532-551`；实测 |
| 移植后应删除 `bin\exe`、`bin\src`（含 tests/setup.py）；保留 `bin\realesrgan`（含 models）、`bin\ffmpeg\bin` | §7 |

---

## 1. 现状总览

### 1.1 进程拓扑（现状）

```
Qt6 App (real-esrguisoft.exe)
 └─ QProcess: bin/exe/image.exe | image_batch.exe | video.exe | video_batch.exe   [Python, 28MB, PyInstaller]
      ├─ image.py          → realesrgan-ncnn-vulkan.exe（单图）
      ├─ image_batch.py    → 多进程(multiprocessing)：CopyWorker / ProcessWorker / FileWatcher
      │                      └─ realesrgan-ncnn-vulkan.exe（目录模式，逐批）
      ├─ video.py          → ffmpeg（拆帧） → image_batch.exe（帧超分） → ffmpeg（合成）
      └─ video_batch.py    → 逐个调用 video.exe（嵌套三层）
```

调用入口：`src/infrastructure/execommand.cpp:23-43`（定位 exe）、`src/infrastructure/exerunner.cpp:98-143`（启动）、
`src/model/processservice.cpp:40-102`（决策单文件/批量）。

### 1.2 引擎与素材（实测）

| 项 | 值 |
| --- | --- |
| realesrgan | `bin\realesrgan\realesrgan-ncnn-vulkan.exe` 6,161,408 B；模型目录 `bin\realesrgan\models`（10 个文件：x4plus 33.4MB、x4plus-anime 8.9MB、animevideov3 x2/x3/x4 各 1.2MB） |
| ffmpeg | `bin\ffmpeg\bin\ffmpeg.exe`，`N-113547-ga42f884cd2-20240206`，含 `--enable-libx264 --enable-libx265 --enable-libvpx --enable-libsvtav1 --enable-libopus` |
| ffprobe | `bin\ffmpeg\bin\ffprobe.exe` |
| 样例视频 | `input\video\1.mp4`：640×480、h264 High yuv420p、`avg_frame_rate=1199/50`（23.98fps）、`nb_frames=181`、7.549s、AAC 48kHz 立体声 |
| 样例图片 | `input\1.png`(164KB) `2.jpg` `3.jpg`(9.6KB) `4.png` `5.jpg` `6.jpg` `7.png` `8.png` `9.png` |

> 实测数字（供验收基准）：
> - `1.png` 单图 `-n realesrgan-x4plus -s 4` → PNG **2,768,423 B**（≈16.8×）
> - `3.jpg` 单图 `-n realesrgan-x4plus-anime -s 4` → PNG **417,102 B**
> - `3.jpg`/`5.jpg` 目录模式 `-s 2 -f png` → `91374 / 200318 B`
> - `input\video\1.mp4` 拆帧 → **181 帧**，共 13,476,136 B（平均 78,531 B/帧，`-qscale:v 1` MJPEG）

---

## 2. 行为清单（逐脚本，带行号）

### 2.1 `bin/src/core.py` 公共模块

| # | 能力 | 行为 | 行号 |
| --- | --- | --- | --- |
| C1 | 常量 | `IMAGE_EXTENSIONS={.jpg,.jpeg,.png,.webp,.bmp}`；`VIDEO_EXTENSIONS={.mp4,.mkv,.avi,.mov,.webm,.wmv,.flv,.ts,.gif}`；`IMAGE_OUTPUT_FORMATS={jpg,jpeg,png,webp}` | 23、26、29 |
| C2 | 模型映射 | `anime→realesrgan-x4plus-anime`、`real→realesrgan-x4plus`、`anime_video→realesr-animevideov3`；未知键回退 anime | 32-36、80-90 |
| C3 | 默认倍数 | `anime=4, real=4, anime_video=2`（未知回退 4） | 39-43、93-103 |
| C4 | 默认值 | `DEFAULT_BATCH_SIZE=200`、`DEFAULT_CRF=18`、`DEFAULT_PRESET='medium'` | 46、49、52 |
| C5 | 路径标准化 | 去成对双引号/单引号 → `os.path.normpath` | 59-73 |
| C6 | **单文件输出命名** | `{stem}{_原扩展名}_{model}{_x?}{.输出扩展名}`；无 `-o` → 输入同目录；`-o` 以分隔符结尾→目录；有扩展名→当成完整文件；已存在目录→目录；无扩展名→目录 | 110-174（关键 132-149） |
| C7 | 输出扩展名分支 | `-f` 非 auto → 用指定扩展名；否则 `.gif/.bmp` 保留原扩展名（**隐患：realesrgan 无法编码 gif/bmp**）；其余沿用输入扩展名，无扩展名→`.png` | 137-146 |
| C8 | **批量输出目录** | `{输入文件夹名}_{model}{_x?}`；未指定 `-o` → 输入文件夹的**父目录**下；指定 `-o` 一律当作父目录 | 177-216 |
| C9 | 图片扫描 | 仅一层（非递归）、`os.path.isfile`、扩展名小写匹配、`sorted()` 字典序 | 223-240 |
| C10 | 视频扫描 | 同上，但**排除已处理文件**：若 `stem.endswith("_" + ext.replace('.','_') + "_" + model + ("_x"))` 则跳过 | 243-271（265-270） |
| C11 | 引擎定位 | `GetBinDir()` 多候选（脚本父目录/`sys.frozen` 的 exe 目录/cwd/bin/cwd/../bin），以 `realesrgan` 或 `ffmpeg` 目录存在为准 | 278-343 |
| C12 | 命令构造 | `-i -o -n`，`-s`（仅当 ≠4）、`-f`（非 auto）、`-g`、`-x`、`-t`（仅当 ≠0） | 379-424（405-422） |
| C13 | 进度输出 | `print(f'PROGRESS:{p:.1f}', flush=True)` → 形如 `PROGRESS:50.0` | 598-605 |
| C14 | 信息输出 | 头部/分隔线、`输入文件信息: {size}, {W}x{H}`、`处理方式: …`、`选择模型: …`、`CMD指令: "quoted" …`、`输入路径:`、`输出路径:`、`输出文件信息:` | 432-436、439-456、549-564、567-595、608-624、627-636 |
| C15 | 分辨率探测 | 图片用 Pillow（缺失则 `(0,0)`）；视频用 `ffprobe -v quiet -print_format json -show_streams`，取第一个 `codec_type=video` 的 width/height | 459-475、478-517 |
| C16 | 文件信息 | `(size, width, height)`，按扩展名分派 | 520-546 |

### 2.2 `bin/src/image.py`（单图）

| # | 能力 | 行为 | 行号 |
| --- | --- | --- | --- |
| I1 | 编码包装 | 若 `sys.stdout/stderr.encoding != utf-8` 则包成 UTF-8 + `errors=replace` | 21-24 |
| I2 | 参数解析 | 位置参数 `input`；`-o/--output`；`-m/--model {anime,real,anime_video}` 默认 `anime`；`-f/--format` 默认 `auto`；`-s/--scale {2,3,4}` 默认 None；`-g/--gpu`；`-x/--tta`；`-t/--tile` 默认 0 | 126-159（139-157） |
| I3 | 前置校验 | realesrgan 不存在 → 异常；输入非文件 → 异常 | 62-68 |
| I4 | 倍数默认 | `scale=None` → `GetModelDefaultScale(model)` | 70-71 |
| I5 | 输出路径 | `DetermineOutputPath(...)` | 73 |
| I6 | 执行 | `Popen(cmd, stdout=PIPE, stderr=STDOUT, text, utf-8, errors=replace, bufsize=1)`——**stdout/stderr 合并** | 84-92 |
| I7 | 进度解析 | 正则 `^(\d+\.?\d*)%$`（整行匹配）；命中 → `PrintProgress(float)`；`[` 开头行丢弃；其他行原样转发 | 95、99-108 |
| I8 | 失败判定 | **只看 returncode != 0** → 抛 `处理失败，返回码: N` | 112-113 |
| I9 | 收尾 | `PrintOutputInfo(final_output)`（文件不存在则跳过大小/分辨率行） | 115 |
| I10 | 退出码 | 成功 `0`；任何异常 `1`（先打印 `错误: {e}` 到 stdout）；argparse 用法错误 `2`（argparse 默认） | 172、173-175、161-175 |

### 2.3 `bin/src/image_batch.py`（批量图）

| # | 能力 | 行为 | 行号 |
| --- | --- | --- | --- |
| B1 | 参数 | `input`；`-o`；`-m` 默认 `anime`；`-f` 默认 `auto`；`-s {2,3,4}`；`-g`；`-x`；`-t`；`--batch-size` 默认 200 | 400-437（434-435） |
| B2 | 前置校验 | 引擎存在；输入必须是目录；`GetModelDefaultScale`；`GetImageFiles` 为空 → `输入文件夹中没有找到图片文件` | 290-307 |
| B3 | 分批 | `image_files[i:i+batch_size]` | 326-331 |
| B4 | **复制到临时目录** | `tempfile.mkdtemp(prefix='realesrgan_batch_')`，每批复制到 `batch_{i}`（`shutil.copy2`，保留 mtime） | 51-73、336 |
| B5 | 引擎调用 | **目录模式**：`-i batch_dir -o 最终输出目录`（不传 `-o batch 输出子目录`，由 realesrgan 平铺同名输出） | 102-105、248-253 |
| B6 | 引擎输出处理 | 丢弃 `[` 开头行、丢弃以 `%` 结尾行、其余打印；rc!=0 记为失败批次 | 120-138 |
| B7 | 进度 | `FileWatcher` 独立进程：起始快照输出目录（size+mtime），每 **0.1 s** 轮询，新文件或 size/mtime 变化即计 1，`completed/total*100`，**只在上涨时** `PrintProgress` | 160-212（202-209、212） |
| B8 | 并发编排 | `Queue(maxsize=2)` 做背压；`CopyWorker` 顺序复制并按批投递；`ProcessWorker` 消费并 `shutil.rmtree(batch_dir)` | 145-265（259-263）、344 |
| B9 | 收尾 | `stop_event` 停止 watcher（超时 2s 则 terminate）；删除临时根目录；统计输出文件数；**输入数≠输出数 → 抛异常** | 372-395 |
| B10 | 退出码 | 成功 0；异常 1（`错误: …`）；argparse 2；Windows 打包需 `freeze_support()` | 451-454、459-461 |

### 2.4 `bin/src/video.py`（单视频）

| # | 能力 | 行为 | 行号 |
| --- | --- | --- | --- |
| V1 | 参数 | `input`；`-o`；`-m` 默认 `anime_video`；`-f` 默认 `auto`；`-s {2,3,4}`；`-g`；`-x`；`-t`；`--crf`；`--preset`（9 档，默认 medium）；`--encoding {h264,h265,vp9,av1}` 默认 h264；`--quality {fast,balanced,quality,快速,均衡,质量}` 默认 balanced | 526-573（544-571） |
| V2 | 前置校验 | ffmpeg / ffprobe 必须存在；输入必须是文件；`scale` 默认按模型；`ffprobe` 取 `show_format+show_streams` | 432-460（59-66） |
| V3 | 帧率 | 遍历 streams 找 video 流，读 `avg_frame_rate`（形如 `1199/50`），分母 0 → 0.0；找不到 → 30 | 83-105 |
| V4 | 音频判定 | 存在 `codec_type==audio` 的流 | 108-124 |
| V5 | 参数优先级 | 先按 quality/encoding 查 `GetEncodingParams`；`--crf` 显式 → 覆盖；`--preset != medium` → 覆盖（**注意：显式传 `--preset medium` 无法覆盖 quality 结果**） | 255-285、449-454 |
| V6 | 拆帧 | 见 §3.3；失败打印 stderr 前 500 字符 | 127-177（143-171） |
| V7 | 帧超分 | 调 `image_batch.exe`（frozen）或 `python -u image_batch.py`，固定 `-f jpg`，传 `-s/-g/-x/-t`；环境变量 `PYTHONIOENCODING=utf-8 PYTHONUNBUFFERED=1`；解析 `PROGRESS:(\d+\.?\d*)` 并**原样**转发进度；其他非 `[` 行转发 | 180-252（196-249） |
| V8 | 帧目录推断 | 处理结果位于 `{processed}/frames_{model}{_x?}`（因为 image_batch 的批量输出目录规则） | 496-497 |
| V9 | 合成 | 见 §3.4 分支矩阵 | 288-405 |
| V10 | 临时目录 | `mkdtemp(prefix='realesrgan_video_')` → `frames/`、`processed/`；`finally` 中 `rmtree`，失败仅打印 | 478-482、509-515 |
| V11 | 进度语义 | 视频总进度 = 帧超分阶段进度（不按阶段加权，拆帧/合成期间进度停在 0/100 附近） | 244-245、488-502 |
| V12 | 输出信息 | 打印检测帧率、音频是否存在、`[步骤 1/3..3/3]`、`总耗时: %.2f 秒` | 474-476、488-502、517-521 |
| V13 | 退出码 | 成功 0；任一阶段失败 → `raise` → `错误: …` + 1；argparse 2 | 489-502、590-593 |

### 2.5 `bin/src/video_batch.py`（批量视频）

| # | 能力 | 行为 | 行号 |
| --- | --- | --- | --- |
| VB1 | 参数 | 与 video 相同（无 `--batch-size`） | 256-301 |
| VB2 | 校验 | `video.exe`/`video.py` 存在；输入必须是目录；`GetVideoFiles`（**排除已处理**）；为空 → 异常 | 157-184 |
| VB3 | 输出目录 | `DetermineOutputFolder(input, -o, model, tta)`，`makedirs` | 178、213 |
| VB4 | 逐视频串行 | 对每个视频 **单独起一个 `video.exe` 进程**，参数：`input -o 输出目录 -m model -s scale --preset preset [--crf N] [-f fmt] [-g] [-x] [-t] [--encoding] [--quality]` | 45-97、219-237 |
| VB5 | 进度映射 | 单个视频进度 `p` → 总进度 `index/total*100 + p/total`（**每视频均分权重，且只增不减无法保证**） | 112-124（121） |
| VB6 | 失败聚合 | 收集失败文件名；最后 `success_count != total` → 抛 `处理不完整: 成功 x/y` | 216-251 |
| VB7 | 退出码 | 成功 0；异常 1；argparse 2 | 320-323 |

### 2.6 `bin/src/setup.py`（打包，仅用于删除评估）

| # | 能力 | 行号 |
| --- | --- | --- |
| S1 | 打包 4 个脚本 → `bin/exe/*.exe`，`PyInstaller --onefile --distpath bin/exe --clean` | 32、35-40、126-133 |
| S2 | 智能跳过已存在 exe（`GetMissingScripts`）、`--force`、`-j` 并行 | 62-78、227-230 |
| S3 | 清理 `build/ dist/ *.spec` | 144-188 |

---

## 3. 外部进程契约（可直接复制）

> 通用约定：所有子进程**不设置 cwd 也能正常工作**（实测：realesrgan 的 `models/` 相对 **exe 所在目录**解析，缺模型会打印
> `_wfopen <exedir>\models\<name>.param failed` 并 **0xC0000409 崩溃**，退出码 `-1073740791`）。

### 3.1 realesrgan-ncnn-vulkan（超分引擎）

**实测 `--help` 全量旗标**（`bin\realesrgan\realesrgan-ncnn-vulkan.exe`，无参数运行）：

```
-h                   show this help
-i input-path        input image path (jpg/png/webp) or directory
-o output-path       output image path (jpg/png/webp) or directory
-s scale             upscale ratio (can be 2, 3, 4. default=4)
-t tile-size         tile size (>=32/0=auto, default=0) can be 0,0,0 for multi-gpu
-m model-path        folder path to the pre-trained models. default=models
-n model-name        model name (default=realesr-animevideov3; can be realesr-animevideov3 | realesrgan-x4plus | realesrgan-x4plus-anime | realesrnet-x4plus)
-g gpu-id            gpu device to use (default=auto) can be 0,1,2 for multi-gpu
-j load:proc:save    thread count for load/proc/save (default=1:2:2)
-x                   enable tta mode
-f format            output image format (jpg/png/webp, default=ext/png)
-v                   verbose output
```

**Python 实际拼装的模板**（`bin/src/core.py:379-424`；`[..]` 表示条件出现）：

```bat
:: 单图（image.py）
"<BIN>\realesrgan\realesrgan-ncnn-vulkan.exe" -i "<输入图片>" -o "<输出文件>" -n "<模型文件短名>" [-s 2^|3] [-f jpg^|png^|webp] [-g <0-2>] [-x] [-t <>=32^|0>]

:: 批量图（image_batch.py；目录 → 目录，输出平铺同名）
"<BIN>\realesrgan\realesrgan-ncnn-vulkan.exe" -i "<输入目录>" -o "<输出目录>" -n "<模型文件短名>" [-s ..] [-f ..] [-g ..] [-x] [-t ..]
```

- `<模型文件短名>`：`realesrgan-x4plus` / `realesrgan-x4plus-anime` / `realesr-animevideov3`（`core.py:32-36`）
- `-s` 仅在 `scale != 4` 时传（`core.py:405-406`）；C++ 现状**总是**传（`src/infrastructure/execommand.cpp:67-78`），等价无害
- `-f` 仅在格式 ≠ auto 时传（`core.py:409-410`）
- 未使用的可用旗标：`-j load:proc:save`、`-m <模型目录>`、`-v`（移植时可用于：`-m` 显式锁定模型目录避免依赖 exe 目录；`-j` 调线程数）

**实测输出样例（全部在 stderr，stdout 为空）**：

```
[0 NVIDIA GeForce RTX 5070 Ti Laptop GPU]  queueC=2[8]  queueG=0[16]  queueT=1[2]
[0 NVIDIA GeForce RTX 5070 Ti Laptop GPU]  bugsbn1=0  bugbilz=0  bugcopc=0  bugihfa=0
[0 NVIDIA GeForce RTX 5070 Ti Laptop GPU]  fp16-p/s/a=1/1/1  int8-p/s/a=1/1/1
[0 NVIDIA GeForce RTX 5070 Ti Laptop GPU]  subgroup=32  basic=1  vote=1  ballot=1  shuffle=1
[1 AMD Radeon(TM) 610M]  queueC=1[4]  queueG=0[8]  queueT=2[1]
...
0.00%
16.67%
33.33%
50.00%
66.67%
83.33%
```

- 进度行格式：`<浮点>%`（两位小数），**每处理完一块/一个 tile 批次输出一行**；单图不保证出现 100%（实测末值为 83.33%）
- 目录模式：进度**每个文件从头计数**（实测 2 张：`0.00% 0.00% 25.00% 50.00% 75.00%`），且**不打印文件名**
- 失败样例：`decode image C:\nonexistent\x.jpg failed` / `encode image <path> failed`
- `[` 开头行是 GPU 枚举信息（`bin/src/image.py:101` 主动丢弃）

### 3.2 ffprobe（探测）

```bat
:: video.py 用的完整版（show_format + show_streams）
"<BIN>\ffmpeg\bin\ffprobe.exe" -v quiet -print_format json -show_format -show_streams "<视频>"

:: core.py 取分辨率用的精简版
"<BIN>\ffmpeg\bin\ffprobe.exe" -v quiet -print_format json -show_streams "<视频>"
```

解析点：`streams[]` 中第一个 `codec_type=="video"` 的 `width/height`（`core.py:510-514`）、
`avg_frame_rate`（`video.py:96-104`）、audio 流存在性（`video.py:121-124`）、`nb_frames/duration`（移植后建议新增，用于精确进度）。
实测 `1.mp4` 返回 `width=640 height=480 avg_frame_rate="1199/50" nb_frames="181" duration="7.549208"`。

### 3.3 ffmpeg 拆帧（`bin/src/video.py:143-153`）

```bat
"<BIN>\ffmpeg\bin\ffmpeg.exe" -y -nostdin -i "<输入视频>" -qscale:v 1 -qmin 1 -qmax 1 -vsync 0 "<临时帧目录>\frame%08d.jpg"
```

- 帧名固定 `frame%08d.jpg`，从 `frame00000001.jpg` 起（`video.py:141`）
- `-qscale:v 1 -qmin 1 -qmax 1` = MJPEG 最高质量（实测 640×480 约 78 KB/帧）
- `-vsync 0`（旧式写法，ffmpeg 6 建议 `-fps_mode passthrough`；本机 ffmpeg 仍接受）
- **注意**：`video.py:462-470` 里用于「打印 CMD 指令」的命令把帧路径写成相对路径 `frames/frame%08d.jpg`，与真正执行的绝对路径不一致（仅显示差异，不影响功能）

### 3.4 ffmpeg 合成（`bin/src/video.py:288-405` 分支矩阵）

基础命令（`frame_rate` 来自 ffprobe 的 `avg_frame_rate`）：

```bat
"<BIN>\ffmpeg\bin\ffmpeg.exe" -y -nostdin -framerate <frame_rate> -i "<帧目录>\frame%08d.jpg" [ -i "<原始视频>" ]
```

`[ -i 原视频 ]` 条件：`has_audio && 输出扩展名 != .gif`（`video.py:319-320`）。

然后按**输出文件扩展名**分派（`video.py:322-379`）：

| 输出扩展名 | 追加参数 |
| --- | --- |
| `.gif` | `-vf fps=<frame_rate> -loop 0`（**忽略 `--encoding`**） |
| `.webm` | `-c:v libvpx-vp9 -crf <crf> -b:v 0 -pix_fmt yuv420p -r <frame_rate>`；有音频追加 `-map 0:v:0 -map 1:a:0 -c:a libopus`（**忽略 `--encoding`**，恒 VP9） |
| 其他，`encoding=h265/hevc` | `-c:v libx265 -crf <crf> -preset <preset> -pix_fmt yuv420p -r <frame_rate>` |
| 其他，`encoding=vp9` | `-c:v libvpx-vp9 -crf <crf> -b:v 0 -pix_fmt yuv420p -r <frame_rate>` |
| 其他，`encoding=av1` | `-c:v libsvtav1 -crf <crf> -b:v 0 -pix_fmt yuv420p -r <frame_rate>` |
| 其他（默认 h264） | `-c:v libx264 -crf <crf> -preset <preset> -pix_fmt yuv420p -r <frame_rate>` |

非 gif 且有音频，统一追加：`-map 0:v:0 -map 1:a:0 -c:a copy`（`video.py:376-377`），最后追加 `<输出文件>`。

**质量矩阵**（`video.py:255-285`，`--crf`/`--preset` 覆盖规则见 §2.4 V5）：

| `--quality` | h264 / h265 | vp9 / av1 |
| --- | --- | --- |
| `fast` / `快速` | CRF **28**，preset `ultrafast` | CRF **35**，preset `fast` |
| `balanced` / `均衡`（默认） | CRF **23**，preset `medium` | CRF **30**，preset `medium` |
| `quality` / `质量` | CRF **18**，preset `slow` | CRF **25**，preset `slow` |

> 注意：`-preset` 只出现在 libx264/libx265 分支；vp9/av1 分支**丢弃** preset（`video.py:350-365`）。

**可直接复制的完整示例**（带音频 MP4 → H.264 均衡）：

```bat
"<BIN>\ffmpeg\bin\ffmpeg.exe" -y -nostdin -framerate 23.98 -i "%TEMP%\realesrgan_video_xxx\processed\frames_anime_video\frame%08d.jpg" -i "<输入视频>" -c:v libx264 -crf 23 -preset medium -pix_fmt yuv420p -r 23.98 -map 0:v:0 -map 1:a:0 -c:a copy "<输出视频>"
```

### 3.5 退出码与失败探测矩阵（**移植重点**）

| 进程 | 场景 | 实测退出码 | 可探测信号 |
| --- | --- | --- | --- |
| realesrgan | 成功 | `0` | 输出文件存在且 size>0 |
| realesrgan | 输入不存在 / 解码失败 | **`0`** | stderr `decode image ... failed`；无输出文件 |
| realesrgan | 输出不可写 / 扩展名无法编码 | **`0`** | stderr `encode image ... failed`；无输出文件 |
| realesrgan | 模型文件缺失 | `-1073740791`（0xC0000409 崩溃） | stderr `_wfopen ...<name>.param failed` |
| realesrgan | `-s` 非法值（如 5） | `0` | 仅打印 `0.00%`，无输出文件 |
| ffmpeg | 成功 | `0` | 输出文件存在 |
| ffmpeg | 参数/解码/编码错误 | 非 0（`1`） | stderr 尾部错误 |
| ffprobe | 成功 | `0` | stdout JSON |
| ffprobe | 文件不可读 | 非 0 | stdout 空 |
| Python exe | 成功 | `0` | — |
| Python exe | 业务异常 | `1`（stdout 打印 `错误: …`） | — |
| Python exe | 参数错误 | `2`（argparse） | stderr usage |

> **当前缺陷链（必须修复）**：`image.py:112-113` 只看 returncode → realesrgan 失败仍返回 0 → C++ `ExeRunner`
> 判成功（`exerunner.cpp:509-517`）→ `ProcessService` 发 `output_ready`（`processservice.cpp:209`）→ 界面显示一个不存在的输出文件。
> 移植后判定成功的充要条件：**退出码 0 且产物存在且 size>0**，并对 stderr 中的 `decode/encode ... failed` 做显式失败识别。

---

## 4. 输出命名与目录规则

### 4.1 公式

单文件（`bin/src/core.py:132-149`）：

```
输出文件名 = <输入 stem> + "_" + <输入扩展名去掉点> + "_" + <model> + ("_x" if TTA) + "." + <输出扩展名>
输出目录   = 用户指定目录；未指定 → 输入文件所在目录
```

批量（`bin/src/core.py:197-216`）：

```
输出目录名 = <输入文件夹名> + "_" + <model> + ("_x" if TTA)
输出目录   = 用户 --output 目录 / 输出目录名；未指定 → <输入文件夹父目录>/<输出目录名>
```

### 4.2 实例表（与 `bin/doc/exe使用指南.md:130-158` 对照）

| 输入 | 参数 | 实际输出（代码事实） | 文档声称 |
| --- | --- | --- | --- |
| `input\1.png` | `-m real` | `input\1_png_real.png` | — |
| `input\1.png` | `-m anime -x` | `input\1_png_anime_x.png` | — |
| `input\2.jpg` | `-m real` | `input\2_jpg_real.jpg` | 文档写 `photo_jpg_real.png`（**文档错误**，`exe使用指南.md:142`） |
| `input\video\1.mp4` | `-m anime_video` | `input\video\1_mp4_anime_video.mp4` | 同 |
| `input\a.gif` | `-m anime_video -f mp4` | `input\a_gif_anime_video.mp4` | 同 |
| `input\a.bmp` | `-m anime` | `input\a_bmp_anime.bmp`（**无 `-f` 时 realesrgan 无法写 bmp，实测 encode failed 且 rc=0**） | 文档称「自动转 PNG」（`exe使用指南.md:171`，**与代码不符**） |
| `input\`（文件夹） | `-m anime` | `real-esrguisoft\input_anime\`（父目录下） | 同 |
| `input\video\` | `-m anime_video -x` | `input\video_anime_video_x\` | 同 |
| 批量 `-o D:\out` | `-m anime` | `D:\out\input_anime\` | 同 |

> 文档其他事实错误：`exe使用指南.md:126` 称 `anime_video` 默认 4x，代码是 **2x**（`core.py:39-43`）。

### 4.3 C++ `OutputPathResolver` 对照（`src/model/outputpathresolver.cpp`）

已实现（与 Python 等价）：

- 单文件命名 `%1_%2` + `_model` + `_x` + 扩展名（`outputpathresolver.cpp:19-41`）↔ `core.py:132-149` ✔
- 未指定输出 → 输入目录（`:37-39`）↔ `core.py:152-153` ✔
- 扩展名回退顺序：设置格式 → 输入扩展名 → `png`（`:27-35`）↔ `core.py:137-146` ✔（仅扩展名带点差异，结果同）
- 批量目录 `%1_%2`（输入目录名 + model[_x]）（`:52-58`）、父目录回退（`:65-67`）✔
- 用户指定输出一律视为**父目录**（`:37-39`、`:60-63`）——与 Python `-o` 作为目录的主路径一致 ✔

差异（移植需补齐/确认）：

| # | 差异 | Python 行为 | C++ 现状 | 建议 |
| --- | --- | --- | --- | --- |
| D1 | `-o` 指向带扩展名的文件 | 当作**完整文件路径**（`core.py:162-164`） | 永远当作目录，`dir/name.ext`（`outputpathresolver.cpp:37-41`） | 保留目录语义（GUI 只给目录），文档化即可；若保留 CLI 兼容需补分支 |
| D2 | `.gif/.bmp` 输入无 `-f` | 输出扩展名保持 `.gif/.bmp` → realesrgan 无法编码（`core.py:143-146`） | 同样保留 `.gif/.bmp`（`:27-31`） | **改为强制 `png`** 并在 UI 提示（修复 Python 隐式 bug） |
| D3 | 已处理文件排除（仅视频批量） | `GetVideoFiles` 按 `_ext_model[_x]` 后缀跳过（`core.py:265-270`） | `ProcessService` 批量直接用 `FileService::files_in_directory`（`processservice.cpp:61-62`、`fileservice.cpp:123-146`），**不排除** | 在 job runner 中补「已处理跳过」 |
| D4 | 输出目录创建 | 批量 `makedirs`（`image_batch.py:334`、`video_batch.py:213`），**单图不建**（依赖 realesrgan） | 由 `FileService::ensure_directory_exists` 负责，但 resolver 不保证 | 移植后**统一在 job 启动前 mkpath**，否则 realesrgan 会 encode failed 且 rc=0 |
| D5 | 路径分隔符 | `os.sep` 归一 | `QStringLiteral("%1/%2.%3")` 正斜杠 | Qt/ffmpeg/realesrgan 均接受 `\` 与 `/`；保持 `/` 即可，但日志与 UI 展示建议 `QDir::toNativeSeparators` |
| D6 | 输入路径引号 | `NormalizePath` 去引号（`core.py:59-73`） | 无（`FileService` 直接使用） | GUI 传入不会带引号；若支持拖放/粘贴需补 |
| D7 | 扩展名大小写 | 输出扩展名小写（`core.py:129`） | `suffix().toLower()` ✔ | 一致 |
| D8 | 批量输入目录带尾分隔符 | `NormalizePath` 后取 `name`（`core.py:192-194`） | `QDir(v_input_dir).dirName()`（`outputpathresolver.cpp:52`） | 等价；但 `dirName()` 对 `.` 返回空，需防御 |

---

## 5. C++ 移植设计

### 5.1 设计原则

1. **保持分层**（`ARCHITECTURE.md:47-58`）：Domain ← Infrastructure ← Model ← Controller ← View。
2. **不动 Controller/View**：以 `ProcessService` 为迁移接缝，内部把 `ExeRunner(exe)` 换成 job runner，对外只发 `ProcessStatus`（`src/domain/processstatus.h:26-48`）。
3. **引擎仍是外部进程**：`realesrgan-ncnn-vulkan.exe`、`ffmpeg.exe`、`ffprobe.exe` 保留（用户已接受其存在）。移植的是**胶水层**，不是 Vulkan/ncnn 本身。
4. **失败判定以产物为准**（§3.5），不以退出码为准。
5. 所有 `QProcess` 一律 `setProcessChannelMode(QProcess::MergedChannels)`（realesrgan 全量输出在 stderr，实测）。

### 5.2 建议类划分

| 层 | 类 | 职责 | 对应 Python |
| --- | --- | --- | --- |
| **Domain** | `ModelCatalog` | 短名 ↔ 模型文件名、默认倍数（纯表） | `core.py:32-43, 80-103` |
| Domain | `EncodingProfile`（`from_quality(format, quality)`） | 质量级别 → (crf, preset) 矩阵；`preset` 覆盖规则 | `video.py:255-285, 449-454` |
| Domain | `FrameRate`（`QPair<int,int>` 有理数 + `toString()`） | 帧率有理数保留/格式化，替代 `float` 打印 | `video.py:83-105` |
| Domain | `OutputNaming`（纯函数） | 单文件/批量输出路径公式（把现 `model/OutputPathResolver` 的算法下沉为无依赖纯函数） | `core.py:110-216` |
| Domain | `MediaInputScan` | 扩展名过滤 + 已处理文件排除 + 稳定排序 | `core.py:223-271` |
| **Infrastructure** | `ExternalTool`（基类） | 封装 `QProcess` 启动、MergedChannels、行缓冲、超时、进程树终止 | `exerunner.cpp:394-439` 复用 |
| Infrastructure | `RealEsrganTool` | 构造 realesrgan 命令行；解析 stderr 的 `xx.xx%`；识别 `decode/encode ... failed`；校验产物 | `core.py:379-424`、`image.py:84-113` |
| Infrastructure | `FfprobeTool` | 探测 JSON → `VideoProbe{width,height,fps,hasAudio,nbFrames,duration}` | `video.py:48-124`、`core.py:478-517` |
| Infrastructure | `FfmpegSplitTool` | 拆帧（支持 `-progress pipe:1` 取拆帧进度） | `video.py:127-177` |
| Infrastructure | `FfmpegEncodeTool` | 合成（分支矩阵；支持从管道读帧序列） | `video.py:288-405` |
| Infrastructure | `TempWorkspace`（`QTemporaryDir` + 崩溃恢复清单） | 创建/清理临时目录，启动时清理上次残留 | `video.py:478-482, 509-515`、`image_batch.py:336, 377-382` |
| Infrastructure | `EngineOutputParser`（升级现 `ExeOutputParser`） | 从合并流中抽出 `PROGRESS:`、`xx.xx%`、`decode/encode failed`、GPU 行 | `exeoutputparser.cpp:7-40`、`image.py:95-108` |
| **Model** | `MediaJob`（值对象） | 一次任务的完整描述：input/output/kind/mode/settings/crf/preset/codec | `execommand.cpp:45-150` 的输入侧 |
| Model | `ImageJobRunner` | 单图/批量图：遍历 → 调引擎 → 产物校验 → 逐文件+总体进度 | `image.py`、`image_batch.py` |
| Model | `VideoJobRunner` | 拆帧 → 帧超分 → 合成 三阶段状态机 + 加权进度 + 清理 | `video.py` |
| Model | `VideoBatchJobRunner` | 遍历视频 → 复用 `VideoJobRunner` → 总体进度与失败聚合 | `video_batch.py` |
| Model | `ProcessService`（保留，改造内部） | 对外仍发 `ProcessStatus`；内部按 kind/mode 选 job runner；暂停/恢复/停止转发 | `processservice.cpp` |
| Model | `BatchProgressWatcher`（保留） | 扫描输出目录计完成数（作为兜底进度源） | `image_batch.py:160-212` |
| Model | `OutputPathResolver`（保留，改为委托 `OutputNaming`） | 兼容现有调用点 | `outputpathresolver.cpp` |
| Common | `Constants`（改造） | 删除 4 个 `*_EXE`；新增 `REALESRGAN_EXE`、`FFMPEG_EXE`、`FFPROBE_EXE`、`MODELS_DIR` | `constants.h:9-17` |

保留不动：`View`、`Controller`、`ProcessStatus`、`Logger`、`FileService`（仅补充「已处理排除」工具函数）。

### 5.3 与现有 `ExeRunner` / `ProcessService` 的关系

```
现状：ProcessService ──> ExeRunner(QProcess: bin/exe/image.exe …) ──> ExeOutputParser(PROGRESS:)
目标：ProcessService ──> ImageJobRunner / VideoJobRunner / VideoBatchJobRunner
                            ├─ RealEsrganTool  ─┐
                            ├─ FfmpegSplitTool  ├─> ExternalTool（继承现 ExeRunner 的进程树管理）
                            ├─ FfmpegEncodeTool ┘
                            └─ TempWorkspace / OutputNaming

删除：ExeCommand（bin/exe 参数拼装）、ExeRunner::resolve_exe_path、ExeCommand::classify
保留：ExeRunner 里的 Windows 进程树 suspend/resume/terminate 代码（`exerunner.cpp:145-315, 394-439`）
      下沉为 `ExternalTool` 的静态/受保护实现。
```

迁移步骤（每步可编译、可回归）：

1. 新增 `ExternalTool`（搬运 `ExeRunner` 的进程树逻辑）与 `VideoProbe`；`ExeRunner` 改为继承/组合，行为不变。
2. 新增 `OutputNaming` 并从 `OutputPathResolver` 委托；跑现有单图/批量用例，输出名零差异。
3. 先移植 **单图**（`ImageJobRunner`）：C++ 直接调 realesrgan；CLI 参数在内部生成不再经 `ExeCommand`。
4. 再移植 **批量图**（复用 `BatchProgressWatcher`，或直接用文件计数）。
5. 移植 **单视频**（三阶段 + `TempWorkspace`），验证帧率/音频/编码分支。
6. 移植 **批量视频**（复用第 5 步）。
7. 删除 `bin\exe`、`bin\src`，清理 `Constants`/`ExeCommand`/`.pro`/`.gitignore`。

### 5.4 视频帧流：落盘 vs 管道

约束：**realesrgan-ncnn-vulkan 只接受文件或目录，不支持 stdin/stdout 视频流**（`--help`：`-i 输入图片/目录`）。
因此「帧完全不落盘」不可行，只能在**落盘量**上优化。三种方案：

| 方案 | 做法 | 磁盘峰值 | 复杂度 | 建议 |
| --- | --- | --- | --- | --- |
| A. 照搬 Python | 全量拆帧到 `frames/` → 逐批复制到 `batch_i/` → 超分输出到 `processed/frames_model/` | **≈ 1×源帧 + 1×复制 + 1×输出 ≈ 3×** | 低 | 不推荐（浪费、慢） |
| B. 分块流水（推荐） | 全量拆帧到 `frames/`（或分块拆） → 每块 K 帧用 **硬链接** 建 `chunk_k/`（NTFS `CreateHardLink`，零拷贝） → realesrgan 目录模式输出 `chunk_k_out/` → 把该块产物**顺序写入 ffmpeg 编码器 stdin**（`-f image2pipe -c:v png -i -`）→ 删除块目录 | **≈ 1×源帧 + K×输出帧** | 中 | ★ 首选 |
| C. 全管道（不可行） | ffmpeg rawvideo → C++ → realesrgan | — | — | realesrgan 不接受流，放弃 |

方案 B 关键点：

- 拆帧阶段可继续用 `-qscale:v 1` 的 MJPEG（与现状一致，兼容性最好）；
- 分块 `K`（建议 16~64 帧，取决于 `-t` tile 与显存）→ 天然形成「暂停/取消」的响应点；
- 编码器长驻：`ffmpeg -y -nostdin -f image2pipe -c:v png -i - -c:v libx264 ... <out>`，
  C++ 侧边收 `chunk_k_out/*.png` 边 `write()` 到其 stdin，**必须**保证写入顺序为帧序号顺序，且编码器不能提前 EOF；
- 若不接受 PNG 管道（体积大 3~5×），可退回方案 B'：保留 `processed/frames_model/` 全部产物，编码完成后删除 `frames/`
  （峰值 ≈ 1×源 + 1×输出，仍优于 A）；实测 181 帧源 13.5MB + x2 PNG 27MB，可接受。
- 音频：编码器需要**第二路输入是文件**（原视频），因此 `-i <原视频>` 与 `-i -` 的组合需写成
  `ffmpeg -i - -i "<原视频>" -map 0:v:0 -map 1:a:0 ...`（管道输入必须是第一路）。

### 5.5 内存 / 磁盘峰值估算

实测单帧体积：640×480 `-qscale:v 1` MJPEG ≈ **78.5 KB**（0.26 B/px）；x2 PNG ≈ 91–200 KB；x4 PNG ≈ 0.4–2.8 MB（内容相关）。

公式（令 N=帧数，S=源帧字节，D=输出帧字节）：

```
方案 B：disk_peak ≈ N*S + K*D            （K=分块帧数）
方案 B'：disk_peak ≈ N*S + N*D
方案 A（现状）：disk_peak ≈ N*S(源) + min(batch,200)*S(复制) + N*D
```

| 场景 | N | S | D | A（现状） | B（K=32） | B' |
| --- | --- | --- | --- | --- | --- | --- |
| `input\video\1.mp4` 640×480 7.5s | 181 | 78 KB | 200 KB | ≈ 13.5+15.7+36 MB ≈ **65 MB** | ≈ 13.5+6.4 MB ≈ **20 MB** | **50 MB** |
| 1080p 30fps 10s | 300 | 500 KB | 2 MB(x4 jpg) | ≈ 150+100+600 MB ≈ **850 MB** | ≈ 150+64 MB ≈ **214 MB** | **750 MB** |
| 1080p 30fps 60s | 1800 | 500 KB | 2 MB | ≈ 900+100+3600 MB ≈ **4.5 GB** | ≈ 900+64 MB ≈ **1 GB** | **4.4 GB** |
| 4K 30fps 60s | 1800 | 1.5 MB | 6 MB | ≈ **13 GB**（风险） | ≈ 2.7+0.2 GB ≈ **2.9 GB** | ≈ **13 GB** |

内存：C++ 编排层常驻 < 50 MB；每个 realesrgan 实例（含 Vulkan）建议按 **0.5–1.5 GB 显存 + 200–400 MB 内存** 预留
（`-t` 越小显存越低）；ffmpeg 编解码 1080p ≈ 150–300 MB、4K ≈ 600 MB–1 GB。**不建议并发多个 realesrgan**（显存竞争是"不稳定"的常见原因）；若要并发，上限 = `min(GPU 数, 2)`，且必须各自 `-g` 指定不同 GPU。
**必须**：临时目录默认放 `%TEMP%`，启动时检查可用空间 `< 预计峰值 × 1.2` 则提前失败并提示。

### 5.6 暂停 / 恢复 / 停止语义（纯 C++ 版）

| 操作 | 实现 |
| --- | --- |
| 暂停 | ① 置 `m_paused=true`，阻止调度下一块/下一个文件；② 对当前运行的引擎进程树调用 `NtSuspendProcess`（复用 `exerunner.cpp:178-252`）；③ 发 `ProcessState::PAUSED` 快照 |
| 恢复 | `NtResumeProcess`（`exerunner.cpp:254-296`）+ `m_paused=false`，继续调度 |
| 停止（取消） | 置 `m_cancelled=true` → `TerminateProcess` 递归子进程（`exerunner.cpp:414-427`）→ 关闭编码器 stdin（让 ffmpeg 正常收尾或 kill）→ **删除 `TempWorkspace`** → 发 `stopped()`（**不视为失败**，对应 `processservice.cpp:122-131` 与 `exerunner.h:69-71`） |
| 暂停边界 | 分块之间是安全点；块内只能挂起进程，恢复后继续（不会丢帧） |
| 注意 | 暂停时若 UI 关闭，`~ProcessService` 必须先 resume 再 terminate（`exerunner.cpp:400-406` 已有该逻辑），否则 `TerminateProcess` 对挂起进程仍有效但更易残留子进程 |

### 5.7 新进度协议（替换 `PROGRESS:` 的进程外协议）

由于编排进入进程内，**不再需要**跨进程 `PROGRESS:` 文本协议；但若保留 `ExternalTool` 的通用解析（便于单测/调试），建议同时输出：

```
PROGRESS:<0-100 浮点>        // 总进度（兼容现 ExeOutputParser）
STAGE:<split|upscale|encode> // 阶段（新增，供 UI 细化文案）
CURRENT:<文件名>             // 当前文件（替代从未生效的 "Processing:" 正则）
```

进度权重建议（视频）：拆帧 10%、帧超分 75%（按已完成帧数 / 探测到的 `nb_frames`）、合成 15%；
图片批量：`已完成文件数 / 总数`（可由 `BatchProgressWatcher` 或 job 内部计数），单图：引擎 `xx.xx%` 直通。
`ProcessStatus.m_progress_percent` 现为 0–100 整数（`processstatus.h:31`），保持单调整递增（UI 层可做 `max()`）。

### 5.8 并发

- 图片批量：保持**串行**调引擎（单 GPU 显存瓶颈），但去掉 Python 的「复制到临时目录」与多进程通信。
- 若必须并行：`QThreadPool` + 每线程一个 realesrgan 实例，**显式 `-g` 绑定不同 GPU**；上限 2。
- 视频批量：如实测，Python 是**串行**（`video_batch.py:219`）；C++ 同样串行，避免 4K 场景磁盘爆量。

---

## 6. 风险与验证点

### 6.1 Python → C++ 语义差异（逐条，必须处理）

| # | 风险 | 说明 | 处理 |
| --- | --- | --- | --- |
| R1 | **退出码不可信** | realesrgan 失败仍返回 0（§3.5 实测） | 以「产物存在 && size>0 && stderr 无 failed」判定 |
| R2 | **进度在 stderr** | realesrgan 全量输出走 stderr，stdout 空（实测） | `MergedChannels` + 解析合并流；修掉 `exerunner.cpp:532-551` 只读 stdout 的缺口 |
| R3 | 编码/解码失败文本 | `decode image ... failed` / `encode image ... failed`；模型缺失会崩溃 `0xC0000409` | 显式匹配这些子串；崩溃按失败处理并提示「模型缺失/显存不足」 |
| R4 | GIF | 图片链路输入含 `.gif` 即坏（realesrgan 不能写 gif），但 `FileService` 图片扩展名**不含 gif**（`fileservice.cpp:9-11`）而文件对话框**含 gif**（`execommand.cpp:161`） | 图片模式 gif → 强制 PNG 输出并提示；对话框与过滤器保持一致 |
| R5 | BMP | 同上，Python 保留 `.bmp` 输出必失败 | 强制 PNG（`core.py:143-146` 的坑） |
| R6 | 帧率浮点漂移 | Python `str(avg_frame_rate 除法结果)` 可能输出 `29.97002997002997`；`0/0` → `-framerate 0.0` 必失败（`video.py:98-104`） | 用 **有理数** `num/den` 原样传（`-framerate 30000/1001`），ffmpeg 原生支持；0/0 时回退 `30/1` 并告警 |
| R7 | 色彩空间/位深 | 全链路 `-pix_fmt yuv420p` 硬编码（`video.py:335,347,355,363,372`）；拆帧走 MJPEG 8-bit 4:2:0，10-bit/HDR/4:4:4 素材必然降质 | 文档化；可作为后续增强项（`-pix_fmt yuv420p10le` + 无损中间格式） |
| R8 | 文件名编码/非 ASCII | PowerShell/Python 均按 UTF-8；realesrgan 内部用 `_wfopen`（宽字符） | C++ 传 `QString` 给 `QProcess::start(exe, args)` 即可；测试中文/emoji/空格路径 |
| R9 | 长路径 >260 | `%TEMP%` + 深目录 + `frame%08d.jpg` 易超限 | `TempWorkspace` 使用短前缀（如 `%TEMP%\resr_<8位随机>`）；必要时 `\\?\` 前缀 |
| R10 | 批量已处理文件 | C++ 批量不排除已处理（§4.3 D3）→ 重跑会重复处理 | `MediaInputScan` 实现 `core.py:265-270` 规则 |
| R11 | 临时目录残留 | Python 仅 `finally rmtree`，进程被 kill 则残留（`video.py:509-515`） | `TempWorkspace` 启动时扫描并清理 `resr_*`；`QCoreApplication` 退出钩子兜底 |
| R12 | `--preset medium` 覆盖失效 | Python 用 `preset != DEFAULT_PRESET` 判断（`video.py:453-454`），显式 medium 不生效 | C++ 用 `std::optional<preset>` 表达「未指定」 |
| R13 | webm/`-f webm` 忽略 encoding | 输出扩展名 `.webm` 恒 VP9（`video.py:329-339`） | 保持现状（兼容）或按 encoding 选择（需产品确认） |
| R14 | `-c:a copy` 容器不兼容 | 源音频 Vorbis/Opus 塞进 mp4 会失败；AVI+MOV 同理 | 失败重试策略：`-c:a aac -b:a 192k`，并记录 |
| R15 | 编码器缺失 | 依赖 `libx264/libx265/libvpx/svtav1/opus`（`ffmpeg -version` 已确认全部可用） | 启动自检 `ffmpeg -encoders` 一次性校验，缺失则禁用对应选项 |
| R16 | 磁盘耗尽 | 4K 60s ≈ 13 GB（§5.5） | 启动前检查可用空间 |
| R17 | 多进程树挂起副作用 | 暂停时挂起整棵树（含 ffmpeg/realesrgan），部分驱动/Vulkan 在挂起期间可能被 TDR 重置 | 暂停期间若 GPU 出现 device lost，恢复后任务判失败并提供「重试当前文件」 |
| R18 | N 卡/核显选择 | 实测机器有 `[0] RTX 5070 Ti` 与 `[1] AMD 610M`；`-g auto` 默认取 0 | `-g` 维持 0/1/2 枚举；探测失败时回退 auto |
| R19 | 进度单调性 | 批量视频 Python 公式可能回退（每个视频从 0 起） | C++ 侧对 `ProcessStatus` 用单调 `max` 过滤 |
| R20 | 输出覆盖 | 同名输出直接覆盖（realesrgan 无 `-y`，实测覆盖） | 保持；必要时加「自动重命名」开关（`FileService::unique_output_path` 已具备） |

### 6.2 必须保留的兼容行为

1. CLI/内部参数语义：`-m/--model`、`-s`、`-f`、`-g`、`-x`、`-t`、`--crf`、`--preset`、`--encoding`、`--quality`、`--batch-size` 的含义与默认值（§2 表）。
2. 输出命名公式与目录规则（§4.1），**逐字节一致**（用户的既有产物目录结构不能变）。
3. 模型短名 → 文件名映射、默认倍数（`anime/real=4`、`anime_video=2`）。
4. 质量矩阵 CRF/preset 数值（§3.4）与 `--crf/--preset` 覆盖语义。
5. 拆帧帧名 `frame%08d.jpg`、`-qscale:v 1` 质量、`-vsync 0` 等价行为。
6. 合成命令的编码器选择与 `pix_fmt yuv420p`、`-r`、音频 `-c:a copy`。
7. 单文件失败 → 整任务失败并 `ProcessState::FAILED`；用户取消 → `stopped()` 不算失败。
8. 批量完成校验：输入数 ≠ 输出数 → 失败（`image_batch.py:394-395`、`video_batch.py:250-251`）。
9. 旧 `PROGRESS:` 解析能力保留（便于回退与既有单测）。

### 6.3 验收测试用例（用工程 `input\` 真实素材）

> 每个用例记录：退出状态、输出路径/命名、产物尺寸、进度序列、耗时、临时目录是否清空。

| ID | 素材 | 用例 | 期望 |
| --- | --- | --- | --- |
| T01 | `input\1.png` | 单图 `-m real -s 4` | 产出 `1_png_real.png`，≈2.77 MB，720p+ 尺寸 ×4，成功 |
| T02 | `input\3.jpg` | 单图 `-m anime -x -s 4` | 产出 `3_jpg_anime_x.jpg`，存在且非空；TTA 生效（耗时约 2–4×） |
| T03 | `input\3.jpg` | 单图 `-f webp` | 产出 `3_jpg_anime.webp`（真实 webp，`file`/规格校验） |
| T04 | `input\3.jpg` | 单图 `-f png` 到不存在目录 | 自动 `mkpath` 后成功（修复 D4） |
| T05 | `input\`（9 图，含 png/jpg） | 批量 `-m anime -s 2` | 产出 `input_anime\`，9 个文件，命名 `N_ext_anime.ext`；进度 0→100 单调；无 `batch_*` 临时残留 |
| T06 | `input\` | 批量 `--batch-size 2` | 分 5 批仍全部完成（批大小不影响结果） |
| T07 | `input\` | 批量二次运行 | 按「已处理排除」规则不重复处理已处理文件（若采用 D3 修复） |
| T08 | `input\video\1.mp4` | 单视频默认 `-m anime_video -s 2` | 产出 `1_mp4_anime_video.mp4`，640×480→1280×960，含音频（`ffprobe` 验证 audio 流），时长 7.55s±1 帧 |
| T09 | `input\video\1.mp4` | `-s 4 -f mp4 --encoding h265 --quality quality` | x265 编码，CRF 18、preset slow，产物可解码 |
| T10 | `input\video\1.mp4` | `--encoding vp9 -f webm` | 视频流 `libvpx-vp9`，音频重编码 `libopus` |
| T11 | `input\video\1.mp4` | `-f gif` | 产出 GIF，`-loop 0`，无音轨 |
| T12 | `input\video\1.mp4` | `--encoding av1` | `libsvtav1` 编码成功（较慢，允许超时放宽） |
| T13 | `input\video\`（3 个相同 mp4） | 批量视频默认 | 产出 `video_anime_video\`，3 个文件；总进度单调；失败聚合为空 |
| T14 | `input\video\1.mp4` | 处理中暂停 5s 再恢复 | 暂停期间无新帧产出；恢复后完成，产物完整 |
| T15 | `input\video\1.mp4` | 处理中取消 | 状态 `IDLE`+`已取消处理`，**不弹失败框**，临时目录被清理，无残留 `resr_*` |
| T16 | `input\video\1.mp4` | 把 `bin\realesrgan\models` 临时改名 | 明确报错「模型缺失」（识别 `_wfopen ... failed`/崩溃码），不静默成功 |
| T17 | 复制 `input\1.png` → `input\中文 空格 测试.png` | 单图 | 成功，输出名保留中文；日志 UTF-8 正常 |
| T18 | 深路径（>200 字符）临时目录 | 单视频 | 不因长路径失败（或给出明确错误） |
| T19 | `input\1.png` 输出到只读目录 | 单图 | 失败可见（识别 `encode image ... failed`），不报成功 |
| T20 | 任意用例 | 观察进程列表 | 任务结束后无遗留 `realesrgan`/`ffmpeg` 子进程 |

---

## 7. 删除清单 / 保留清单

### 7.1 移植完成后**可删除**

| 路径 | 理由 |
| --- | --- |
| `bin\exe\image.exe`、`image_batch.exe`、`video.exe`、`video_batch.exe`（4×28 MB ≈ **112 MB**） | 被 C++ 实现替代 |
| `bin\src\`（`core.py`、`image.py`、`image_batch.py`、`video.py`、`video_batch.py`、`setup.py`） | 逻辑已移植 |
| `bin\src\tests\`（6 个测试文件 + `__init__.py` + `conftest.py` + `helpers.py`） | Python 单测随源码删除；用 C++ 单测（注：工程已有 `tests/`）替换 |
| `bin\src\.coverage` | 产物 |
| `bin\src\__pycache__`（若存在，已被 `.gitignore` 覆盖） | 产物 |
| `bin\doc\exe使用指南.md` | 描述的是将被删除的 exe CLI；建议重写为《引擎与编码参数说明》或并入本规格 |
| `bin\ffmpeg\include\`、`bin\ffmpeg\lib\`（约 90 个头文件 + 导入库）、`bin\ffmpeg\bin\ffplay.exe` | 仅开发期需要；运行期只需要 `ffmpeg.exe`、`ffprobe.exe` 及其 DLL。**删除前确认工程无 FFmpeg 开发依赖**（当前 `.pro` 未链接 libav） |
| `bin\realesrgan\vcomp140d.dll` | Debug 版运行库，发布不需要（`vcomp140.dll` 保留） |
| `src\infrastructure\execommand.h/.cpp` | 参数拼装被 job runner 取代（除非保留 CLI 兼容层） |
| `src\common\constants.h:9-17` 中的 `EXE_DIR`、`IMAGE_EXE`、`IMAGE_BATCH_EXE`、`VIDEO_EXE`、`VIDEO_BATCH_EXE` | 改为引擎路径常量 |
| `.gitignore` 中 Python 段（第 1-12 行等） | 无 Python 源文件 |

### 7.2 **必须保留**

| 路径 | 理由 |
| --- | --- |
| `bin\realesrgan\realesrgan-ncnn-vulkan.exe` + `vcomp140.dll` | 算法引擎 |
| `bin\realesrgan\models\` 全部 10 个文件（含 `realesrgan-x4plus{,-anime}.{bin,param}`、`realesr-animevideov3-x{2,3,4}.{bin,param}`） | 模型权重；缺一即对应倍数/模型不可用 |
| `bin\ffmpeg\bin\ffmpeg.exe`、`ffprobe.exe` + 全部运行期 DLL（`avcodec-60`、`avformat-60`、`avutil-58`、`avfilter-9`、`swscale-7`、`swresample-4`、`avdevice-60`、`postproc-57`） | 拆帧/合成/探测 |
| `bin\ffmpeg\LICENSE.txt` | LGPL/GPL 合规 |
| 工程 `input\` | 验收素材（可另存到测试资源） |
| `resources\`、`tests\`、`src\` 其余分层代码 | 应用主体 |

### 7.3 依赖/构建清理

- 无 Python 依赖残留（`.pro`/README 中不引用 PyInstaller）；`README.md` 中「bin\exe 需先运行 setup.py」段落需更新（README 约 14 KB，含运行说明）。
- 打包脚本 `bin\src\setup.py` 删除后，发布流程改为「`qmake` + `make` 产出主程序 + 携带 `bin\realesrgan`、`bin\ffmpeg\bin`」。
- `Constants::Paths::EXE_DIR` 的自动发现逻辑（`exerunner.cpp:58-82` 向上 10 层找 `bin/exe`）改为找 `bin/realesrgan/realesrgan-ncnn-vulkan.exe` 与 `bin/ffmpeg/bin/ffmpeg.exe`。

---

## 8. 待产品/技术确认（不影响本文结论）

1. 输出 `-f webm` 时是否仍强制 VP9（当前 Python 忽略 `--encoding`）？
2. 是否接受「图片 BMP/GIF 输入强制输出 PNG」的行为变更（现存 bug 修复）？
3. 视频进度是否改为阶段加权（10/75/15）而非当前「帧超分进度直通」？
4. 是否需要保留 `image.exe` 等 CLI 入口（供脚本调用）？若需要，可用 `--cli` 模式复用 job runner。
