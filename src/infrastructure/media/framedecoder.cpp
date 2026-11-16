#include "framedecoder.h"

#include "ffmpegutils.h"
#include "mediaframe.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <algorithm>
#include <climits>

namespace
{

// sws 上下文重建依据：分辨率 / 像素格式 / 色彩参数任一变化都要重建
struct SwsCache
{
	SwsContextPtr m_sws;
	int m_width = 0;
	int m_height = 0;
	int m_pixel_format = -1;
	AVColorSpace m_color_space = AVCOL_SPC_UNSPECIFIED;
	AVColorRange m_color_range = AVCOL_RANGE_UNSPECIFIED;
};

// 把解码帧转换成 RGBA8888 的 QImage（全范围 sRGB），色彩矩阵按流标签换算
bool
convert_frame_to_image(const AVFrame *v_frame,
					   AVColorSpace v_stream_color_space,
					   AVColorRange v_stream_color_range, SwsCache *v_cache,
					   QImage *v_image, MediaError *v_error)
{
	const int width = v_frame->width;
	const int height = v_frame->height;
	if (width <= 0 || height <= 0)
	{
		*v_error = MediaError::make(
			MediaErrorCode::DECODE_FAILED,
			QCoreApplication::translate("FrameDecoder",
										"解码得到的帧尺寸非法：%1x%2")
				.arg(width)
				.arg(height));
		return false;
	}

	const AVPixelFormat source_format
		= static_cast<AVPixelFormat>(v_frame->format);
	// 帧自带的色彩参数优先，缺失时回退到流级标注
	const AVColorSpace color_space
		= (v_frame->colorspace != AVCOL_SPC_UNSPECIFIED) ? v_frame->colorspace
														 : v_stream_color_space;
	const AVColorRange color_range
		= (v_frame->color_range != AVCOL_RANGE_UNSPECIFIED)
			  ? v_frame->color_range
			  : v_stream_color_range;

	if (!v_cache->m_sws || v_cache->m_width != width
		|| v_cache->m_height != height
		|| v_cache->m_pixel_format != static_cast<int>(source_format)
		|| v_cache->m_color_space != color_space
		|| v_cache->m_color_range != color_range)
	{
		v_cache->m_sws.reset(sws_getContext(
			width, height, source_format, width, height, AV_PIX_FMT_RGBA,
			SWS_BILINEAR, nullptr, nullptr, nullptr));
		if (!v_cache->m_sws)
		{
			*v_error = MediaError::make(
				MediaErrorCode::INTERNAL,
				QCoreApplication::translate(
					"FrameDecoder", "无法创建像素格式转换上下文（%1x%2）")
					.arg(width)
					.arg(height));
			return false;
		}
		MediaFfmpeg::configure_yuv_to_rgba(v_cache->m_sws.get(), color_space,
										   color_range, height);
		v_cache->m_width = width;
		v_cache->m_height = height;
		v_cache->m_pixel_format = static_cast<int>(source_format);
		v_cache->m_color_space = color_space;
		v_cache->m_color_range = color_range;
	}

	QImage image(width, height, QImage::Format_RGBA8888);
	if (image.isNull())
	{
		*v_error = MediaError::make(
			MediaErrorCode::INTERNAL,
			QCoreApplication::translate("FrameDecoder",
										"无法分配帧图像内存（%1x%2）")
				.arg(width)
				.arg(height));
		return false;
	}

	uint8_t *destination_data[4] = { image.bits(), nullptr, nullptr, nullptr };
	int destination_linesize[4]
		= { static_cast<int>(image.bytesPerLine()), 0, 0, 0 };
	const int scaled_rows
		= sws_scale(v_cache->m_sws.get(), v_frame->data, v_frame->linesize, 0,
					height, destination_data, destination_linesize);
	if (scaled_rows <= 0)
	{
		*v_error = MediaError::make(
			MediaErrorCode::DECODE_FAILED,
			QCoreApplication::translate("FrameDecoder", "像素格式转换失败"));
		return false;
	}

	*v_image = image;
	return true;
}

} // namespace

void
FrameDecoder::set_progress_callback(ProgressCallback v_callback)
{
	m_progress_callback = std::move(v_callback);
}

void
FrameDecoder::set_cancel_checker(CancelChecker v_checker)
{
	m_cancel_checker = std::move(v_checker);
}

bool
FrameDecoder::cancel_requested() const
{
	return m_cancel_checker && m_cancel_checker();
}

void
FrameDecoder::report_progress(int v_frames_done, int v_frames_total) const
{
	if (m_progress_callback)
	{
		m_progress_callback(v_frames_done, v_frames_total);
	}
}

FrameDecoder::Result
FrameDecoder::decode_to_png_sequence(const QString &v_input_path,
									 const QString &v_output_dir)
{
	return decode_to_png_sequence(v_input_path, v_output_dir, Options());
}

FrameDecoder::Result
FrameDecoder::decode_to_png_sequence(const QString &v_input_path,
									 const QString &v_output_dir,
									 const Options &v_options)
{
	Result result;
	MediaFfmpeg::ensure_initialized();

	if (v_input_path.trimmed().isEmpty() || v_output_dir.trimmed().isEmpty())
	{
		result.m_error = MediaError::make(
			MediaErrorCode::INVALID_ARGUMENT,
			QCoreApplication::translate("FrameDecoder",
										"拆帧失败：输入路径或帧输出目录为空"));
		return result;
	}
	if (!QFileInfo(v_input_path).isFile())
	{
		result.m_error = MediaError::make(
			MediaErrorCode::FILE_NOT_FOUND,
			QCoreApplication::translate("FrameDecoder",
										"拆帧失败：输入视频不存在 %1")
				.arg(v_input_path));
		return result;
	}

	// 先探测：拿到总帧数（进度分母）与色彩标签，并把 Probe 结果留在结果里复用
	const MediaProbeResult probe = MediaProbe::probe(v_input_path);
	if (!probe.m_ok)
	{
		result.m_error = probe.m_error;
		return result;
	}
	result.m_source = probe.m_info;
	if (!probe.m_info.m_has_video)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::STREAM_NOT_FOUND,
			QCoreApplication::translate("FrameDecoder",
										"拆帧失败：文件中没有视频流 %1")
				.arg(v_input_path));
		return result;
	}

	const QString extension
		= v_options.m_frame_extension.startsWith(QLatin1Char('.'))
			  ? v_options.m_frame_extension.mid(1)
			  : v_options.m_frame_extension;
	const int frames_total = static_cast<int>(std::min<qint64>(
		probe.m_info.m_video.m_frame_count, static_cast<qint64>(INT_MAX)));
	result.m_frames_total = frames_total;

	// 打开输入容器
	// FFmpeg Windows 按 UTF-8 解释 char* 路径；QFile::encodeName 是本地 ANSI
	// 编码（中文 Windows 为 GBK），非 ASCII 路径会打不开。Linux 下两者等价。
	const QByteArray native_path = v_input_path.toUtf8();
	AVFormatContext *raw_context = nullptr;
	int ret = avformat_open_input(&raw_context, native_path.constData(),
								  nullptr, nullptr);
	if (ret < 0 || raw_context == nullptr)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::OPEN_FAILED,
			QCoreApplication::translate("FrameDecoder",
										"拆帧失败：无法打开输入视频 %1（%2）")
				.arg(v_input_path, MediaFfmpeg::error_text(ret)));
		return result;
	}
	AvInputFormatContextPtr format(raw_context);

	ret = avformat_find_stream_info(format.get(), nullptr);
	if (ret < 0)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::PROBE_FAILED,
			MediaFfmpeg::error_text(
				QCoreApplication::translate("FrameDecoder",
											"拆帧失败：无法读取流信息"),
				ret));
		return result;
	}

	// 选最佳视频流并打开解码器
	const int video_index = av_find_best_stream(
		format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (video_index < 0)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::STREAM_NOT_FOUND,
			QCoreApplication::translate("FrameDecoder",
										"拆帧失败：找不到可解码的视频流"));
		return result;
	}
	AVStream *video_stream = format->streams[video_index];
	const AVCodec *decoder
		= avcodec_find_decoder(video_stream->codecpar->codec_id);
	if (decoder == nullptr)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::UNSUPPORTED,
			QCoreApplication::translate(
				"FrameDecoder", "拆帧失败：当前 FFmpeg 构建缺少 %1 解码器")
				.arg(QString::fromUtf8(
					avcodec_get_name(video_stream->codecpar->codec_id))));
		return result;
	}

	AvCodecContextPtr decoder_context(avcodec_alloc_context3(decoder));
	if (!decoder_context)
	{
		result.m_error
			= MediaError::make(MediaErrorCode::INTERNAL,
							   QCoreApplication::translate(
								   "FrameDecoder", "无法分配解码器上下文"));
		return result;
	}
	ret = avcodec_parameters_to_context(decoder_context.get(),
										video_stream->codecpar);
	if (ret < 0)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::DECODE_FAILED,
			MediaFfmpeg::error_text(QCoreApplication::translate(
										"FrameDecoder", "无法初始化解码参数"),
									ret));
		return result;
	}
	// 单线程解码：拆帧阶段按调用方节流，避免默认多线程造成的资源争抢
	decoder_context->thread_count = 1;
	ret = avcodec_open2(decoder_context.get(), decoder, nullptr);
	if (ret < 0)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::DECODE_FAILED,
			MediaFfmpeg::error_text(
				QCoreApplication::translate("FrameDecoder", "无法打开解码器"),
				ret));
		return result;
	}

	// 帧输出目录（已存在则复用，不清理旧文件，由调用方决定）
	QDir output_directory(v_output_dir);
	if (!output_directory.exists() && !QDir().mkpath(v_output_dir))
	{
		result.m_error = MediaError::make(
			MediaErrorCode::IO_FAILED,
			QCoreApplication::translate("FrameDecoder",
										"拆帧失败：无法创建帧输出目录 %1")
				.arg(v_output_dir));
		return result;
	}

	const AVColorSpace stream_color_space = video_stream->codecpar->color_space;
	const AVColorRange stream_color_range = video_stream->codecpar->color_range;

	AvFramePtr frame(av_frame_alloc());
	AvPacketPtr packet(av_packet_alloc());
	if (!frame || !packet)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::INTERNAL,
			QCoreApplication::translate("FrameDecoder", "无法分配帧 / 包对象"));
		return result;
	}

	SwsCache sws_cache;
	QImage image;
	MediaError conversion_error;
	int frames_written = 0;
	bool reached_limit = false;

	// 单帧写出：转换 + PNG 落盘 + 进度回调；供主循环与解码器 flush 复用
	auto write_frame = [&]() -> bool
	{
		if (!convert_frame_to_image(frame.get(), stream_color_space,
									stream_color_range, &sws_cache, &image,
									&conversion_error))
		{
			result.m_error = conversion_error;
			return false;
		}

		const QString file_path
			= output_directory.filePath(MediaFrameSequence::frame_file_name(
				v_options.m_frame_prefix, extension, frames_written + 1));
		if (!image.save(file_path, "PNG"))
		{
			result.m_error = MediaError::make(
				MediaErrorCode::IO_FAILED,
				QCoreApplication::translate("FrameDecoder",
											"拆帧失败：无法写入帧文件 %1")
					.arg(file_path));
			return false;
		}

		++frames_written;
		report_progress(frames_written, frames_total);
		return true;
	};

	report_progress(0, frames_total);

	// 取帧并写出：主循环与解码器 flush 共用同一段逻辑，避免两处重复代码
	// 各自演化（旧实现是两份 25 行拷贝，CPD 27 行重复）。
	// 返回 false 表示错误/取消（result 已填充）；返回 true 时若 reached_limit
	// 为 true 表示已达到帧数上限。
	auto drain_decoder = [&]() -> bool
	{
		while (true)
		{
			ret = avcodec_receive_frame(decoder_context.get(), frame.get());
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			{
				return true;
			}
			if (ret < 0)
			{
				result.m_error = MediaError::make(
					MediaErrorCode::DECODE_FAILED,
					MediaFfmpeg::error_text(
						QCoreApplication::translate("FrameDecoder",
													"拆帧失败：解码视频帧失败"),
						ret));
				result.m_frames_written = frames_written;
				return false;
			}

			if (cancel_requested())
			{
				av_frame_unref(frame.get());
				result.m_frames_written = frames_written;
				result.m_error = MediaError::make(
					MediaErrorCode::CANCELLED,
					QCoreApplication::translate("FrameDecoder", "拆帧已取消"));
				return false;
			}

			if (!write_frame())
			{
				av_frame_unref(frame.get());
				result.m_frames_written = frames_written;
				return false;
			}
			av_frame_unref(frame.get());

			if (v_options.m_max_frames > 0
				&& frames_written >= v_options.m_max_frames)
			{
				reached_limit = true;
				return true;
			}
		}
	};

	while (!reached_limit)
	{
		ret = av_read_frame(format.get(), packet.get());
		if (ret == AVERROR_EOF)
		{
			break;
		}
		if (ret < 0)
		{
			result.m_error = MediaError::make(
				MediaErrorCode::DECODE_FAILED,
				MediaFfmpeg::error_text(
					QCoreApplication::translate("FrameDecoder",
												"拆帧失败：读取视频包失败"),
					ret));
			result.m_frames_written = frames_written;
			return result;
		}

		if (packet->stream_index != video_index)
		{
			av_packet_unref(packet.get());
			continue;
		}

		// 标准 FFmpeg send/receive 循环：EAGAIN 表示解码器输入队列满，
		// 必须先取走已解出的帧再重发同一个包。旧实现把 EAGAIN 当作普通
		// 成功直接进入 receive，导致该包被静默丢弃（潜在丢帧）。
		bool packet_sent = false;
		while (!packet_sent)
		{
			ret = avcodec_send_packet(decoder_context.get(), packet.get());
			if (ret == AVERROR(EAGAIN))
			{
				if (!drain_decoder())
				{
					av_packet_unref(packet.get());
					return result;
				}
				if (reached_limit)
				{
					av_packet_unref(packet.get());
					break;
				}
				continue;
			}
			if (ret < 0 && ret != AVERROR_EOF)
			{
				av_packet_unref(packet.get());
				result.m_error = MediaError::make(
					MediaErrorCode::DECODE_FAILED,
					MediaFfmpeg::error_text(
						QCoreApplication::translate(
							"FrameDecoder", "拆帧失败：解码器拒绝数据包"),
						ret));
				result.m_frames_written = frames_written;
				return result;
			}
			packet_sent = true;
		}
		av_packet_unref(packet.get());
		if (reached_limit)
		{
			break;
		}
		if (!drain_decoder())
		{
			return result;
		}
	}

	// 达到帧数上限时不再冲刷解码器（后续帧本来就要丢弃）
	if (!reached_limit)
	{
		bool flush_sent = false;
		while (!flush_sent)
		{
			ret = avcodec_send_packet(decoder_context.get(), nullptr);
			if (ret == AVERROR(EAGAIN))
			{
				if (!drain_decoder())
				{
					return result;
				}
				continue;
			}
			if (ret < 0 && ret != AVERROR_EOF)
			{
				result.m_error = MediaError::make(
					MediaErrorCode::DECODE_FAILED,
					MediaFfmpeg::error_text(
						QCoreApplication::translate("FrameDecoder",
													"拆帧失败：冲刷解码器失败"),
						ret));
				result.m_frames_written = frames_written;
				return result;
			}
			flush_sent = true;
		}
		if (!drain_decoder())
		{
			return result;
		}
	}

	result.m_frames_written = frames_written;
	result.m_ok = true;
	return result;
}
