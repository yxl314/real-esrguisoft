#ifndef VIDEOENCODER_H
#define VIDEOENCODER_H

// ============================================================================
// 基础设施层 - 视频合成编码
//
// 对应旧 Python 编排里 ffmpeg 的合成命令（video.py:288-405 分支矩阵）：
//   帧序列（PNG）+ 原视频音频 → 目标容器 / 编码（h264/h265/vp9/av1 + crf + preset）
// 本类直接用 libavcodec + libswscale + libavformat 完成，不再启动 ffmpeg.exe。
// 音频优先 stream copy，目标容器不支持该音频编码时自动重编码（mp4→aac、
// webm→libopus）。
//
// 线程模型：encode_png_sequence() 为阻塞调用（建议放在工作线程），
// 进度 / 取消回调都在调用线程同步触发。
// ============================================================================

#include "mediaerror.h"

#include "mediasettings.h"

#include <QString>

#include <functional>

class VideoEncoder
{
  public:
	// 进度回调：已编码帧数 / 总帧数（PNG 序列文件数）
	using ProgressCallback
		= std::function<void(int v_frames_done, int v_frames_total)>;
	// 取消谓词：每帧轮询一次，返回 true 表示请求取消
	using CancelChecker = std::function<bool()>;

	struct Options
	{
		VideoEncoding m_encoding = VideoEncoding::H264; // 目标视频编码
		int m_crf = 23;								 // 质量（0-51，越大越差）
		QString m_preset = QStringLiteral("medium"); // x264/x265 预设名
		double m_frame_rate = 30.0;					 // 输出帧率
		// 精确帧率覆盖（>0 时优先于 m_frame_rate），如 24000/1001
		int m_frame_rate_num = 0;
		int m_frame_rate_den = 1;
		QString
			m_audio_source_path; // 音频来源（通常是被拆帧的原视频）；空 = 无音频
		bool m_copy_audio = true;		  // 优先 stream copy；false 强制重编码
		double m_audio_max_seconds = 0.0; // >0 时把音频截断到该时长
	};

	struct Result
	{
		bool m_ok = false;
		int m_frames_encoded = 0;	 // 实际编码帧数
		int m_audio_packets = 0;	 // 写入输出文件的音频包数
		bool m_audio_copied = false; // true = 音频流直通，false = 重编码
		MediaError m_error;
	};

	void
	set_progress_callback(ProgressCallback v_callback);
	void
	set_cancel_checker(CancelChecker v_checker);

	// 把 frames_dir 下的 frame%08d.<ext> 序列编码为 v_output_path；
	// 容器类型由输出扩展名决定（.mp4/.mkv/.webm/.avi/.mov）。
	Result
	encode_png_sequence(const QString &v_frames_dir,
						const QString &v_output_path);
	Result
	encode_png_sequence(const QString &v_frames_dir,
						const QString &v_output_path, const Options &v_options);

	// 编码格式 → FFmpeg 编码器名（如 H264 -> "libx264"）
	static QString
	encoder_name(VideoEncoding v_encoding);

	// 当前 FFmpeg 构建是否带该编码器（缺 libx264 / libsvtav1 时返回 false）
	static bool
	is_encoder_available(VideoEncoding v_encoding);

  private:
	// 输出路径对：m_working_path 为同目录临时输出（FFmpeg 实际写入），
	// m_display_path 仅用于用户可见错误文案。
	struct OutputPaths
	{
		QString m_working_path;
		QString m_display_path;
	};

	// 实际编码实现（公开入口负责临时文件 → 最终产物的发布/清理）。
	Result
	encode_png_sequence_impl(const QString &v_frames_dir,
							 const OutputPaths &v_output,
							 const Options &v_options);

	bool
	cancel_requested() const;
	void
	report_progress(int v_frames_done, int v_frames_total) const;

	ProgressCallback m_progress_callback;
	CancelChecker m_cancel_checker;
};

#endif // VIDEOENCODER_H
