#include "videoencoder.h"

#include "ffmpegutils.h"
#include "mediaframe.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <algorithm>
#include <climits>
#include <cstring>
#include <vector>

namespace
{

// 编码器能力描述（libx264 / libx265 / libvpx-vp9 / libsvtav1 的选项差异）
struct EncoderChoice
{
	QString m_codec_name;	 // avcodec_find_encoder_by_name 使用的名字
	QString m_crf_option;	 // 质量选项名
	QString m_preset_option; // 预设选项名（空 = 该编码器不认 preset）
	bool m_zero_bitrate;	 // CRF 模式需要 bit_rate = 0（libvpx / libsvtav1）
};

EncoderChoice
choose_encoder(VideoEncoding v_encoding)
{
	switch (v_encoding)
	{
		case VideoEncoding::H264:
			return EncoderChoice{ QStringLiteral("libx264"),
								  QStringLiteral("crf"),
								  QStringLiteral("preset"), false };
		case VideoEncoding::H265:
			return EncoderChoice{ QStringLiteral("libx265"),
								  QStringLiteral("crf"),
								  QStringLiteral("preset"), false };
		case VideoEncoding::VP9:
			return EncoderChoice{ QStringLiteral("libvpx-vp9"),
								  QStringLiteral("crf"), QString(), true };
		case VideoEncoding::AV1:
			return EncoderChoice{ QStringLiteral("libsvtav1"),
								  QStringLiteral("crf"), QString(), true };
	}
	return EncoderChoice{ QString(), QString(), QString(), false };
}

// 输出文件采用「同目录临时文件 + 成功后替换」：编码失败/取消时不会留下
// 半成品，也不会在失败前就把已存在的旧产物截断（旧实现直接 avio_open
// 最终路径，失败即损坏旧文件）。临时文件保留原扩展名，FFmpeg 才能按扩展名
// 推断容器。
QString
make_working_output_path(const QString &v_final_path)
{
	const QFileInfo info(v_final_path);
	if (info.absolutePath().isEmpty())
	{
		return QString();
	}
	QString base = info.completeBaseName();
	if (base.isEmpty())
	{
		base = QStringLiteral("output");
	}
	const QString suffix = info.suffix();
	QString name
		= QStringLiteral(".%1.resr-part-%2")
			  .arg(base, QString::number(QCoreApplication::applicationPid()));
	if (!suffix.isEmpty())
	{
		name += QLatin1Char('.') + suffix;
	}
	return QDir(info.absolutePath()).filePath(name);
}

// 把临时输出改名为最终路径。Windows QFile::rename 不覆盖已存在目标；
// 这里先直接 rename（POSIX 可原子替换），失败再把旧产物改名为 .resr-prev
// 备份、放入新产物、成功后再删备份；新产物放入失败则恢复旧产物并保留临时
// 文件，避免"先删旧产物、后 rename 失败"导致旧成品丢失（task-38 P3）。
bool
publish_output_file(const QString &v_working_path, const QString &v_final_path,
					MediaError *v_error)
{
	const auto make_error = [v_final_path]()
	{
		return MediaError::make(
			MediaErrorCode::IO_FAILED,
			QCoreApplication::translate("VideoEncoder", "无法打开输出文件 %1")
				.arg(v_final_path));
	};
	if (QFile::rename(v_working_path, v_final_path))
	{
		return true;
	}

	const QString backup_path = v_final_path + QStringLiteral(".resr-prev");
	const bool had_old_output = QFileInfo::exists(v_final_path);
	if (had_old_output)
	{
		(void)QFile::remove(backup_path); // 清理上次异常中断的备份
		if (!QFile::rename(v_final_path, backup_path))
		{
			*v_error = make_error();
			return false;
		}
	}
	if (!QFile::rename(v_working_path, v_final_path))
	{
		if (had_old_output)
		{
			(void)QFile::rename(backup_path, v_final_path);
		}
		qCritical().noquote()
			<< QStringLiteral("Failed to publish video output; working file "
							  "retained at %1")
				   .arg(v_working_path);
		*v_error = make_error();
		return false;
	}
	if (had_old_output)
	{
		(void)QFile::remove(backup_path);
	}
	return true;
}

// 复制声道布局；失败时回退立体声并 WARN（避免把空布局交给编码器）。
// 返回 false 表示已回退；音频帧缓冲路径仍单独检查并报错。
bool
copy_channel_layout_or_default(AVChannelLayout *v_dst,
							   const AVChannelLayout *v_src)
{
	av_channel_layout_uninit(v_dst);
	if (av_channel_layout_copy(v_dst, v_src) == 0)
	{
		return true;
	}
	qWarning().noquote()
		<< "Failed to copy audio channel layout; fallback to stereo";
	av_channel_layout_default(v_dst, 2);
	return false;
}

// 把 limited range 的 YUV420P 帧填充为黑色（Y=16 / U=V=128），
// 用于宽高为奇数时补一像素黑边（yuv420p 要求宽高为偶数）
void
fill_yuv420p_black(AVFrame *v_frame)
{
	const int luma_height = v_frame->height;
	const int chroma_height = (v_frame->height + 1) / 2;
	for (int y = 0; y < luma_height; ++y)
	{
		std::memset(v_frame->data[0]
						+ static_cast<ptrdiff_t>(y) * v_frame->linesize[0],
					16, static_cast<size_t>(v_frame->linesize[0]));
	}
	for (int y = 0; y < chroma_height; ++y)
	{
		std::memset(v_frame->data[1]
						+ static_cast<ptrdiff_t>(y) * v_frame->linesize[1],
					128, static_cast<size_t>(v_frame->linesize[1]));
		std::memset(v_frame->data[2]
						+ static_cast<ptrdiff_t>(y) * v_frame->linesize[2],
					128, static_cast<size_t>(v_frame->linesize[2]));
	}
}

// 判断封装是否接受某个音频编码（用于决定 stream copy 还是重编码）
bool
muxer_accepts_audio_codec(const AVOutputFormat *v_format, AVCodecID v_codec_id)
{
	if (v_format == nullptr)
	{
		return false;
	}
	const int supported
		= avformat_query_codec(v_format, v_codec_id, FF_COMPLIANCE_NORMAL);
	if (supported == 1)
	{
		return true;
	}
	const QString format_name = (v_format->name != nullptr)
									? QString::fromUtf8(v_format->name)
									: QString();
	// webm 是 matroska 的受限子集：音频只允许 Vorbis / Opus。早期实现对
	// matroska/webm 一律放宽，导致 AAC 源在 webm 输出时错误地走 stream copy
	// → avformat_write_header 报「Only VP8 or VP9 or AV1 video and Vorbis or
	// Opus audio ...」直接失败。这里必须对 webm 收紧，强制走重编码分支。
	if (format_name.contains(QStringLiteral("webm")))
	{
		return v_codec_id == AV_CODEC_ID_VORBIS
			   || v_codec_id == AV_CODEC_ID_OPUS;
	}
	// matroska 的 query 对部分编码返回未知，按容器族放宽
	return format_name.contains(QStringLiteral("matroska"));
}

// 选择音频重编码器：webm 用 libopus，其余用 aac
const AVCodec *
pick_audio_encoder(const AVOutputFormat *v_output_format)
{
	const QString format_name
		= (v_output_format != nullptr && v_output_format->name != nullptr)
			  ? QString::fromUtf8(v_output_format->name)
			  : QString();
	if (format_name.contains(QStringLiteral("webm")))
	{
		return avcodec_find_encoder_by_name("libopus");
	}
	const AVCodec *encoder = avcodec_find_encoder_by_name("aac");
	if (encoder == nullptr)
	{
		encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
	}
	return encoder;
}

// 音频轨：复制（stream copy）或重编码两种模式共用
struct AudioTrack
{
	AvInputFormatContextPtr m_input;
	AVStream *m_input_stream = nullptr;
	AVStream *m_output_stream = nullptr;
	AvCodecContextPtr m_decoder;
	AvCodecContextPtr m_encoder;
	SwrContextPtr m_resampler;
	AvAudioFifoPtr m_fifo; // 编码器要求固定帧长时用于重新分帧
	int m_frame_size = 0;  // >0 = 编码器固定帧长（样本数，如 libopus 960@48k）
	bool m_copy = false;
	qint64 m_input_start = 0;	// 输入音频流起始时间（输入时基）
	double m_max_seconds = 0.0; // >0 时按秒数截断音频（两种模式共用）
	qint64 m_max_samples = 0;	// >0 时按输出采样数截断（重编码时使用）
};

// 打开音频来源并建立输出音频流（在写文件头之前调用）
bool
setup_audio_track(AVFormatContext *v_output_format,
				  const VideoEncoder::Options &v_options, AudioTrack *v_track,
				  MediaError *v_error)
{
	if (v_options.m_audio_source_path.trimmed().isEmpty())
	{
		return true;
	}

	const QFileInfo file_info(v_options.m_audio_source_path);
	if (!file_info.exists() || !file_info.isFile())
	{
		*v_error = MediaError::make(
			MediaErrorCode::FILE_NOT_FOUND,
			QCoreApplication::translate("VideoEncoder", "音频来源不存在 %1")
				.arg(v_options.m_audio_source_path));
		return false;
	}

	// FFmpeg 在 Windows 上把 char* 路径按 UTF-8 解释（内部转宽字符），因此这里
	// 必须用 QString::toUtf8()；QFile::encodeName 在中文 Windows 上是本地 ANSI
	// 编码（GBK），会打不开中文路径。Linux（UTF-8 locale）下两者等价。
	const QByteArray native_path = v_options.m_audio_source_path.toUtf8();
	AVFormatContext *raw_input = nullptr;
	int ret = avformat_open_input(&raw_input, native_path.constData(), nullptr,
								  nullptr);
	if (ret < 0 || raw_input == nullptr)
	{
		*v_error
			= MediaError::make(MediaErrorCode::OPEN_FAILED,
							   QCoreApplication::translate(
								   "VideoEncoder", "无法打开音频来源 %1（%2）")
								   .arg(v_options.m_audio_source_path,
										MediaFfmpeg::error_text(ret)));
		return false;
	}
	v_track->m_input.reset(raw_input);

	ret = avformat_find_stream_info(v_track->m_input.get(), nullptr);
	if (ret < 0)
	{
		*v_error = MediaError::make(
			MediaErrorCode::PROBE_FAILED,
			MediaFfmpeg::error_text(QCoreApplication::translate(
										"VideoEncoder", "无法读取音频流信息"),
									ret));
		return false;
	}

	const int audio_index = av_find_best_stream(
		v_track->m_input.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (audio_index < 0)
	{
		// 来源文件没有音频流：静默跳过音频（视频仍可正常编码）
		v_track->m_input.reset();
		return true;
	}

	AVStream *input_stream = v_track->m_input->streams[audio_index];
	v_track->m_input_stream = input_stream;
	v_track->m_input_start = (input_stream->start_time != AV_NOPTS_VALUE)
								 ? input_stream->start_time
								 : 0;
	v_track->m_max_seconds = v_options.m_audio_max_seconds;

	const AVOutputFormat *output_format = v_output_format->oformat;
	const bool can_copy = v_options.m_copy_audio
						  && muxer_accepts_audio_codec(
							  output_format, input_stream->codecpar->codec_id);
	if (can_copy)
	{
		AVStream *output_stream = avformat_new_stream(v_output_format, nullptr);
		if (output_stream == nullptr)
		{
			*v_error
				= MediaError::make(MediaErrorCode::INTERNAL,
								   QCoreApplication::translate(
									   "VideoEncoder", "无法创建输出音频流"));
			return false;
		}
		ret = avcodec_parameters_copy(output_stream->codecpar,
									  input_stream->codecpar);
		if (ret < 0)
		{
			*v_error = MediaError::make(
				MediaErrorCode::MUX_FAILED,
				MediaFfmpeg::error_text(
					QCoreApplication::translate("VideoEncoder",
												"无法复制音频编码参数"),
					ret));
			return false;
		}
		output_stream->codecpar->codec_tag = 0;
		output_stream->time_base = input_stream->time_base;
		if (input_stream->duration != AV_NOPTS_VALUE)
		{
			output_stream->duration = input_stream->duration;
		}
		v_track->m_output_stream = output_stream;
		v_track->m_copy = true;
		return true;
	}

	// ---- 重编码 ----
	const AVCodec *decoder
		= avcodec_find_decoder(input_stream->codecpar->codec_id);
	if (decoder == nullptr)
	{
		*v_error = MediaError::make(
			MediaErrorCode::UNSUPPORTED,
			QCoreApplication::translate("VideoEncoder",
										"当前 FFmpeg 构建缺少 %1 音频解码器")
				.arg(QString::fromUtf8(
					avcodec_get_name(input_stream->codecpar->codec_id))));
		return false;
	}
	v_track->m_decoder.reset(avcodec_alloc_context3(decoder));
	if (!v_track->m_decoder)
	{
		*v_error
			= MediaError::make(MediaErrorCode::INTERNAL,
							   QCoreApplication::translate(
								   "VideoEncoder", "无法分配音频解码器上下文"));
		return false;
	}
	ret = avcodec_parameters_to_context(v_track->m_decoder.get(),
										input_stream->codecpar);
	if (ret < 0)
	{
		*v_error = MediaError::make(
			MediaErrorCode::DECODE_FAILED,
			MediaFfmpeg::error_text(
				QCoreApplication::translate("VideoEncoder",
											"无法初始化音频解码参数"),
				ret));
		return false;
	}
	ret = avcodec_open2(v_track->m_decoder.get(), decoder, nullptr);
	if (ret < 0)
	{
		*v_error = MediaError::make(
			MediaErrorCode::DECODE_FAILED,
			MediaFfmpeg::error_text(QCoreApplication::translate(
										"VideoEncoder", "无法打开音频解码器"),
									ret));
		return false;
	}

	const AVCodec *encoder = pick_audio_encoder(output_format);
	if (encoder == nullptr)
	{
		*v_error = MediaError::make(
			MediaErrorCode::UNSUPPORTED,
			QCoreApplication::translate(
				"VideoEncoder",
				"当前 FFmpeg 构建缺少音频编码器，无法重编码音频"));
		return false;
	}

	v_track->m_encoder.reset(avcodec_alloc_context3(encoder));
	if (!v_track->m_encoder)
	{
		*v_error
			= MediaError::make(MediaErrorCode::INTERNAL,
							   QCoreApplication::translate(
								   "VideoEncoder", "无法分配音频编码器上下文"));
		return false;
	}

	// 查询编码器支持的采样率 / 采样格式 / 声道布局。
	// FFmpeg 8+ 的 AVCodec 已收窄为不透明结构，必须走 avcodec_get_supported_config。
	auto query_supported_config
		= [encoder](enum AVCodecConfig v_config) -> const void *
	{
		const void *configs = nullptr;
		if (avcodec_get_supported_config(nullptr, encoder, v_config, 0,
										 &configs, nullptr)
			< 0)
		{
			return nullptr;
		}
		return configs;
	};

	// 采样率：优先沿用输入采样率（编码器支持时），否则用其推荐值
	int sample_rate = input_stream->codecpar->sample_rate;
	const int *supported_sample_rates = static_cast<const int *>(
		query_supported_config(AV_CODEC_CONFIG_SAMPLE_RATE));
	if (supported_sample_rates != nullptr)
	{
		bool sample_rate_supported = false;
		for (const int *rate = supported_sample_rates; *rate != 0; ++rate)
		{
			if (*rate == sample_rate)
			{
				sample_rate_supported = true;
				break;
			}
		}
		if (!sample_rate_supported)
		{
			sample_rate = supported_sample_rates[0];
		}
	}
	if (sample_rate <= 0)
	{
		sample_rate = 48000;
	}

	const AVSampleFormat *supported_sample_formats
		= static_cast<const AVSampleFormat *>(
			query_supported_config(AV_CODEC_CONFIG_SAMPLE_FORMAT));
	v_track->m_encoder->sample_rate = sample_rate;
	v_track->m_encoder->sample_fmt = (supported_sample_formats != nullptr)
										 ? supported_sample_formats[0]
										 : AV_SAMPLE_FMT_FLTP;
	v_track->m_encoder->bit_rate = 128000;
	v_track->m_encoder->time_base = AVRational{ 1, sample_rate };

	// 声道布局：优先匹配输入布局，否则用编码器支持的第一种（最终回退立体声）
	const AVChannelLayout *supported_layouts
		= static_cast<const AVChannelLayout *>(
			query_supported_config(AV_CODEC_CONFIG_CHANNEL_LAYOUT));
	AVChannelLayout chosen_layout = {};
	bool layout_chosen = false;
	const AVChannelLayout *input_layout = &input_stream->codecpar->ch_layout;
	if (supported_layouts != nullptr)
	{
		for (const AVChannelLayout *layout = supported_layouts;
			 layout->nb_channels > 0; ++layout)
		{
			if (av_channel_layout_compare(layout, input_layout) == 0)
			{
				copy_channel_layout_or_default(&chosen_layout, layout);
				layout_chosen = true;
				break;
			}
		}
		if (!layout_chosen)
		{
			copy_channel_layout_or_default(&chosen_layout, supported_layouts);
			layout_chosen = true;
		}
	}
	if (!layout_chosen)
	{
		if (av_channel_layout_check(input_layout) == 1)
		{
			copy_channel_layout_or_default(&chosen_layout, input_layout);
		}
		else
		{
			av_channel_layout_default(&chosen_layout, 2);
		}
	}
	copy_channel_layout_or_default(&v_track->m_encoder->ch_layout,
								   &chosen_layout);

	if (output_format->flags & AVFMT_GLOBALHEADER)
	{
		v_track->m_encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	ret = avcodec_open2(v_track->m_encoder.get(), encoder, nullptr);
	if (ret < 0)
	{
		// 个别编码器不接受输入声道布局：回退立体声再试一次
		av_channel_layout_uninit(&v_track->m_encoder->ch_layout);
		av_channel_layout_default(&v_track->m_encoder->ch_layout, 2);
		ret = avcodec_open2(v_track->m_encoder.get(), encoder, nullptr);
	}
	av_channel_layout_uninit(&chosen_layout);
	if (ret < 0)
	{
		*v_error = MediaError::make(
			MediaErrorCode::ENCODE_FAILED,
			MediaFfmpeg::error_text(QCoreApplication::translate(
										"VideoEncoder", "无法打开音频编码器"),
									ret));
		return false;
	}

	// 固定帧长编码器（如 libopus：48000Hz 下 960 样本/帧）不接受任意长度的
	// 帧，直接 avcodec_send_frame 会报 "nb_samples > frame_size"。这里建
	// AVAudioFifo，把重采样后的数据按 frame_size 重新分帧再送编码器；
	// frame_size == 0 的编码器（可变帧长）保持直接发送。
	v_track->m_frame_size = v_track->m_encoder->frame_size;
	if (v_track->m_frame_size > 0)
	{
		AVAudioFifo *fifo = av_audio_fifo_alloc(
			v_track->m_encoder->sample_fmt,
			v_track->m_encoder->ch_layout.nb_channels, v_track->m_frame_size);
		if (fifo == nullptr)
		{
			*v_error = MediaError::make(
				MediaErrorCode::INTERNAL,
				QCoreApplication::translate("VideoEncoder",
											"无法分配音频重采样 FIFO"));
			return false;
		}
		v_track->m_fifo.reset(fifo);
	}

	// 重采样：解码帧（输入布局/格式/采样率）→ 编码器要求
	SwrContext *raw_resampler = nullptr;
	ret = swr_alloc_set_opts2(
		&raw_resampler, &v_track->m_encoder->ch_layout,
		v_track->m_encoder->sample_fmt, v_track->m_encoder->sample_rate,
		&v_track->m_decoder->ch_layout, v_track->m_decoder->sample_fmt,
		v_track->m_decoder->sample_rate, 0, nullptr);
	if (ret < 0 || raw_resampler == nullptr)
	{
		*v_error = MediaError::make(
			MediaErrorCode::INTERNAL,
			MediaFfmpeg::error_text(
				QCoreApplication::translate("VideoEncoder",
											"无法创建音频重采样上下文"),
				ret));
		return false;
	}
	v_track->m_resampler.reset(raw_resampler);
	ret = swr_init(v_track->m_resampler.get());
	if (ret < 0)
	{
		*v_error = MediaError::make(
			MediaErrorCode::INTERNAL,
			MediaFfmpeg::error_text(QCoreApplication::translate(
										"VideoEncoder", "无法初始化音频重采样"),
									ret));
		return false;
	}

	AVStream *output_stream = avformat_new_stream(v_output_format, nullptr);
	if (output_stream == nullptr)
	{
		*v_error = MediaError::make(
			MediaErrorCode::INTERNAL,
			QCoreApplication::translate("VideoEncoder", "无法创建输出音频流"));
		return false;
	}
	output_stream->time_base = v_track->m_encoder->time_base;
	ret = avcodec_parameters_from_context(output_stream->codecpar,
										  v_track->m_encoder.get());
	if (ret < 0)
	{
		*v_error = MediaError::make(
			MediaErrorCode::MUX_FAILED,
			MediaFfmpeg::error_text(QCoreApplication::translate(
										"VideoEncoder", "无法写出音频编码参数"),
									ret));
		return false;
	}
	output_stream->codecpar->codec_tag = 0;
	v_track->m_output_stream = output_stream;
	v_track->m_copy = false;

	if (v_options.m_audio_max_seconds > 0.0)
	{
		v_track->m_max_samples
			= static_cast<qint64>(v_options.m_audio_max_seconds * sample_rate);
	}
	return true;
}

// 把已编码音频包写入输出容器（复制模式下即 remux）
int
write_audio_packets(AVFormatContext *v_output_format, AudioTrack *v_track,
					AVPacket *v_packet)
{
	int written = 0;
	while (true)
	{
		const int ret
			= avcodec_receive_packet(v_track->m_encoder.get(), v_packet);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		{
			return written;
		}
		if (ret < 0)
		{
			return -1;
		}
		// 音频截断：丢弃超出时长上限的包
		if (v_track->m_max_samples > 0 && v_packet->pts != AV_NOPTS_VALUE
			&& v_packet->pts >= v_track->m_max_samples)
		{
			av_packet_unref(v_packet);
			continue;
		}
		av_packet_rescale_ts(v_packet, v_track->m_encoder->time_base,
							 v_track->m_output_stream->time_base);
		v_packet->stream_index = v_track->m_output_stream->index;
		v_packet->pos = -1;
		const int write_ret = av_write_frame(v_output_format, v_packet);
		av_packet_unref(v_packet);
		if (write_ret < 0)
		{
			return -2;
		}
		++written;
	}
}

// 写音频：复制模式直接 remux 输入包；重编码模式走 解码 → 重采样 → 编码
bool
write_audio_track(AVFormatContext *v_output_format, AudioTrack *v_track,
				  const std::function<bool()> &v_cancel, int *v_packet_count,
				  MediaError *v_error)
{
	if (v_track->m_input_stream == nullptr
		|| v_track->m_output_stream == nullptr)
	{
		return true;
	}

	if (v_track->m_copy)
	{
		AvPacketPtr packet(av_packet_alloc());
		if (!packet)
		{
			*v_error
				= MediaError::make(MediaErrorCode::INTERNAL,
								   QCoreApplication::translate(
									   "VideoEncoder", "无法分配音频数据包"));
			return false;
		}
		while (true)
		{
			if (v_cancel && v_cancel())
			{
				*v_error
					= MediaError::make(MediaErrorCode::CANCELLED,
									   QCoreApplication::translate(
										   "VideoEncoder", "视频合成已取消"));
				return false;
			}
			int ret = av_read_frame(v_track->m_input.get(), packet.get());
			if (ret == AVERROR_EOF)
			{
				break;
			}
			if (ret < 0)
			{
				*v_error = MediaError::make(
					MediaErrorCode::IO_FAILED,
					MediaFfmpeg::error_text(
						QCoreApplication::translate("VideoEncoder",
													"读取音频数据包失败"),
						ret));
				return false;
			}
			if (packet->stream_index != v_track->m_input_stream->index)
			{
				av_packet_unref(packet.get());
				continue;
			}

			if (packet->pts != AV_NOPTS_VALUE)
			{
				packet->pts -= v_track->m_input_start;
				if (packet->pts < 0)
				{
					packet->pts = 0;
				}
			}
			if (packet->dts != AV_NOPTS_VALUE)
			{
				packet->dts -= v_track->m_input_start;
				if (packet->dts < 0)
				{
					packet->dts = 0;
				}
			}

			av_packet_rescale_ts(packet.get(),
								 v_track->m_input_stream->time_base,
								 v_track->m_output_stream->time_base);
			packet->stream_index = v_track->m_output_stream->index;
			packet->pos = -1;

			// 按秒数截断音频：达到上限后不再写出（视频可能比原音频短）
			if (v_track->m_max_seconds > 0.0 && packet->pts != AV_NOPTS_VALUE)
			{
				const double seconds
					= packet->pts * av_q2d(v_track->m_output_stream->time_base);
				if (seconds >= v_track->m_max_seconds)
				{
					break;
				}
			}

			ret = av_write_frame(v_output_format, packet.get());
			av_packet_unref(packet.get());
			if (ret < 0)
			{
				*v_error = MediaError::make(
					MediaErrorCode::MUX_FAILED,
					MediaFfmpeg::error_text(
						QCoreApplication::translate("VideoEncoder",
													"写入音频数据包失败"),
						ret));
				return false;
			}
			++(*v_packet_count);
		}
		return true;
	}

	// ---- 重编码模式 ----
	AvPacketPtr input_packet(av_packet_alloc());
	AvFramePtr decoded_frame(av_frame_alloc());
	AvFramePtr converted_frame(av_frame_alloc());
	AvFramePtr framed_frame(av_frame_alloc());
	AvPacketPtr output_packet(av_packet_alloc());
	if (!input_packet || !decoded_frame || !converted_frame || !framed_frame
		|| !output_packet)
	{
		*v_error
			= MediaError::make(MediaErrorCode::INTERNAL,
							   QCoreApplication::translate(
								   "VideoEncoder", "无法分配音频处理对象"));
		return false;
	}

	qint64 next_pts = 0;
	bool stop_decode = false;
	MediaError encode_error;

	// 发送一帧到编码器并写包；返回 false 表示失败
	auto encode_frame = [&](AVFrame *v_frame) -> bool
	{
		const int send_ret
			= avcodec_send_frame(v_track->m_encoder.get(), v_frame);
		if (send_ret < 0)
		{
			encode_error = MediaError::make(
				MediaErrorCode::ENCODE_FAILED,
				MediaFfmpeg::error_text(
					QCoreApplication::translate("VideoEncoder",
												"音频编码失败：接收帧失败"),
					send_ret));
			return false;
		}
		const int written = write_audio_packets(v_output_format, v_track,
												output_packet.get());
		if (written < 0)
		{
			encode_error = MediaError::make(
				MediaErrorCode::MUX_FAILED,
				QCoreApplication::translate("VideoEncoder",
											"音频编码失败：写出数据包失败"));
			return false;
		}
		*v_packet_count += written;
		return true;
	};

	const int frame_size = v_track->m_frame_size;

	// 从 FIFO 取出编码器要求的固定帧长送编码；v_flush=true 时把不足一帧的
	// 尾部作为末帧发送（libopus 允许最后一帧短于 frame_size，不会多出静音）。
	auto drain_audio_fifo = [&](bool v_flush) -> bool
	{
		if (v_track->m_fifo == nullptr)
		{
			return true;
		}
		while (av_audio_fifo_size(v_track->m_fifo.get()) >= frame_size)
		{
			av_frame_unref(framed_frame.get());
			framed_frame->format = v_track->m_encoder->sample_fmt;
			framed_frame->sample_rate = v_track->m_encoder->sample_rate;
			if (av_channel_layout_copy(&framed_frame->ch_layout,
									   &v_track->m_encoder->ch_layout)
				< 0)
			{
				encode_error = MediaError::make(
					MediaErrorCode::INTERNAL,
					QCoreApplication::translate("VideoEncoder",
												"无法分配音频分帧缓冲"));
				return false;
			}
			framed_frame->nb_samples = frame_size;
			const int alloc_ret = av_frame_get_buffer(framed_frame.get(), 0);
			if (alloc_ret < 0)
			{
				encode_error = MediaError::make(
					MediaErrorCode::INTERNAL,
					MediaFfmpeg::error_text(
						QCoreApplication::translate("VideoEncoder",
													"无法分配音频分帧缓冲"),
						alloc_ret));
				return false;
			}
			const int read = av_audio_fifo_read(
				v_track->m_fifo.get(),
				reinterpret_cast<void **>(framed_frame->extended_data),
				frame_size);
			if (read != frame_size)
			{
				encode_error = MediaError::make(
					MediaErrorCode::INTERNAL,
					QCoreApplication::translate(
						"VideoEncoder", "音频 FIFO 读取样本数不足（%1/%2）")
						.arg(read)
						.arg(frame_size));
				return false;
			}
			framed_frame->nb_samples = frame_size;
			framed_frame->pts = next_pts;
			next_pts += frame_size;
			if (!encode_frame(framed_frame.get()))
			{
				return false;
			}
		}
		if (v_flush && av_audio_fifo_size(v_track->m_fifo.get()) > 0)
		{
			const int remaining = av_audio_fifo_size(v_track->m_fifo.get());
			av_frame_unref(framed_frame.get());
			framed_frame->format = v_track->m_encoder->sample_fmt;
			framed_frame->sample_rate = v_track->m_encoder->sample_rate;
			if (av_channel_layout_copy(&framed_frame->ch_layout,
									   &v_track->m_encoder->ch_layout)
				< 0)
			{
				encode_error = MediaError::make(
					MediaErrorCode::INTERNAL,
					QCoreApplication::translate("VideoEncoder",
												"无法分配音频末帧缓冲"));
				return false;
			}
			framed_frame->nb_samples = remaining;
			const int alloc_ret = av_frame_get_buffer(framed_frame.get(), 0);
			if (alloc_ret < 0)
			{
				encode_error = MediaError::make(
					MediaErrorCode::INTERNAL,
					MediaFfmpeg::error_text(
						QCoreApplication::translate("VideoEncoder",
													"无法分配音频末帧缓冲"),
						alloc_ret));
				return false;
			}
			const int read = av_audio_fifo_read(
				v_track->m_fifo.get(),
				reinterpret_cast<void **>(framed_frame->extended_data),
				remaining);
			if (read != remaining)
			{
				encode_error = MediaError::make(
					MediaErrorCode::INTERNAL,
					QCoreApplication::translate(
						"VideoEncoder", "音频 FIFO 末帧读取样本数不足（%1/%2）")
						.arg(read)
						.arg(remaining));
				return false;
			}
			framed_frame->nb_samples = remaining;
			framed_frame->pts = next_pts;
			next_pts += remaining;
			if (!encode_frame(framed_frame.get()))
			{
				return false;
			}
		}
		return true;
	};

	// 把重采样后的任意长度音频送编码器：固定帧长时先写 FIFO 分帧，
	// 可变帧长编码器保持原有直接发送行为。
	auto feed_audio_frame = [&](AVFrame *v_frame) -> bool
	{
		if (v_track->m_fifo == nullptr)
		{
			v_frame->pts = next_pts;
			next_pts += v_frame->nb_samples;
			return encode_frame(v_frame);
		}
		const int wrote = av_audio_fifo_write(
			v_track->m_fifo.get(),
			reinterpret_cast<void **>(v_frame->extended_data),
			v_frame->nb_samples);
		if (wrote != v_frame->nb_samples)
		{
			encode_error = MediaError::make(
				MediaErrorCode::INTERNAL,
				QCoreApplication::translate("VideoEncoder",
											"音频重采样 FIFO 写入失败（%1/%2）")
					.arg(wrote)
					.arg(v_frame->nb_samples));
			return false;
		}
		return drain_audio_fifo(false);
	};

	// 把解码帧重采样为编码器格式后送编码器
	auto convert_and_encode = [&](const AVFrame *v_frame) -> bool
	{
		int capacity = swr_get_out_samples(v_track->m_resampler.get(),
										   v_frame->nb_samples);
		if (capacity <= 0)
		{
			capacity = v_frame->nb_samples;
		}
		av_frame_unref(converted_frame.get());
		converted_frame->format = v_track->m_encoder->sample_fmt;
		converted_frame->sample_rate = v_track->m_encoder->sample_rate;
		if (av_channel_layout_copy(&converted_frame->ch_layout,
								   &v_track->m_encoder->ch_layout)
			< 0)
		{
			encode_error
				= MediaError::make(MediaErrorCode::INTERNAL,
								   QCoreApplication::translate(
									   "VideoEncoder", "无法分配重采样输出帧"));
			return false;
		}
		converted_frame->nb_samples = capacity;
		int ret = av_frame_get_buffer(converted_frame.get(), 0);
		if (ret < 0)
		{
			encode_error = MediaError::make(
				MediaErrorCode::INTERNAL,
				MediaFfmpeg::error_text(
					QCoreApplication::translate("VideoEncoder",
												"无法分配重采样输出帧"),
					ret));
			return false;
		}
		const int converted = swr_convert(
			v_track->m_resampler.get(), converted_frame->extended_data,
			capacity, const_cast<const uint8_t **>(v_frame->extended_data),
			v_frame->nb_samples);
		if (converted < 0)
		{
			encode_error = MediaError::make(
				MediaErrorCode::INTERNAL,
				MediaFfmpeg::error_text(QCoreApplication::translate(
											"VideoEncoder", "音频重采样失败"),
										converted));
			return false;
		}
		if (converted == 0)
		{
			return true;
		}
		converted_frame->nb_samples = converted;
		return feed_audio_frame(converted_frame.get());
	};

	// 取音频解码帧并送编码/重采样；主循环 EAGAIN 重试与普通路径共用。
	auto drain_audio_decoder = [&]() -> bool
	{
		while (true)
		{
			const int ret = avcodec_receive_frame(v_track->m_decoder.get(),
												  decoded_frame.get());
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			{
				return true;
			}
			if (ret < 0)
			{
				*v_error = MediaError::make(
					MediaErrorCode::DECODE_FAILED,
					MediaFfmpeg::error_text(
						QCoreApplication::translate("VideoEncoder",
													"音频解码失败：接收帧失败"),
						ret));
				return false;
			}
			if (!convert_and_encode(decoded_frame.get()))
			{
				*v_error = encode_error;
				return false;
			}
			av_frame_unref(decoded_frame.get());

			if (v_track->m_max_samples > 0
				&& next_pts >= v_track->m_max_samples)
			{
				stop_decode = true;
				return true;
			}
		}
	};

	// 解码循环
	while (!stop_decode)
	{
		if (v_cancel && v_cancel())
		{
			*v_error = MediaError::make(
				MediaErrorCode::CANCELLED,
				QCoreApplication::translate("VideoEncoder", "视频合成已取消"));
			return false;
		}
		int ret = av_read_frame(v_track->m_input.get(), input_packet.get());
		if (ret == AVERROR_EOF)
		{
			break;
		}
		if (ret < 0)
		{
			*v_error = MediaError::make(
				MediaErrorCode::IO_FAILED,
				MediaFfmpeg::error_text(
					QCoreApplication::translate("VideoEncoder",
												"读取音频数据包失败"),
					ret));
			return false;
		}
		if (input_packet->stream_index != v_track->m_input_stream->index)
		{
			av_packet_unref(input_packet.get());
			continue;
		}
		// 标准 send/receive 循环：EAGAIN 时先排空已解出的帧，再重发同一个包
		// （旧实现直接丢弃该包，长音频/某些解码器下会丢样本）。
		bool packet_sent = false;
		while (!packet_sent)
		{
			const int send_ret = avcodec_send_packet(v_track->m_decoder.get(),
													 input_packet.get());
			if (send_ret == AVERROR(EAGAIN))
			{
				if (!drain_audio_decoder())
				{
					av_packet_unref(input_packet.get());
					return false;
				}
				if (stop_decode)
				{
					av_packet_unref(input_packet.get());
					break;
				}
				continue;
			}
			if (send_ret < 0 && send_ret != AVERROR_EOF)
			{
				av_packet_unref(input_packet.get());
				*v_error = MediaError::make(
					MediaErrorCode::DECODE_FAILED,
					MediaFfmpeg::error_text(
						QCoreApplication::translate(
							"VideoEncoder", "音频解码失败：接收数据包失败"),
						send_ret));
				return false;
			}
			packet_sent = true;
		}
		av_packet_unref(input_packet.get());
		if (stop_decode)
		{
			break;
		}
		if (!drain_audio_decoder())
		{
			return false;
		}
	}

	// 冲刷解码器
	if (!stop_decode)
	{
		const int flush_ret
			= avcodec_send_packet(v_track->m_decoder.get(), nullptr);
		// AVERROR_EOF 表示此前已 flush 过，可继续取帧；其它负值属真实失败。
		if (flush_ret < 0 && flush_ret != AVERROR_EOF)
		{
			*v_error = MediaError::make(
				MediaErrorCode::DECODE_FAILED,
				MediaFfmpeg::error_text(
					QCoreApplication::translate("VideoEncoder",
												"音频解码失败：冲刷阶段失败"),
					flush_ret));
			return false;
		}
		while (true)
		{
			const int ret = avcodec_receive_frame(v_track->m_decoder.get(),
												  decoded_frame.get());
			if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
			{
				break;
			}
			if (ret < 0)
			{
				*v_error = MediaError::make(
					MediaErrorCode::DECODE_FAILED,
					MediaFfmpeg::error_text(
						QCoreApplication::translate(
							"VideoEncoder", "音频解码失败：冲刷阶段失败"),
						ret));
				return false;
			}
			if (!convert_and_encode(decoded_frame.get()))
			{
				*v_error = encode_error;
				return false;
			}
			av_frame_unref(decoded_frame.get());
		}
	}

	// 冲刷重采样器（采样率转换可能还有缓存样本）
	while (true)
	{
		const int capacity = 1024;
		av_frame_unref(converted_frame.get());
		converted_frame->format = v_track->m_encoder->sample_fmt;
		converted_frame->sample_rate = v_track->m_encoder->sample_rate;
		if (av_channel_layout_copy(&converted_frame->ch_layout,
								   &v_track->m_encoder->ch_layout)
			< 0)
		{
			*v_error
				= MediaError::make(MediaErrorCode::INTERNAL,
								   QCoreApplication::translate(
									   "VideoEncoder", "无法分配重采样输出帧"));
			return false;
		}
		converted_frame->nb_samples = capacity;
		const int alloc_ret = av_frame_get_buffer(converted_frame.get(), 0);
		if (alloc_ret < 0)
		{
			*v_error = MediaError::make(
				MediaErrorCode::INTERNAL,
				MediaFfmpeg::error_text(
					QCoreApplication::translate("VideoEncoder",
												"无法分配重采样输出帧"),
					alloc_ret));
			return false;
		}
		const int flushed
			= swr_convert(v_track->m_resampler.get(),
						  converted_frame->extended_data, capacity, nullptr, 0);
		if (flushed <= 0)
		{
			break;
		}
		if (v_track->m_max_samples > 0 && next_pts >= v_track->m_max_samples)
		{
			break;
		}
		converted_frame->nb_samples = flushed;
		if (!feed_audio_frame(converted_frame.get()))
		{
			*v_error = encode_error;
			return false;
		}
	}

	// 把 FIFO 中不足一帧的尾部作为末帧发送，再冲刷编码器
	if (!drain_audio_fifo(true))
	{
		*v_error = encode_error;
		return false;
	}

	// 冲刷编码器
	if (!encode_frame(nullptr))
	{
		*v_error = encode_error;
		return false;
	}
	return true;
}

} // namespace

void
VideoEncoder::set_progress_callback(ProgressCallback v_callback)
{
	m_progress_callback = std::move(v_callback);
}

void
VideoEncoder::set_cancel_checker(CancelChecker v_checker)
{
	m_cancel_checker = std::move(v_checker);
}

bool
VideoEncoder::cancel_requested() const
{
	return m_cancel_checker && m_cancel_checker();
}

void
VideoEncoder::report_progress(int v_frames_done, int v_frames_total) const
{
	if (m_progress_callback)
	{
		m_progress_callback(v_frames_done, v_frames_total);
	}
}

QString
VideoEncoder::encoder_name(VideoEncoding v_encoding)
{
	return choose_encoder(v_encoding).m_codec_name;
}

bool
VideoEncoder::is_encoder_available(VideoEncoding v_encoding)
{
	MediaFfmpeg::ensure_initialized();
	return avcodec_find_encoder_by_name(
			   encoder_name(v_encoding).toUtf8().constData())
		   != nullptr;
}

VideoEncoder::Result
VideoEncoder::encode_png_sequence(const QString &v_frames_dir,
								  const QString &v_output_path)
{
	return encode_png_sequence(v_frames_dir, v_output_path, Options());
}

VideoEncoder::Result
VideoEncoder::encode_png_sequence(const QString &v_frames_dir,
								  const QString &v_output_path,
								  const Options &v_options)
{
	// 先写同目录临时文件，成功后再替换最终产物：失败/取消不会留下半成品，
	// 已存在的旧成品也不会被提前截断。
	MediaFfmpeg::ensure_initialized();
	if (v_frames_dir.trimmed().isEmpty() || v_output_path.trimmed().isEmpty())
	{
		return encode_png_sequence_impl(
			v_frames_dir, OutputPaths{ v_output_path, v_output_path },
			v_options);
	}
	const QString working_path = make_working_output_path(v_output_path);
	if (working_path.isEmpty())
	{
		Result result;
		result.m_error = MediaError::make(
			MediaErrorCode::INVALID_ARGUMENT,
			QCoreApplication::translate("VideoEncoder",
										"视频合成失败：输出路径非法"));
		return result;
	}
	// pid 复用等极端情况下可能残留同名临时文件，先清理。
	(void)QFile::remove(working_path);

	Result result = encode_png_sequence_impl(
		v_frames_dir, OutputPaths{ working_path, v_output_path }, v_options);
	if (!result.m_ok)
	{
		(void)QFile::remove(working_path);
		return result;
	}
	if (!publish_output_file(working_path, v_output_path, &result.m_error))
	{
		// 发布失败时保留临时文件（含完整编码内容）供排查/手工恢复；
		// 仅在编码阶段失败或成功发布后才删除临时文件。
		result.m_ok = false;
	}
	return result;
}

VideoEncoder::Result
VideoEncoder::encode_png_sequence_impl(const QString &v_frames_dir,
									   const OutputPaths &v_output,
									   const Options &v_options)
{
	Result result;

	// ---- 参数校验 ----
	if (v_frames_dir.trimmed().isEmpty()
		|| v_output.m_display_path.trimmed().isEmpty())
	{
		result.m_error = MediaError::make(
			MediaErrorCode::INVALID_ARGUMENT,
			QCoreApplication::translate("VideoEncoder",
										"视频合成失败：帧目录或输出路径为空"));
		return result;
	}
	const QDir frames_directory(v_frames_dir);
	if (!frames_directory.exists())
	{
		result.m_error = MediaError::make(
			MediaErrorCode::INVALID_ARGUMENT,
			QCoreApplication::translate("VideoEncoder",
										"视频合成失败：帧目录不存在 %1")
				.arg(v_frames_dir));
		return result;
	}

	// 帧序列：与 FrameDecoder 共用 frame%08d.png 命名约定
	const QStringList frame_names = MediaFrameSequence::list_frame_files(
		v_frames_dir, QStringLiteral("frame"), QStringLiteral("png"));
	if (frame_names.isEmpty())
	{
		result.m_error = MediaError::make(
			MediaErrorCode::INVALID_ARGUMENT,
			QCoreApplication::translate(
				"VideoEncoder", "视频合成失败：帧目录中没有 frame*.png 文件 %1")
				.arg(v_frames_dir));
		return result;
	}
	const int frames_total = frame_names.size();
	std::vector<QString> frame_paths;
	frame_paths.reserve(static_cast<size_t>(frames_total));
	for (const QString &name : frame_names)
	{
		frame_paths.push_back(frames_directory.filePath(name));
	}

	// ---- 输出容器与视频流 ----
	// 同音频源：FFmpeg Windows 按 UTF-8 解释 char* 路径，中文/非 ASCII 输出
	// 路径必须用 toUtf8()（QFile::encodeName 是本地 8-bit 编码会乱码）。
	const QByteArray native_output_path = v_output.m_working_path.toUtf8();
	AVFormatContext *raw_output = nullptr;
	int ret = avformat_alloc_output_context2(&raw_output, nullptr, nullptr,
											 native_output_path.constData());
	if (ret < 0 || raw_output == nullptr)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::UNSUPPORTED,
			QCoreApplication::translate(
				"VideoEncoder", "视频合成失败：无法根据输出扩展名推断容器 %1")
				.arg(v_output.m_display_path));
		return result;
	}
	AvOutputFormatContextPtr output(raw_output);

	const EncoderChoice encoder_choice = choose_encoder(v_options.m_encoding);
	const AVCodec *video_codec = avcodec_find_encoder_by_name(
		encoder_choice.m_codec_name.toUtf8().constData());
	if (video_codec == nullptr)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::UNSUPPORTED,
			QCoreApplication::translate(
				"VideoEncoder", "视频合成失败：当前 FFmpeg 构建缺少 %1 编码器")
				.arg(encoder_choice.m_codec_name));
		return result;
	}

	// 帧率：精确有理数优先
	AVRational frame_rate;
	if (v_options.m_frame_rate_num > 0 && v_options.m_frame_rate_den > 0)
	{
		frame_rate = AVRational{ v_options.m_frame_rate_num,
								 v_options.m_frame_rate_den };
	}
	else
	{
		frame_rate = MediaFfmpeg::double_to_frame_rate(v_options.m_frame_rate);
	}
	if (frame_rate.num <= 0 || frame_rate.den <= 0)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::INVALID_ARGUMENT,
			QCoreApplication::translate("VideoEncoder",
										"视频合成失败：输出帧率非法（%1）")
				.arg(v_options.m_frame_rate));
		return result;
	}
	if (v_options.m_crf < 0 || v_options.m_crf > 63)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::INVALID_ARGUMENT,
			QCoreApplication::translate("VideoEncoder",
										"视频合成失败：CRF 超出范围（%1）")
				.arg(v_options.m_crf));
		return result;
	}

	// 用首帧确定画面尺寸；yuv420p 要求宽高为偶数，奇数时补一像素黑边
	const QImage first_image(frame_paths.front());
	if (first_image.isNull())
	{
		result.m_error = MediaError::make(
			MediaErrorCode::IO_FAILED,
			QCoreApplication::translate("VideoEncoder",
										"视频合成失败：无法读取帧文件 %1")
				.arg(frame_paths.front()));
		return result;
	}
	const int source_width = first_image.width();
	const int source_height = first_image.height();
	const int encode_width = source_width + (source_width % 2);
	const int encode_height = source_height + (source_height % 2);

	AvCodecContextPtr video_context(avcodec_alloc_context3(video_codec));
	if (!video_context)
	{
		result.m_error
			= MediaError::make(MediaErrorCode::INTERNAL,
							   QCoreApplication::translate(
								   "VideoEncoder", "无法分配视频编码器上下文"));
		return result;
	}
	video_context->width = encode_width;
	video_context->height = encode_height;
	video_context->time_base = av_inv_q(frame_rate);
	video_context->framerate = frame_rate;
	video_context->pix_fmt = AV_PIX_FMT_YUV420P;
	video_context->gop_size = 12;
	if (encoder_choice.m_zero_bitrate)
	{
		video_context->bit_rate = 0;
	}

	// 输出色彩标注：HD 用 BT.709、SD 用 BT.601，limited range（与编码换算一致）
	const AVColorSpace target_color_space
		= MediaFfmpeg::target_color_space(encode_height);
	video_context->color_range = AVCOL_RANGE_MPEG;
	video_context->colorspace = target_color_space;
	if (target_color_space == AVCOL_SPC_BT709)
	{
		video_context->color_primaries = AVCOL_PRI_BT709;
		video_context->color_trc = AVCOL_TRC_BT709;
	}
	else
	{
		video_context->color_primaries = AVCOL_PRI_SMPTE170M;
		video_context->color_trc = AVCOL_TRC_SMPTE170M;
	}

	AVDictionary *codec_options = nullptr;
	if (!encoder_choice.m_preset_option.isEmpty()
		&& !v_options.m_preset.trimmed().isEmpty())
	{
		av_dict_set(&codec_options,
					encoder_choice.m_preset_option.toUtf8().constData(),
					v_options.m_preset.toUtf8().constData(), 0);
	}
	av_dict_set(&codec_options,
				encoder_choice.m_crf_option.toUtf8().constData(),
				QString::number(v_options.m_crf).toUtf8().constData(), 0);
	if (output->oformat->flags & AVFMT_GLOBALHEADER)
	{
		video_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	ret = avcodec_open2(video_context.get(), video_codec, &codec_options);
	av_dict_free(&codec_options);
	if (ret < 0)
	{
		result.m_error
			= MediaError::make(MediaErrorCode::ENCODE_FAILED,
							   MediaFfmpeg::error_text(
								   QCoreApplication::translate(
									   "VideoEncoder", "无法打开 %1 视频编码器")
									   .arg(encoder_choice.m_codec_name),
								   ret));
		return result;
	}

	AVStream *video_stream = avformat_new_stream(output.get(), nullptr);
	if (video_stream == nullptr)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::INTERNAL,
			QCoreApplication::translate("VideoEncoder", "无法创建输出视频流"));
		return result;
	}
	video_stream->time_base = video_context->time_base;
	video_stream->avg_frame_rate = frame_rate;
	ret = avcodec_parameters_from_context(video_stream->codecpar,
										  video_context.get());
	if (ret < 0)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::MUX_FAILED,
			MediaFfmpeg::error_text(QCoreApplication::translate(
										"VideoEncoder", "无法写出视频编码参数"),
									ret));
		return result;
	}

	// ---- 音频流 ----
	AudioTrack audio_track;
	MediaError audio_error;
	if (!setup_audio_track(output.get(), v_options, &audio_track, &audio_error))
	{
		result.m_error = audio_error;
		return result;
	}

	// ---- 打开输出文件并写头部 ----
	if (native_output_path.isEmpty())
	{
		result.m_error = MediaError::make(
			MediaErrorCode::INVALID_ARGUMENT,
			QCoreApplication::translate("VideoEncoder",
										"视频合成失败：输出路径非法"));
		return result;
	}
	if ((output->oformat->flags & AVFMT_NOFILE) == 0)
	{
		ret = avio_open(&output->pb, native_output_path.constData(),
						AVIO_FLAG_WRITE);
		if (ret < 0)
		{
			result.m_error = MediaError::make(
				MediaErrorCode::IO_FAILED,
				MediaFfmpeg::error_text(
					QCoreApplication::translate("VideoEncoder",
												"无法打开输出文件 %1")
						.arg(v_output.m_display_path),
					ret));
			return result;
		}
	}
	ret = avformat_write_header(output.get(), nullptr);
	if (ret < 0)
	{
		result.m_error = MediaError::make(
			MediaErrorCode::MUX_FAILED,
			MediaFfmpeg::error_text(QCoreApplication::translate(
										"VideoEncoder", "无法写入输出文件头"),
									ret));
		return result;
	}

	// ---- 视频帧编码 ----
	AvFramePtr yuv_frame(av_frame_alloc());
	AvPacketPtr packet(av_packet_alloc());
	if (!yuv_frame || !packet)
	{
		result.m_error
			= MediaError::make(MediaErrorCode::INTERNAL,
							   QCoreApplication::translate(
								   "VideoEncoder", "无法分配编码帧 / 数据包"));
		return result;
	}

	SwsContextPtr sws;
	MediaError encode_error;

	// 从编码器取包并写输出；返回 false 表示失败
	auto write_video_packets = [&]() -> bool
	{
		while (true)
		{
			const int receive_ret
				= avcodec_receive_packet(video_context.get(), packet.get());
			if (receive_ret == AVERROR(EAGAIN) || receive_ret == AVERROR_EOF)
			{
				return true;
			}
			if (receive_ret < 0)
			{
				encode_error = MediaError::make(
					MediaErrorCode::ENCODE_FAILED,
					MediaFfmpeg::error_text(
						QCoreApplication::translate("VideoEncoder",
													"视频编码失败：取包失败"),
						receive_ret));
				return false;
			}
			av_packet_rescale_ts(packet.get(), video_context->time_base,
								 video_stream->time_base);
			// 补单帧时长：多数编码器返回的 packet 不带 duration，muxer 只按
			// 最后一个 PTS 计算流时长，产物会比源少一帧。按帧率折算到流时基。
			if (packet->duration <= 0)
			{
				packet->duration = av_rescale_q(1, av_inv_q(frame_rate),
												video_stream->time_base);
			}
			packet->stream_index = video_stream->index;
			const int write_ret = av_write_frame(output.get(), packet.get());
			av_packet_unref(packet.get());
			if (write_ret < 0)
			{
				encode_error = MediaError::make(
					MediaErrorCode::MUX_FAILED,
					MediaFfmpeg::error_text(
						QCoreApplication::translate(
							"VideoEncoder", "视频编码失败：写出数据包失败"),
						write_ret));
				return false;
			}
		}
	};

	// 送一帧到编码器并写包；v_frame 为 nullptr 表示冲刷
	auto encode_video_frame = [&](AVFrame *v_frame) -> bool
	{
		int send_ret = avcodec_send_frame(video_context.get(), v_frame);
		// 标准 send/receive：EAGAIN 表示编码器输出队列未排空，必须先取走
		// 已产出的 packet 再重发同一帧（旧实现把 EAGAIN 当致命错误）。
		while (send_ret == AVERROR(EAGAIN))
		{
			if (!write_video_packets())
			{
				return false;
			}
			send_ret = avcodec_send_frame(video_context.get(), v_frame);
		}
		if (send_ret < 0 && send_ret != AVERROR_EOF)
		{
			encode_error = MediaError::make(
				MediaErrorCode::ENCODE_FAILED,
				MediaFfmpeg::error_text(
					QCoreApplication::translate("VideoEncoder",
												"视频编码失败：发送帧失败"),
					send_ret));
			return false;
		}
		return write_video_packets();
	};

	report_progress(0, frames_total);

	int frames_encoded = 0;
	for (int index = 0; index < frames_total; ++index)
	{
		if (cancel_requested())
		{
			result.m_frames_encoded = frames_encoded;
			result.m_error = MediaError::make(
				MediaErrorCode::CANCELLED,
				QCoreApplication::translate("VideoEncoder", "视频合成已取消"));
			return result;
		}

		const QImage source_image(frame_paths[static_cast<size_t>(index)]);
		if (source_image.isNull())
		{
			result.m_frames_encoded = frames_encoded;
			result.m_error = MediaError::make(
				MediaErrorCode::IO_FAILED,
				QCoreApplication::translate("VideoEncoder",
											"视频合成失败：无法读取帧文件 %1")
					.arg(frame_paths[static_cast<size_t>(index)]));
			return result;
		}
		if (source_image.width() != source_width
			|| source_image.height() != source_height)
		{
			result.m_frames_encoded = frames_encoded;
			result.m_error = MediaError::make(
				MediaErrorCode::INVALID_ARGUMENT,
				QCoreApplication::translate(
					"VideoEncoder", "视频合成失败：帧尺寸不一致（%1 为 %2x%3）")
					.arg(frame_paths[static_cast<size_t>(index)])
					.arg(source_image.width())
					.arg(source_image.height()));
			return result;
		}

		const QImage rgba_image
			= source_image.convertToFormat(QImage::Format_RGBA8888);
		if (rgba_image.isNull())
		{
			result.m_frames_encoded = frames_encoded;
			result.m_error = MediaError::make(
				MediaErrorCode::IO_FAILED,
				QCoreApplication::translate(
					"VideoEncoder", "视频合成失败：帧文件格式转换失败 %1")
					.arg(frame_paths[static_cast<size_t>(index)]));
			return result;
		}

		// 颜色换算上下文只需创建一次（源尺寸 / 输出格式固定）
		if (!sws)
		{
			sws.reset(sws_getContext(source_width, source_height,
									 AV_PIX_FMT_RGBA, source_width,
									 source_height, AV_PIX_FMT_YUV420P,
									 SWS_BILINEAR, nullptr, nullptr, nullptr));
			if (!sws)
			{
				result.m_frames_encoded = frames_encoded;
				result.m_error = MediaError::make(
					MediaErrorCode::INTERNAL,
					QCoreApplication::translate(
						"VideoEncoder", "无法创建视频编码像素转换上下文"));
				return result;
			}
			MediaFfmpeg::configure_rgba_to_yuv(sws.get(), target_color_space,
											   source_height);
		}

		// 准备 YUV 目标帧（补齐黑边，保证 yuv420p 宽高为偶数）
		av_frame_unref(yuv_frame.get());
		yuv_frame->format = AV_PIX_FMT_YUV420P;
		yuv_frame->width = encode_width;
		yuv_frame->height = encode_height;
		ret = av_frame_get_buffer(yuv_frame.get(), 32);
		if (ret < 0)
		{
			result.m_frames_encoded = frames_encoded;
			result.m_error = MediaError::make(
				MediaErrorCode::INTERNAL,
				MediaFfmpeg::error_text(
					QCoreApplication::translate("VideoEncoder",
												"无法分配编码帧缓冲"),
					ret));
			return result;
		}
		fill_yuv420p_black(yuv_frame.get());

		const uint8_t *source_data[4]
			= { rgba_image.constBits(), nullptr, nullptr, nullptr };
		int source_linesize[4]
			= { static_cast<int>(rgba_image.bytesPerLine()), 0, 0, 0 };
		uint8_t *destination_data[4] = { yuv_frame->data[0], yuv_frame->data[1],
										 yuv_frame->data[2], nullptr };
		int destination_linesize[4]
			= { yuv_frame->linesize[0], yuv_frame->linesize[1],
				yuv_frame->linesize[2], 0 };
		const int scaled
			= sws_scale(sws.get(), source_data, source_linesize, 0,
						source_height, destination_data, destination_linesize);
		if (scaled <= 0)
		{
			result.m_frames_encoded = frames_encoded;
			result.m_error = MediaError::make(
				MediaErrorCode::ENCODE_FAILED,
				QCoreApplication::translate("VideoEncoder",
											"视频编码失败：像素格式转换失败"));
			return result;
		}

		yuv_frame->pts = frames_encoded;
		if (!encode_video_frame(yuv_frame.get()))
		{
			result.m_frames_encoded = frames_encoded;
			result.m_error = encode_error;
			return result;
		}
		++frames_encoded;
		report_progress(frames_encoded, frames_total);
	}

	// 冲刷视频编码器
	if (!encode_video_frame(nullptr))
	{
		result.m_frames_encoded = frames_encoded;
		result.m_error = encode_error;
		return result;
	}

	// ---- 音频编码 / 复制 ----
	if (!write_audio_track(
			output.get(), &audio_track, [this]() { return cancel_requested(); },
			&result.m_audio_packets, &audio_error))
	{
		result.m_frames_encoded = frames_encoded;
		result.m_error = audio_error;
		return result;
	}
	result.m_audio_copied = audio_track.m_copy;

	// ---- 收尾 ----
	ret = av_write_trailer(output.get());
	if (ret < 0)
	{
		result.m_frames_encoded = frames_encoded;
		result.m_error = MediaError::make(
			MediaErrorCode::MUX_FAILED,
			MediaFfmpeg::error_text(QCoreApplication::translate(
										"VideoEncoder", "无法写入输出文件尾"),
									ret));
		return result;
	}

	result.m_frames_encoded = frames_encoded;
	result.m_ok = true;
	return result;
}
