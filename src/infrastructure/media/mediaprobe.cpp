#include "mediaprobe.h"

#include "ffmpegutils.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

#include <cmath>

namespace
{

// 容器时长：优先 format->duration，缺失时取各流时长的最大值兜底
double
resolve_duration_seconds(const AVFormatContext *v_format)
{
	if (v_format->duration != AV_NOPTS_VALUE && v_format->duration > 0)
	{
		return static_cast<double>(v_format->duration)
			   / static_cast<double>(AV_TIME_BASE);
	}

	double longest_seconds = 0.0;
	for (unsigned int i = 0; i < v_format->nb_streams; ++i)
	{
		const AVStream *stream = v_format->streams[i];
		if (stream->duration == AV_NOPTS_VALUE || stream->duration <= 0)
		{
			continue;
		}
		const double seconds
			= static_cast<double>(stream->duration) * av_q2d(stream->time_base);
		if (seconds > longest_seconds)
		{
			longest_seconds = seconds;
		}
	}
	return longest_seconds;
}

// 填视频流信息；注意帧数是「元数据优先、时长×帧率兜底」的估算
void
fill_video_info(AVFormatContext *v_format, int v_stream_index,
				const MediaInfo &v_media, VideoStreamInfo *v_video)
{
	AVStream *stream = v_format->streams[v_stream_index];
	const AVCodecParameters *parameters = stream->codecpar;

	v_video->m_index = v_stream_index;
	v_video->m_codec_name
		= QString::fromUtf8(avcodec_get_name(parameters->codec_id));
	v_video->m_width = parameters->width;
	v_video->m_height = parameters->height;

	const char *pixel_format
		= av_get_pix_fmt_name(static_cast<AVPixelFormat>(parameters->format));
	v_video->m_pixel_format = (pixel_format != nullptr)
								  ? QString::fromUtf8(pixel_format)
								  : QString();

	// 帧率：先读流自带的 avg_frame_rate / r_frame_rate，再让 FFmpeg 按容器猜
	AVRational frame_rate = MediaFfmpeg::stream_frame_rate(stream);
	if (frame_rate.num <= 0 || frame_rate.den <= 0)
	{
		frame_rate = av_guess_frame_rate(v_format, stream, nullptr);
	}
	if (frame_rate.num > 0 && frame_rate.den > 0)
	{
		v_video->m_frame_rate_num = frame_rate.num;
		v_video->m_frame_rate_den = frame_rate.den;
		v_video->m_frame_rate = MediaFfmpeg::rational_to_double(frame_rate);
	}
	else
	{
		v_video->m_frame_rate_num = 0;
		v_video->m_frame_rate_den = 1;
		v_video->m_frame_rate = 0.0;
	}

	// 容器未记录码率时保持 0（由调用方按文件大小 / 时长自行估算）
	v_video->m_bit_rate = parameters->bit_rate;
	v_video->m_color_space
		= MediaFfmpeg::color_space_name(parameters->color_space);

	// 帧数：优先容器元数据（mp4 的 nb_frames），否则按时长 × 帧率估算
	if (stream->nb_frames > 0)
	{
		v_video->m_frame_count = stream->nb_frames;
		v_video->m_frame_count_estimated = false;
	}
	else if (v_video->m_frame_rate > 0.0 && v_media.m_duration_seconds > 0.0)
	{
		v_video->m_frame_count = static_cast<qint64>(
			std::llround(v_video->m_frame_rate * v_media.m_duration_seconds));
		v_video->m_frame_count_estimated = true;
	}
	else
	{
		v_video->m_frame_count = 0;
		v_video->m_frame_count_estimated = true;
	}
}

// 填音频流信息
void
fill_audio_info(const AVStream *v_stream, int v_stream_index,
				AudioStreamInfo *v_audio)
{
	const AVCodecParameters *parameters = v_stream->codecpar;

	v_audio->m_index = v_stream_index;
	v_audio->m_codec_name
		= QString::fromUtf8(avcodec_get_name(parameters->codec_id));
	v_audio->m_sample_rate = parameters->sample_rate;
	v_audio->m_channels = parameters->ch_layout.nb_channels;
	v_audio->m_bit_rate = parameters->bit_rate;

	char layout_buffer[128] = { 0 };
	if (av_channel_layout_describe(&parameters->ch_layout, layout_buffer,
								   sizeof(layout_buffer))
		> 0)
	{
		v_audio->m_channel_layout = QString::fromUtf8(layout_buffer);
	}
}

} // namespace

MediaProbeResult
MediaProbe::probe(const QString &v_path)
{
	MediaProbeResult result;
	MediaFfmpeg::ensure_initialized();

	if (v_path.trimmed().isEmpty())
	{
		result.m_error
			= MediaError::make(MediaErrorCode::INVALID_ARGUMENT,
							   QCoreApplication::translate(
								   "MediaProbe", "探测失败：输入路径为空"));
		return result;
	}

	const QFileInfo file_info(v_path);
	if (!file_info.exists() || !file_info.isFile())
	{
		result.m_error
			= MediaError::make(MediaErrorCode::FILE_NOT_FOUND,
							   QCoreApplication::translate(
								   "MediaProbe", "探测失败：输入文件不存在 %1")
								   .arg(v_path));
		return result;
	}

	// FFmpeg Windows 按 UTF-8 解释 char* 路径（内部转宽字符）；这里必须用
	// toUtf8()，QFile::encodeName 在中文 Windows 上是 GBK，中文路径会失败。
	const QByteArray native_path = v_path.toUtf8();
	AVFormatContext *raw_context = nullptr;
	int ret = avformat_open_input(&raw_context, native_path.constData(),
								  nullptr, nullptr);
	if (ret < 0 || raw_context == nullptr)
	{
		result.m_error
			= MediaError::make(MediaErrorCode::OPEN_FAILED,
							   QCoreApplication::translate(
								   "MediaProbe", "无法打开媒体文件 %1（%2）")
								   .arg(v_path, MediaFfmpeg::error_text(ret)));
		return result;
	}
	AvInputFormatContextPtr format(raw_context);

	ret = avformat_find_stream_info(format.get(), nullptr);
	if (ret < 0)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::PROBE_FAILED,
			MediaFfmpeg::error_text(
				QCoreApplication::translate("MediaProbe", "无法读取流信息"),
				ret));
		return result;
	}

	MediaInfo &media = result.m_info;
	media.m_path = v_path;
	if (format->iformat != nullptr)
	{
		if (format->iformat->name != nullptr)
		{
			media.m_format_name = QString::fromUtf8(format->iformat->name);
		}
		if (format->iformat->long_name != nullptr)
		{
			media.m_format_long_name
				= QString::fromUtf8(format->iformat->long_name);
		}
	}
	media.m_duration_seconds = resolve_duration_seconds(format.get());
	media.m_bit_rate = format->bit_rate;

	// 视频流：取 FFmpeg 判定的「最佳」流，与 ffprobe 的 default 流语义一致
	const int video_index = av_find_best_stream(
		format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (video_index >= 0)
	{
		fill_video_info(format.get(), video_index, media, &media.m_video);
		media.m_has_video = true;
	}

	// 音频流
	const int audio_index = av_find_best_stream(
		format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (audio_index >= 0)
	{
		fill_audio_info(format->streams[audio_index], audio_index,
						&media.m_audio);
		media.m_has_audio = true;
	}

	if (!media.m_has_video && !media.m_has_audio)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::STREAM_NOT_FOUND,
			QCoreApplication::translate("MediaProbe",
										"媒体文件既没有视频流也没有音频流：%1")
				.arg(v_path));
		return result;
	}

	result.m_ok = true;
	return result;
}
