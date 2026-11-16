#ifndef FRAMEDECODER_H
#define FRAMEDECODER_H

// ============================================================================
// 基础设施层 - 视频拆帧
//
// 对应旧 Python 编排里 ffmpeg 的拆帧命令（video.py:127-177）：
//   ffmpeg -i <输入> -qscale:v 1 ... <帧目录>/frame%08d.jpg
// 本类直接用 libavcodec + libswscale 解码并写成无损 PNG 序列（frame%08d.png），
// 不再启动 ffmpeg.exe；色彩矩阵按流的 bt709 / bt601 标签正确换算。
//
// 线程模型：decode_to_png_sequence() 为阻塞调用（建议放在工作线程），
// 进度 / 取消回调都在调用线程同步触发。
// ============================================================================

#include "mediaerror.h"
#include "mediaprobe.h"

#include <QString>

#include <functional>

class FrameDecoder
{
  public:
	// 进度回调：已完成帧数 / 源视频总帧数（探测值，可能为估算；0 表示未知）
	using ProgressCallback
		= std::function<void(int v_frames_done, int v_frames_total)>;
	// 取消谓词：每帧轮询一次，返回 true 表示请求取消
	using CancelChecker = std::function<bool()>;

	struct Options
	{
		int m_max_frames
			= 0; // >0 时只解码前 N 帧（冒烟测试 / 预览用）；0 = 全部
		QString m_frame_prefix = QStringLiteral("frame");
		QString m_frame_extension = QStringLiteral("png");
	};

	struct Result
	{
		bool m_ok = false;
		int m_frames_written = 0; // 实际写出的帧数
		int m_frames_total = 0;	  // 源视频总帧数（探测估算值）
		MediaInfo m_source;		  // 源文件探测结果，可直接传给 VideoEncoder
		MediaError m_error;
	};

	void
	set_progress_callback(ProgressCallback v_callback);
	void
	set_cancel_checker(CancelChecker v_checker);

	// 把视频逐帧解码为 PNG 序列写入 v_output_dir（目录不存在时自动创建）。
	// 帧文件名为 frame + 8 位补零序号 + .png，从 00000001 起。
	Result
	decode_to_png_sequence(const QString &v_input_path,
						   const QString &v_output_dir);
	Result
	decode_to_png_sequence(const QString &v_input_path,
						   const QString &v_output_dir,
						   const Options &v_options);

  private:
	bool
	cancel_requested() const;
	void
	report_progress(int v_frames_done, int v_frames_total) const;

	ProgressCallback m_progress_callback;
	CancelChecker m_cancel_checker;
};

#endif // FRAMEDECODER_H
