#ifndef FFMPEG_RAII_H
#define FFMPEG_RAII_H

// ============================================================================
// 基础设施层 - FFmpeg 资源 RAII 包装（媒体层内部使用）
//
// AVFormatContext / AVCodecContext / AVFrame / AVPacket / SwsContext /
// SwrContext 都是需要手工释放的 C 结构。本文件用 unique_ptr + 自定义删除器
// 统一托管，保证任何返回路径（含中途失败、提前取消）都不会泄漏资源。
// ============================================================================

// FFmpeg 9 的公开头文件不再自带 extern "C" 链接声明，C++ 调用方必须显式
// 以 C 链接包含，否则链接期找不到 av* 符号（FFmpeg 官方 C++ 用法约定）。
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <memory>

// 输入格式上下文：avformat_open_input 创建，avformat_close_input 释放
struct AvInputFormatContextDeleter
{
	void
	operator()(AVFormatContext *v_context) const
	{
		if (v_context != nullptr)
		{
			avformat_close_input(&v_context);
		}
	}
};
using AvInputFormatContextPtr
	= std::unique_ptr<AVFormatContext, AvInputFormatContextDeleter>;

// 输出格式上下文：avformat_alloc_output_context2 创建；
// 释放时先关闭 avio（若由 FFmpeg 打开）再释放上下文
struct AvOutputFormatContextDeleter
{
	void
	operator()(AVFormatContext *v_context) const
	{
		if (v_context == nullptr)
		{
			return;
		}
		if (v_context->pb != nullptr
			&& (v_context->oformat == nullptr
				|| (v_context->oformat->flags & AVFMT_NOFILE) == 0))
		{
			avio_closep(&v_context->pb);
		}
		avformat_free_context(v_context);
	}
};
using AvOutputFormatContextPtr
	= std::unique_ptr<AVFormatContext, AvOutputFormatContextDeleter>;

// 编解码上下文：avcodec_alloc_context3 创建，avcodec_free_context 释放
struct AvCodecContextDeleter
{
	void
	operator()(AVCodecContext *v_context) const
	{
		if (v_context != nullptr)
		{
			avcodec_free_context(&v_context);
		}
	}
};
using AvCodecContextPtr
	= std::unique_ptr<AVCodecContext, AvCodecContextDeleter>;

// 帧：av_frame_alloc 创建，av_frame_free 释放
struct AvFrameDeleter
{
	void
	operator()(AVFrame *v_frame) const
	{
		if (v_frame != nullptr)
		{
			av_frame_free(&v_frame);
		}
	}
};
using AvFramePtr = std::unique_ptr<AVFrame, AvFrameDeleter>;

// 包：av_packet_alloc 创建，av_packet_free 释放
struct AvPacketDeleter
{
	void
	operator()(AVPacket *v_packet) const
	{
		if (v_packet != nullptr)
		{
			av_packet_free(&v_packet);
		}
	}
};
using AvPacketPtr = std::unique_ptr<AVPacket, AvPacketDeleter>;

// 像素格式转换上下文：sws_getContext 创建，sws_freeContext 释放
struct SwsContextDeleter
{
	void
	operator()(SwsContext *v_context) const
	{
		if (v_context != nullptr)
		{
			sws_freeContext(v_context);
		}
	}
};
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

// 音频重采样上下文：swr_alloc_set_opts2 创建，swr_free 释放
struct SwrContextDeleter
{
	void
	operator()(SwrContext *v_context) const
	{
		if (v_context != nullptr)
		{
			swr_free(&v_context);
		}
	}
};
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

// 音频重采样 FIFO：重编码时按编码器固定帧长（如 libopus 960 样本/帧）重新分帧
struct AvAudioFifoDeleter
{
	void
	operator()(AVAudioFifo *v_fifo) const
	{
		if (v_fifo != nullptr)
		{
			av_audio_fifo_free(v_fifo);
		}
	}
};
using AvAudioFifoPtr = std::unique_ptr<AVAudioFifo, AvAudioFifoDeleter>;

#endif // FFMPEG_RAII_H
