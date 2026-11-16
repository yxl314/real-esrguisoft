#ifndef FFMPEGUTILS_H
#define FFMPEGUTILS_H

// ============================================================================
// 基础设施层 - FFmpeg 公共工具
//
// 媒体层各封装类共享的底层工具：进程内初始化、AVERROR 文本化、帧率与
// 色彩空间换算。只依赖 libav*，不感知任何业务语义，供 media 目录内部使用。
// ============================================================================

#include "ffmpeg_raii.h"

#include <QString>

namespace MediaFfmpeg
{

// 进程内 FFmpeg 全局初始化（幂等）：
// 关闭 FFmpeg 自身的控制台日志（错误统一通过 MediaError 返回），
// 保证库被多处调用时不会重复初始化。
void
ensure_initialized();

// AVERROR 错误码 → 可读文本
QString
error_text(int v_error_code);

// "操作描述：错误文本" 组合，作为 MediaError::m_message
QString
error_text(const QString &v_operation, int v_error_code);

// 有理数 → double（分母为 0 或非法时返回 0.0）
double
rational_to_double(AVRational v_rational);

// 取流帧率：优先 avg_frame_rate，其次 r_frame_rate；均无效返回 {0, 1}
AVRational
stream_frame_rate(const AVStream *v_stream);

// 帧率 double → 有理数（encoding 时构造 time_base；非法输入返回 {0, 1}）
AVRational
double_to_frame_rate(double v_frame_rate);

// 帧率 → "23.976" 文本（保留 3 位小数；无效返回 "未知"）
QString
frame_rate_label(AVRational v_frame_rate);

// AVColorSpace → "bt709" / "bt601" / "bt2020" / "unknown"
QString
color_space_name(AVColorSpace v_color_space);

// 选择 swscale 色彩矩阵（SWS_CS_*）：未标注时按画面高度猜，
// >= 720 行按 BT.709，否则按 BT.601（与 ffmpeg 命令行默认行为一致）
int
sws_matrix(AVColorSpace v_color_space, int v_height);

// 配置「YUV 源 → RGBA 目标」的 swscale 色彩参数（解码用）：
// 源按有限/全范围与标注矩阵，目标固定为全范围 RGB
void
configure_yuv_to_rgba(SwsContext *v_sws, AVColorSpace v_color_space,
					  AVColorRange v_color_range, int v_height);

// 配置「RGBA 源 → YUV 目标」的 swscale 色彩参数（编码用）：
// 源为全范围 RGB，目标为 limited range YUV、矩阵与输出流标注一致
void
configure_rgba_to_yuv(SwsContext *v_sws, AVColorSpace v_color_space,
					  int v_height);

// 按输出高度选择编码器色彩标注（HD 用 BT.709，SD 用 BT.601）
AVColorSpace
target_color_space(int v_height);

} // namespace MediaFfmpeg

#endif // FFMPEGUTILS_H
