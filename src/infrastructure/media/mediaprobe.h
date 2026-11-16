#ifndef MEDIAPROBE_H
#define MEDIAPROBE_H

// ============================================================================
// 基础设施层 - 媒体探测
//
// 对应旧 Python 编排里 ffprobe 的角色（video.py:48-124 / core.py:478-517）：
// 直接调用 libavformat 打开容器并读取流信息，不启动外部进程。
// 探测结果 MediaInfo 同时被 FrameDecoder / VideoEncoder 复用，避免重复打开。
// ============================================================================

#include "mediaerror.h"

#include <QString>

#include <QtGlobal>

// 视频流信息
struct VideoStreamInfo
{
	int m_index = -1;		   // 容器内流序号
	QString m_codec_name;	   // 编码名，如 "h264"
	int m_width = 0;		   // 显示宽
	int m_height = 0;		   // 显示高
	QString m_pixel_format;	   // 像素格式，如 "yuv420p"
	double m_frame_rate = 0.0; // 平均帧率（如 23.976）
	int m_frame_rate_num = 0;  // 帧率有理数分子（如 1199）
	int m_frame_rate_den = 1;  // 帧率有理数分母（如 50）
	qint64 m_frame_count = 0;  // 帧数：nb_frames 元数据优先，否则按时长估算
	bool m_frame_count_estimated = false; // 帧数是否为估算值
	qint64 m_bit_rate = 0;				  // 码率（bit/s，可能为 0）
	QString m_color_space; // 色彩矩阵标签："bt709"/"bt601"/"unknown"
};

// 音频流信息
struct AudioStreamInfo
{
	int m_index = -1;		  // 容器内流序号
	QString m_codec_name;	  // 编码名，如 "aac"
	int m_sample_rate = 0;	  // 采样率（Hz）
	int m_channels = 0;		  // 声道数
	QString m_channel_layout; // 声道布局，如 "stereo"
	qint64 m_bit_rate = 0;	  // 码率（bit/s，可能为 0）
};

// 媒体文件完整探测结果
struct MediaInfo
{
	QString m_path;					 // 输入路径
	QString m_format_name;			 // 容器短名，如 "mov,mp4,m4a,3gp,3g2,mj2"
	QString m_format_long_name;		 // 容器长名
	double m_duration_seconds = 0.0; // 容器时长（秒，缺失时为流时长兜底）
	qint64 m_bit_rate = 0;			 // 容器总码率（bit/s，可能为 0）
	bool m_has_video = false;		 // 是否含视频流
	bool m_has_audio = false;		 // 是否含音频流
	VideoStreamInfo m_video;		 // 视频流（m_has_video 为 true 时有效）
	AudioStreamInfo m_audio;		 // 音频流（m_has_audio 为 true 时有效）
};

// 探测结果：ok 为 true 时 m_info 有效，否则 m_error 说明原因
struct MediaProbeResult
{
	bool m_ok = false;
	MediaInfo m_info;
	MediaError m_error;
};

class MediaProbe
{
  public:
	// 探测媒体文件（阻塞，可在任意线程调用）
	static MediaProbeResult
	probe(const QString &v_path);
};

#endif // MEDIAPROBE_H
