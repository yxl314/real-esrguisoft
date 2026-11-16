# third_party/ffmpeg - 内嵌 FFmpeg 源码

本目录内嵌 **FFmpeg 9.0.2** 官方源码（来源：<https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz>），
用于替代旧 `bin/ffmpeg/` 里 142MB 的 Windows-only 预编译 exe/DLL 包，并为
Windows / Linux 提供可复现的静态库构建路径。

- **默认构建不编译本源码**：工程通过 CMake 的 `cmake/FindFFmpeg.cmake`
  优先使用系统已安装的 FFmpeg 开发包（MSYS2 UCRT64 的 FFmpeg 9.x 即可零构建使用）。
- 只有需要自包含 / 固定版本的产物时，才运行 `build-ffmpeg.sh` / `build-ffmpeg.ps1`
  构建静态库，然后 `cmake -DFFMPEG_ROOT=<install 目录>` 指向构建产物。
- 完整说明（目录结构、构建步骤、许可证与分发注意事项）见
  [`docs/design/ffmpeg-integration.md`](../../docs/design/ffmpeg-integration.md)。

## 版本策略

- 每个版本一个目录：`ffmpeg-<version>/`（当前仅 `ffmpeg-9.0.2/`）。
- 构建产物放在 `build-ffmpeg-<version>-<platform>/`，由 `.gitignore` 排除。
- 升级版本：下载新的官方 tar.xz，解压到 `ffmpeg-<new-version>/`，删除 `tests/`、
  `doc/`、`tools/`、`.forgejo/`（本工程不构建测试与文档），再更新
  `build-ffmpeg.sh` 默认 `--version` 与 `docs/design/ffmpeg-integration.md`。

## 源码裁剪说明

为控制仓库体积，内嵌副本已删除以下与库构建无关的目录（**不含 .git**）：

| 已删除目录 | 体积 | 原因 |
| --- | --- | --- |
| `tests/` | ~10.5 MB / 5170 文件 | FATE 测试集，本工程不跑 FFmpeg 自测 |
| `doc/` | ~2.8 MB | 构建脚本启用 `--disable-doc` |
| `tools/` | ~0.4 MB | 独立小工具，不参与库构建 |
| `.forgejo/` | 极小 | 上游 CI 配置 |

完整官方源码可用以下命令恢复（任选其一）：

```bash
# Linux / MSYS2
curl -sSL -o ffmpeg-9.0.2.tar.xz https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz
tar -xf ffmpeg-9.0.2.tar.xz
```

```powershell
# Windows PowerShell
curl.exe -sSL --ssl-no-revoke -o ffmpeg-9.0.2.tar.xz https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz
tar -xf ffmpeg-9.0.2.tar.xz
```

## 许可证

FFmpeg 主体为 **LGPL v2.1+**；若启用 `--enable-gpl`（默认构建脚本会启用 x264/x265），
产物整体变为 **GPL**。仓库内只有源码，不包含任何预编译二进制。
分发要求详见 `docs/design/ffmpeg-integration.md` 的「许可证与分发」章节。
