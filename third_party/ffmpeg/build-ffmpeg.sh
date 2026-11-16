#!/usr/bin/env bash
# ============================================================================
# build-ffmpeg.sh - 构建内嵌 FFmpeg 静态库（Windows/MSYS2 与 Linux 通用）
#
# 用法：
#   ./build-ffmpeg.sh [选项]
#     --version <x.y.z>  要构建的版本（默认 9.0.2，对应本目录 ffmpeg-<版本>/ 源码）
#     --lgpl-only        仅保留 LGPL 兼容组件（不启用 x264/x265，即无 H.264/H.265
#                        编码器；VP9/AV1 仍可用）
#     --clean            构建前删除该版本的构建目录
#     --jobs N           并行任务数（默认 CPU 核数）
#     -h, --help         显示本帮助后退出（不构建）
#
# 产物：third_party/ffmpeg/build-ffmpeg-<版本>-<平台>/install/
#   ├── include/         libav* 头文件
#   ├── lib/             libavformat.a / libavcodec.a / libavutil.a / ...
#   └── lib/pkgconfig/   FFmpeg 自带 .pc 文件
#
# 构建后 CMake 使用方式（两种任选）：
#   cmake -S . -B build -DFFMPEG_ROOT=<install 目录>
#   或把 <install>/lib/pkgconfig 加入 PKG_CONFIG_PATH 后按默认方式配置。
#
# 许可证提示：默认启用 --enable-gpl --enable-libx264 --enable-libx265，
#   产物属于 GPL 组合作品；对外分发含该产物的程序必须遵守 GPL。
#   需要闭源/LGPL 分发时使用 --lgpl-only（H.264/H.265 编码不可用，
#   VideoEncoder 会返回 UNSUPPORTED；VP9/AV1 不受影响）。
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# FFmpeg configure 拒绝 MSYS 原生环境（uname/目标为 MSYS_NT*），
# 自动切到 UCRT64（等价于从「MSYS2 UCRT64」终端启动），保证 Windows 下可用。
case "$(uname -s)" in
	MSYS_*)
		export MSYSTEM=UCRT64
		export PATH="/ucrt64/bin:${PATH}"
		echo "提示：检测到 MSYS 环境，已自动切换为 UCRT64（MSYSTEM=UCRT64）"
		;;
esac

VERSION="9.0.2"
LGPL_ONLY=0
DO_CLEAN=0
JOBS=""

usage() {
	# 打印文件头部的注释块（第 1 个分隔线与第 2 个分隔线之间）
	awk 'NR == 1 { next }
	     /^# ={10,}$/ { separators++; if (separators == 2) { exit } }
	     { sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"
}

while [ $# -gt 0 ]; do
	case "$1" in
		--version)
			[ $# -ge 2 ] || { echo "错误：--version 需要参数" >&2; exit 2; }
			VERSION="$2"
			shift 2
			;;
		--lgpl-only)
			LGPL_ONLY=1
			shift
			;;
		--clean)
			DO_CLEAN=1
			shift
			;;
		--jobs)
			[ $# -ge 2 ] || { echo "错误：--jobs 需要参数" >&2; exit 2; }
			JOBS="$2"
			shift 2
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "错误：未知参数 $1（用 --help 查看用法）" >&2
			exit 2
			;;
	esac
done

SRC_DIR="${SCRIPT_DIR}/ffmpeg-${VERSION}"
if [ ! -d "${SRC_DIR}" ]; then
	echo "错误：找不到源码目录 ${SRC_DIR}" >&2
	echo "      请从 https://ffmpeg.org/releases/ffmpeg-${VERSION}.tar.xz 下载解压，" >&2
	echo "      详见 docs/design/ffmpeg-integration.md" >&2
	exit 1
fi

if [ -z "${JOBS}" ]; then
	if command -v nproc >/dev/null 2>&1; then
		JOBS="$(nproc)"
	elif command -v sysctl >/dev/null 2>&1; then
		JOBS="$(sysctl -n hw.ncpu)"
	else
		JOBS=4
	fi
fi

PLATFORM="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
BUILD_DIR="${SCRIPT_DIR}/build-ffmpeg-${VERSION}-${PLATFORM}"
PREFIX_DIR="${BUILD_DIR}/install"

have_pkg() {
	command -v pkg-config >/dev/null 2>&1 && pkg-config --exists "$1" >/dev/null 2>&1
}

if [ "${DO_CLEAN}" -eq 1 ] && [ -d "${BUILD_DIR}" ]; then
	echo "清理构建目录：${BUILD_DIR}"
	rm -rf "${BUILD_DIR}"
fi
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# ---------------------------------------------------------------------------
# 最小化静态库配置：只保留 libavformat/libavcodec/libavutil/libswscale/
# libswresample 与常用编解码器，不构建 ffmpeg/ffprobe 程序与文档。
# ---------------------------------------------------------------------------
CONFIGURE_ARGS=(
	--prefix="${PREFIX_DIR}"
	--disable-shared
	--enable-static
	--disable-programs
	--disable-doc
	--disable-avdevice
	--disable-avfilter
	--disable-network
	--disable-debug
	--disable-autodetect
)

# 编解码器：VP9 / AV1 走 BSD 系外部库，两种许可证模式下都可启用
if have_pkg "vpx"; then
	CONFIGURE_ARGS+=(--enable-libvpx)
else
	echo "提示：未找到 libvpx，跳过 VP9 编码器（用 MSYS2 的 mingw-w64-ucrt-x86_64-libvpx 安装）"
fi
if have_pkg "SvtAv1Enc"; then
	CONFIGURE_ARGS+=(--enable-libsvtav1)
else
	echo "提示：未找到 SVT-AV1，跳过 AV1 编码器（用 MSYS2 的 mingw-w64-ucrt-x86_64-svt-av1 安装）"
fi

# x86 汇编优化依赖 nasm；未安装时禁用，保证脚本可直接跑通（性能略降）
if command -v nasm >/dev/null 2>&1; then
	echo "nasm 版本：$(nasm -v 2>/dev/null | head -n 1)"
else
	echo "提示：未找到 nasm，追加 --disable-x86asm（禁用 x86 汇编优化）"
	CONFIGURE_ARGS+=(--disable-x86asm)
fi

# H.264 / H.265 编码器来自 GPL 的 x264 / x265，仅在非 LGPL-only 模式下启用
if [ "${LGPL_ONLY}" -eq 0 ]; then
	CONFIGURE_ARGS+=(--enable-gpl)
	if have_pkg "x264"; then
		CONFIGURE_ARGS+=(--enable-libx264)
	else
		echo "提示：未找到 libx264，跳过 H.264 编码器"
	fi
	if have_pkg "x265"; then
		CONFIGURE_ARGS+=(--enable-libx265)
	else
		echo "提示：未找到 libx265，跳过 H.265 编码器"
	fi
else
	echo "LGPL-only 模式：不启用 x264/x265（H.264/H.265 编码不可用）"
fi

echo "=== 配置 FFmpeg ${VERSION}（${PLATFORM}）==="
echo "源码：${SRC_DIR}"
echo "构建：${BUILD_DIR}"
echo "安装：${PREFIX_DIR}"
"${SRC_DIR}/configure" "${CONFIGURE_ARGS[@]}"

echo "=== 编译（-j${JOBS}）==="
make -j"${JOBS}"

echo "=== 安装 ==="
make install

echo ""
echo "构建完成：${PREFIX_DIR}"
echo "CMake 配置示例："
echo "  cmake -S \"${SCRIPT_DIR}/../..\" -B build -DFFMPEG_ROOT=\"${PREFIX_DIR}\""
