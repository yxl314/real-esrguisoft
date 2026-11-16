#include "tooltips.h"

#include <QCoreApplication>

namespace ToolTips
{

QString
image_type_real()
{
	return QCoreApplication::translate("ToolTips",
									   "真实图片模式\n"
									   "适用于：照片、截图、写实风格图片\n"
									   "特点：保留更多细节，处理效果更自然");
}

QString
image_type_anime()
{
	return QCoreApplication::translate("ToolTips",
									   "动漫图片模式\n"
									   "适用于：动画截图、漫画、插画\n"
									   "特点：优化线条和色块，减少伪影");
}

QString
model_real_esrga_nx4plus()
{
	return QCoreApplication::translate("ToolTips",
									   "RealESRGAN x4plus 模型\n"
									   "通用超分辨率模型\n"
									   "放大倍数：4x\n"
									   "适用场景：照片、插画、通用图片");
}

QString
model_real_esrga_nx4plus_anime()
{
	return QCoreApplication::translate("ToolTips",
									   "RealESRGAN x4plus Anime 模型\n"
									   "动漫专用超分辨率模型\n"
									   "放大倍数：4x\n"
									   "适用场景：动画截图、漫画、二次元图片");
}

QString
model_real_esr_anime_video_v3()
{
	return QCoreApplication::translate("ToolTips",
									   "RealESR AnimeVideo V3 模型\n"
									   "动漫视频专用模型\n"
									   "放大倍数：2x,3x,4x\n"
									   "适用场景：动画视频、GIF动图\n"
									   "特点：处理速度更快，适合视频处理");
}

QString
scale_option()
{
	return QCoreApplication::translate("ToolTips", "放大倍数设置\n"
												   "2x：放大2倍\n"
												   "3x：放大3倍\n"
												   "4x：放大4倍（推荐）");
}

QString
tta_mode()
{
	return QCoreApplication::translate("ToolTips",
									   "TTA 模式（测试时增强）\n"
									   "开启后：通过多次处理提升质量\n"
									   "缺点：处理时间增加约8倍\n"
									   "建议：仅在需要最高质量时开启");
}

QString
tile_size()
{
	return QCoreApplication::translate(
		"ToolTips", "图块大小设置\n"
					"自动：安全默认 256（推荐；显式传给引擎，"
					"避免自动图块过大）\n"
					"较小值：显存占用少，速度慢\n"
					"较大值：显存占用多，速度快（范围 32–1024）");
}

QString
gpu_id()
{
	return QCoreApplication::translate("ToolTips", "GPU 设备编号\n"
												   "指定使用的显卡设备\n"
												   "默认：auto（自动选择）\n"
												   "多显卡时可手动指定");
}

QString
output_format()
{
	return QCoreApplication::translate("ToolTips",
									   "输出格式设置\n"
									   "自动：保持原格式\n"
									   "JPG：有损压缩，文件小\n"
									   "PNG：无损压缩，质量高\n"
									   "WebP：新一代格式，兼顾质量和大小");
}

QString
crf_value()
{
	return QCoreApplication::translate("ToolTips",
									   "CRF 值（视频质量）\n"
									   "范围：0-51\n"
									   "0：无损质量，文件最大\n"
									   "18：视觉无损（推荐）\n"
									   "23：默认值，质量与大小平衡\n"
									   "51：最差质量，文件最小");
}

QString
preset_value()
{
	return QCoreApplication::translate("ToolTips", "编码预设\n"
												   "ultrafast：最快，文件大\n"
												   "fast：快速\n"
												   "medium：平衡（推荐）\n"
												   "slow：慢速，质量好\n"
												   "veryslow：最慢，质量最好");
}

QString
stop_button()
{
	return QCoreApplication::translate("ToolTips", "取消处理\n"
												   "终止当前处理任务\n"
												   "已处理的文件将保留");
}

} // namespace ToolTips
