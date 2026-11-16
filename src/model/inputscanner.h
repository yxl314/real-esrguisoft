#ifndef INPUTSCANNER_H
#define INPUTSCANNER_H

// ============================================================================
// 模型层 - 输入文件扫描
//
// 集中实现「输入目录 → 待处理文件列表」规则，与旧 Python 编排层逐条对齐：
//   - 只扫描一层（非递归）、按扩展名过滤（大小写不敏感）、按路径字典序排序；
//   - 视频批量额外排除「已处理」文件（文件名后缀形如 _mp4_anime_video[_x]）。
// 纯静态函数、无状态，可独立测试。
// ============================================================================

#include <QString>
#include <QStringList>

#include "mediasettings.h"

class InputScanner
{
  public:
	// 图片扩展名（对应 Python core.IMAGE_EXTENSIONS）
	static const QStringList &
	image_extensions();
	// 视频扩展名（对应 Python core.VIDEO_EXTENSIONS）
	static const QStringList &
	video_extensions();

	// 扫描目录下的图片文件（单层、字典序）
	static QStringList
	image_files(const QString &v_dir_path);

	// 扫描目录下的视频文件（单层、字典序、排除已处理文件）
	static QStringList
	video_files(const QString &v_dir_path, ModelType v_model_type,
				bool v_tta_enabled);

	// 按媒体种类分派
	static QStringList
	files_for_kind(MediaKind v_kind, const QString &v_dir_path,
				   const MediaSettings &v_settings);

	// 判断一个视频文件是否已被同名规则的输出文件覆盖（Python core.GetVideoFiles）
	static bool
	is_processed_video(const QString &v_file_path, ModelType v_model_type,
					   bool v_tta_enabled);
};

#endif // INPUTSCANNER_H
