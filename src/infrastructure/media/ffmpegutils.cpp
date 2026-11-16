#include "ffmpegutils.h"

#include <QCoreApplication>

#include <mutex>

namespace
{

std::once_flag g_init_flag;

} // namespace

namespace MediaFfmpeg
{

void
ensure_initialized()
{
	std::call_once(g_init_flag,
				   []()
				   {
					   // 库使用者自己处理错误，不让 FFmpeg 往 stderr 刷屏；
					   // 仍然保留 AV_LOG_ERROR 级别的信息便于排查
					   av_log_set_level(AV_LOG_ERROR);
				   });
}

QString
error_text(int v_error_code)
{
	char buffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
	const int written = av_strerror(v_error_code, buffer, sizeof(buffer));
	if (written < 0)
	{
		return QCoreApplication::translate("MediaFfmpeg", "未知错误（%1）")
			.arg(v_error_code);
	}
	return QString::fromUtf8(buffer);
}

QString
error_text(const QString &v_operation, int v_error_code)
{
	return QCoreApplication::translate("MediaFfmpeg", "%1：%2")
		.arg(v_operation, error_text(v_error_code));
}

double
rational_to_double(AVRational v_rational)
{
	if (v_rational.num == 0 || v_rational.den == 0)
	{
		return 0.0;
	}
	return av_q2d(v_rational);
}

AVRational
stream_frame_rate(const AVStream *v_stream)
{
	if (v_stream == nullptr)
	{
		return AVRational{ 0, 1 };
	}
	AVRational frame_rate = v_stream->avg_frame_rate;
	if (frame_rate.num <= 0 || frame_rate.den <= 0)
	{
		frame_rate = v_stream->r_frame_rate;
	}
	if (frame_rate.num <= 0 || frame_rate.den <= 0)
	{
		return AVRational{ 0, 1 };
	}
	return frame_rate;
}

AVRational
double_to_frame_rate(double v_frame_rate)
{
	if (v_frame_rate <= 0.0)
	{
		return AVRational{ 0, 1 };
	}
	AVRational frame_rate = av_d2q(v_frame_rate, 1000000);
	if (frame_rate.num <= 0 || frame_rate.den <= 0)
	{
		return AVRational{ 0, 1 };
	}
	return frame_rate;
}

QString
frame_rate_label(AVRational v_frame_rate)
{
	const double frame_rate = rational_to_double(v_frame_rate);
	if (frame_rate <= 0.0)
	{
		return QCoreApplication::translate("MediaFfmpeg", "未知");
	}
	return QString::number(frame_rate, 'f', 3);
}

QString
color_space_name(AVColorSpace v_color_space)
{
	switch (v_color_space)
	{
		case AVCOL_SPC_BT709:
			return QStringLiteral("bt709");
		case AVCOL_SPC_BT470BG:
		case AVCOL_SPC_SMPTE170M:
			return QStringLiteral("bt601");
		case AVCOL_SPC_BT2020_NCL:
		case AVCOL_SPC_BT2020_CL:
			return QStringLiteral("bt2020");
		case AVCOL_SPC_UNSPECIFIED:
			return QStringLiteral("unknown");
		default:
			break;
	}
	return QStringLiteral("unknown");
}

int
sws_matrix(AVColorSpace v_color_space, int v_height)
{
	switch (v_color_space)
	{
		case AVCOL_SPC_BT709:
			return SWS_CS_ITU709;
		case AVCOL_SPC_BT470BG:
		case AVCOL_SPC_SMPTE170M:
			return SWS_CS_ITU601;
		case AVCOL_SPC_BT2020_NCL:
		case AVCOL_SPC_BT2020_CL:
			return SWS_CS_BT2020;
		default:
			break;
	}
	// 未标注：HD 画面按 BT.709，SD 按 BT.601
	return (v_height >= 720) ? SWS_CS_ITU709 : SWS_CS_ITU601;
}

void
configure_yuv_to_rgba(SwsContext *v_sws, AVColorSpace v_color_space,
					  AVColorRange v_color_range, int v_height)
{
	if (v_sws == nullptr)
	{
		return;
	}

	int *inv_table = nullptr;
	int *table = nullptr;
	int src_range = 0;
	int dst_range = 0;
	int brightness = 0;
	int contrast = 0;
	int saturation = 0;
	sws_getColorspaceDetails(v_sws, &inv_table, &src_range, &table, &dst_range,
							 &brightness, &contrast, &saturation);

	// 源 YUV：矩阵按流标注（未标注按分辨率猜），范围按流标注（默认有限）
	inv_table = const_cast<int *>(
		sws_getCoefficients(sws_matrix(v_color_space, v_height)));
	src_range = (v_color_range == AVCOL_RANGE_JPEG) ? 1 : 0;
	// 目标 RGBA：全范围
	dst_range = 1;
	brightness = 0;
	contrast = 1 << 16;
	saturation = 1 << 16;

	sws_setColorspaceDetails(v_sws, inv_table, src_range, table, dst_range,
							 brightness, contrast, saturation);
}

void
configure_rgba_to_yuv(SwsContext *v_sws, AVColorSpace v_color_space,
					  int v_height)
{
	if (v_sws == nullptr)
	{
		return;
	}

	int *inv_table = nullptr;
	int *table = nullptr;
	int src_range = 0;
	int dst_range = 0;
	int brightness = 0;
	int contrast = 0;
	int saturation = 0;
	sws_getColorspaceDetails(v_sws, &inv_table, &src_range, &table, &dst_range,
							 &brightness, &contrast, &saturation);

	// 源 RGBA：sRGB 全范围；目标 YUV：有限范围 + 与输出流一致的矩阵
	table = const_cast<int *>(
		sws_getCoefficients(sws_matrix(v_color_space, v_height)));
	src_range = 1;
	dst_range = 0;
	brightness = 0;
	contrast = 1 << 16;
	saturation = 1 << 16;

	sws_setColorspaceDetails(v_sws, inv_table, src_range, table, dst_range,
							 brightness, contrast, saturation);
}

AVColorSpace
target_color_space(int v_height)
{
	return (v_height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
}

} // namespace MediaFfmpeg
