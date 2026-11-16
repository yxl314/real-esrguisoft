#include "mediasettings.h"

#include <QCoreApplication>

// ---------------------------------------------------------------------------
// 枚举 -> 字符串
// ---------------------------------------------------------------------------
QString
media_kind_to_string(MediaKind v_kind)
{
	return v_kind == MediaKind::VIDEO
			   ? QCoreApplication::translate("MediaSettings", "视频")
			   : QCoreApplication::translate("MediaSettings", "图片");
}

QString
media_kind_token(MediaKind v_kind)
{
	// 日志 / 技术标识用（不翻译）：保证英文界面下日志不出现中英混杂。
	return v_kind == MediaKind::VIDEO ? QStringLiteral("video")
									  : QStringLiteral("image");
}

QString
image_type_to_string(ImageType v_type)
{
	switch (v_type)
	{
		case ImageType::REAL_IMAGE:
			return QCoreApplication::translate("MediaSettings", "真实图片");
		case ImageType::ANIME_IMAGE:
			return QCoreApplication::translate("MediaSettings", "动漫图片");
	}
	return QCoreApplication::translate("MediaSettings", "真实图片");
}

QString
model_type_to_string(ModelType v_type)
{
	switch (v_type)
	{
		case ModelType::REAL_ESRGAN_X4PLUS:
			return QStringLiteral("realesrgan-x4plus");
		case ModelType::REAL_ESRGAN_X4PLUS_ANIME:
			return QStringLiteral("realesrgan-x4plus-anime");
		case ModelType::REAL_ESR_ANIMEVIDEOV3:
			return QStringLiteral("realesr-animevideov3");
	}
	return QStringLiteral("realesrgan-x4plus");
}

QString
scale_factor_to_string(ScaleFactor v_factor)
{
	switch (v_factor)
	{
		case ScaleFactor::SCALE2X:
			return QCoreApplication::translate("MediaSettings", "2倍");
		case ScaleFactor::SCALE3X:
			return QCoreApplication::translate("MediaSettings", "3倍");
		case ScaleFactor::SCALE4X:
			return QCoreApplication::translate("MediaSettings", "4倍");
	}
	return QCoreApplication::translate("MediaSettings", "4倍");
}

QString
process_mode_to_string(ProcessMode v_mode)
{
	switch (v_mode)
	{
		case ProcessMode::SINGLE_FILE:
			return QCoreApplication::translate("MediaSettings", "单文件处理");
		case ProcessMode::BATCH_PROCESS:
			return QCoreApplication::translate("MediaSettings", "批量处理");
	}
	return QCoreApplication::translate("MediaSettings", "单文件处理");
}

QString
video_encoding_to_string(VideoEncoding v_encoding)
{
	switch (v_encoding)
	{
		case VideoEncoding::H264:
			return QStringLiteral("H.264");
		case VideoEncoding::H265:
			return QStringLiteral("H.265");
		case VideoEncoding::VP9:
			return QStringLiteral("VP9");
		case VideoEncoding::AV1:
			return QStringLiteral("AV1");
	}
	return QStringLiteral("H.264");
}

QString
video_quality_to_string(VideoQuality v_quality)
{
	switch (v_quality)
	{
		case VideoQuality::FAST:
			return QCoreApplication::translate("MediaSettings", "快速");
		case VideoQuality::BALANCED:
			return QCoreApplication::translate("MediaSettings", "均衡");
		case VideoQuality::QUALITY:
			return QCoreApplication::translate("MediaSettings", "质量");
	}
	return QCoreApplication::translate("MediaSettings", "均衡");
}

QString
output_format_to_string(OutputFormat v_format)
{
	switch (v_format)
	{
		case OutputFormat::ORIGINAL:
			return QCoreApplication::translate("MediaSettings", "原格式");
		case OutputFormat::JPG:
			return QStringLiteral("jpg");
		case OutputFormat::PNG:
			return QStringLiteral("png");
		case OutputFormat::WEB_P:
			return QStringLiteral("webp");
		case OutputFormat::MP4:
			return QStringLiteral("mp4");
		case OutputFormat::MKV:
			return QStringLiteral("mkv");
		case OutputFormat::AVI:
			return QStringLiteral("avi");
		case OutputFormat::MOV:
			return QStringLiteral("mov");
		case OutputFormat::WEB_M:
			return QStringLiteral("webm");
		case OutputFormat::GIF:
			return QStringLiteral("gif");
	}
	return QStringLiteral("原格式");
}

QString
gpu_device_to_string(GPUDevice v_device)
{
	switch (v_device)
	{
		case GPUDevice::AUTO:
			return QCoreApplication::translate("MediaSettings", "自动");
		case GPUDevice::GPU0:
			return QStringLiteral("0");
		case GPUDevice::GPU1:
			return QStringLiteral("1");
		case GPUDevice::GPU2:
			return QStringLiteral("2");
	}
	return QStringLiteral("自动");
}

// ---------------------------------------------------------------------------
// 字符串 -> 枚举
// ---------------------------------------------------------------------------
ImageType
string_to_image_type(const QString &v_str)
{
	if (v_str == QStringLiteral("真实图片"))
		return ImageType::REAL_IMAGE;
	if (v_str == QStringLiteral("动漫图片"))
		return ImageType::ANIME_IMAGE;
	return ImageType::REAL_IMAGE;
}

ModelType
string_to_model_type(const QString &v_str)
{
	if (v_str == QStringLiteral("realesrgan-x4plus"))
		return ModelType::REAL_ESRGAN_X4PLUS;
	if (v_str == QStringLiteral("realesrgan-x4plus-anime"))
		return ModelType::REAL_ESRGAN_X4PLUS_ANIME;
	if (v_str == QStringLiteral("realesr-animevideov3"))
		return ModelType::REAL_ESR_ANIMEVIDEOV3;
	return ModelType::REAL_ESRGAN_X4PLUS;
}

ScaleFactor
string_to_scale_factor(const QString &v_str)
{
	if (v_str == QStringLiteral("2倍"))
		return ScaleFactor::SCALE2X;
	if (v_str == QStringLiteral("3倍"))
		return ScaleFactor::SCALE3X;
	if (v_str == QStringLiteral("4倍"))
		return ScaleFactor::SCALE4X;
	return ScaleFactor::SCALE4X;
}

ProcessMode
string_to_process_mode(const QString &v_str)
{
	if (v_str == QStringLiteral("单文件处理"))
		return ProcessMode::SINGLE_FILE;
	if (v_str == QStringLiteral("批量处理"))
		return ProcessMode::BATCH_PROCESS;
	return ProcessMode::SINGLE_FILE;
}

VideoEncoding
string_to_video_encoding(const QString &v_str)
{
	if (v_str == QStringLiteral("H.264"))
		return VideoEncoding::H264;
	if (v_str == QStringLiteral("H.265"))
		return VideoEncoding::H265;
	if (v_str == QStringLiteral("VP9"))
		return VideoEncoding::VP9;
	if (v_str == QStringLiteral("AV1"))
		return VideoEncoding::AV1;
	return VideoEncoding::H264;
}

VideoQuality
string_to_video_quality(const QString &v_str)
{
	if (v_str == QStringLiteral("快速"))
		return VideoQuality::FAST;
	if (v_str == QStringLiteral("均衡"))
		return VideoQuality::BALANCED;
	if (v_str == QStringLiteral("质量"))
		return VideoQuality::QUALITY;
	return VideoQuality::BALANCED;
}

OutputFormat
string_to_output_format(const QString &v_str)
{
	if (v_str == QStringLiteral("原格式"))
		return OutputFormat::ORIGINAL;
	if (v_str == QStringLiteral("jpg"))
		return OutputFormat::JPG;
	if (v_str == QStringLiteral("png"))
		return OutputFormat::PNG;
	if (v_str == QStringLiteral("webp"))
		return OutputFormat::WEB_P;
	if (v_str == QStringLiteral("mp4"))
		return OutputFormat::MP4;
	if (v_str == QStringLiteral("mkv"))
		return OutputFormat::MKV;
	if (v_str == QStringLiteral("avi"))
		return OutputFormat::AVI;
	if (v_str == QStringLiteral("mov"))
		return OutputFormat::MOV;
	if (v_str == QStringLiteral("webm"))
		return OutputFormat::WEB_M;
	if (v_str == QStringLiteral("gif"))
		return OutputFormat::GIF;
	return OutputFormat::ORIGINAL;
}

GPUDevice
string_to_gpu_device(const QString &v_str)
{
	if (v_str == QStringLiteral("自动"))
		return GPUDevice::AUTO;
	if (v_str == QStringLiteral("0"))
		return GPUDevice::GPU0;
	if (v_str == QStringLiteral("1"))
		return GPUDevice::GPU1;
	if (v_str == QStringLiteral("2"))
		return GPUDevice::GPU2;
	return GPUDevice::AUTO;
}

// ---------------------------------------------------------------------------
// 领域辅助函数
// ---------------------------------------------------------------------------
QString
model_short_name(ModelType v_type)
{
	switch (v_type)
	{
		case ModelType::REAL_ESRGAN_X4PLUS:
			return QStringLiteral("real");
		case ModelType::REAL_ESRGAN_X4PLUS_ANIME:
			return QStringLiteral("anime");
		case ModelType::REAL_ESR_ANIMEVIDEOV3:
			return QStringLiteral("anime_video");
	}
	return QStringLiteral("real");
}

QString
model_file_name(ModelType v_type)
{
	switch (v_type)
	{
		case ModelType::REAL_ESRGAN_X4PLUS:
			return QStringLiteral("realesrgan-x4plus");
		case ModelType::REAL_ESRGAN_X4PLUS_ANIME:
			return QStringLiteral("realesrgan-x4plus-anime");
		case ModelType::REAL_ESR_ANIMEVIDEOV3:
			return QStringLiteral("realesr-animevideov3");
	}
	return QStringLiteral("realesrgan-x4plus");
}

int
model_default_scale(ModelType v_type)
{
	// 动漫视频模型（realesr-animevideov3）默认 2 倍，其余默认 4 倍
	return v_type == ModelType::REAL_ESR_ANIMEVIDEOV3 ? 2 : 4;
}

QString
output_format_extension(OutputFormat v_format)
{
	switch (v_format)
	{
		case OutputFormat::JPG:
			return QStringLiteral("jpg");
		case OutputFormat::PNG:
			return QStringLiteral("png");
		case OutputFormat::WEB_P:
			return QStringLiteral("webp");
		case OutputFormat::MP4:
			return QStringLiteral("mp4");
		case OutputFormat::MKV:
			return QStringLiteral("mkv");
		case OutputFormat::AVI:
			return QStringLiteral("avi");
		case OutputFormat::MOV:
			return QStringLiteral("mov");
		case OutputFormat::WEB_M:
			return QStringLiteral("webm");
		case OutputFormat::GIF:
			return QStringLiteral("gif");
		case OutputFormat::ORIGINAL:
			return QString();
	}
	return QString();
}

VideoEncodeParams
video_encode_params(VideoEncoding v_encoding, VideoQuality v_quality)
{
	const bool software_codec = (v_encoding == VideoEncoding::H264
								 || v_encoding == VideoEncoding::H265);

	switch (v_quality)
	{
		case VideoQuality::FAST:
			// 快速：优先速度，质量稍低
			return software_codec
					   ? VideoEncodeParams{ 28, QStringLiteral("ultrafast") }
					   : VideoEncodeParams{ 35, QStringLiteral("fast") };
		case VideoQuality::QUALITY:
			// 质量：优先质量，速度较慢
			return software_codec
					   ? VideoEncodeParams{ 18, QStringLiteral("slow") }
					   : VideoEncodeParams{ 25, QStringLiteral("slow") };
		case VideoQuality::BALANCED:
			break;
	}
	// 均衡（默认）：速度与质量平衡
	return software_codec ? VideoEncodeParams{ 23, QStringLiteral("medium") }
						  : VideoEncodeParams{ 30, QStringLiteral("medium") };
}

bool
is_engine_image_format(const QString &v_extension)
{
	QString extension = v_extension;
	if (extension.startsWith(QLatin1Char('.')))
	{
		extension.remove(0, 1);
	}
	extension = extension.toLower();

	return extension == QStringLiteral("jpg")
		   || extension == QStringLiteral("jpeg")
		   || extension == QStringLiteral("png")
		   || extension == QStringLiteral("webp");
}

QList<OutputFormat>
image_output_format_options()
{
	// 枚举顺序即 UI 下拉顺序（视图用 itemData 承载，不靠文本反解）
	return { OutputFormat::ORIGINAL, OutputFormat::JPG, OutputFormat::PNG,
			 OutputFormat::WEB_P };
}

QList<OutputFormat>
video_output_format_options()
{
	// 阶段二方案 B：GIF 输出已移除（仅保留为输入格式）；枚举顺序即 UI 顺序。
	return { OutputFormat::ORIGINAL, OutputFormat::MP4, OutputFormat::MKV,
			 OutputFormat::AVI,		 OutputFormat::MOV, OutputFormat::WEB_M };
}

QString
tile_size_auto_token()
{
	// 图块大小「自动」的规范值（非翻译显示文本）：UI collect 写入该 token，
	// 领域数据保持中文规范值不变，显示层的翻译由视图负责。
	return QStringLiteral("自动");
}

int
safe_default_tile_size()
{
	// 自动图块的安全默认值：显式 256 而不是让引擎自动选择。引擎在 12GB 级
	// 显存上自动图块可能很大，单次 dispatch 时间不可控，是 TDR 的风险放大
	// 因素（详见 docs/ci.md 与 task-34 加固说明）。
	return 256;
}

int
safe_min_tile_size()
{
	return 32;
}

int
safe_max_tile_size()
{
	return 1024;
}

QString
safe_default_thread_count()
{
	// 单推理线程：避免两个推理线程共享 Vulkan 设备带来的并发压力。
	return QStringLiteral("1:1:2");
}

int
safe_retry_tile_size()
{
	// 引擎崩溃后的降级重试参数：更小图块 + 单线程，仅重试时使用。
	return 128;
}

QString
safe_retry_thread_count()
{
	return QStringLiteral("1:1:1");
}

QStringList
image_output_format_names()
{
	return { output_format_to_string(OutputFormat::ORIGINAL),
			 output_format_to_string(OutputFormat::JPG),
			 output_format_to_string(OutputFormat::PNG),
			 output_format_to_string(OutputFormat::WEB_P) };
}

QStringList
video_output_format_names()
{
	// 视频输出候选（阶段二按产品决策移除 gif）：原生 libav 编码链路没有
	// GIF 编码器，此前 UI 提供 gif 但任务必然失败；GIF 作为输入仍可解码，
	// 默认输出由视频服务映射到 mp4。OutputFormat::GIF 枚举保留用于兼容
	// 旧配置/字符串解析，不再出现在输出格式下拉里。
	return { output_format_to_string(OutputFormat::ORIGINAL),
			 output_format_to_string(OutputFormat::MP4),
			 output_format_to_string(OutputFormat::MKV),
			 output_format_to_string(OutputFormat::AVI),
			 output_format_to_string(OutputFormat::MOV),
			 output_format_to_string(OutputFormat::WEB_M) };
}