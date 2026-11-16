#ifndef FILESERVICE_H
#define FILESERVICE_H

// ============================================================================
// 模型层 - 文件系统服务
//
// 与媒体文件相关的纯文件操作：扩展名识别、目录扫描、大小格式化等。
// 全部为静态无状态函数，可独立测试，不依赖任何 UI。
// ============================================================================

#include <QString>
#include <QStringList>

#include "mediasettings.h"

class FileService
{
  public:
	// 指定媒体种类对应的过滤器列表（如 "*.jpg"）
	static QStringList
	filters(MediaKind v_kind);
	static QStringList
	image_filters();
	static QStringList
	video_filters();
	static QStringList
	all_media_filters();

	static bool
	is_image_file(const QString &v_file_path);
	static bool
	is_video_file(const QString &v_file_path);
	static bool
	is_media_file(const QString &v_file_path);
	// 判断文件是否属于指定媒体种类（gif 在图片 / 视频中均视为有效）
	static bool
	is_file_of_kind(const QString &v_file_path, MediaKind v_kind);

	static QString
	file_extension(const QString &v_file_path);
	static QString
	file_name(const QString &v_file_path);
	static QString
	file_name_without_extension(const QString &v_file_path);
	static QString
	directory(const QString &v_file_path);

	static bool
	is_valid_input_path(const QString &v_path);
	static bool
	is_valid_output_path(const QString &v_path);

	// 返回目录下所有匹配过滤器的文件（绝对路径）
	static QStringList
	files_in_directory(const QString &v_dir_path,
					   const QStringList &v_name_filters);

	static qint64
	file_size(const QString &v_file_path);
	static QString
	format_file_size(qint64 v_bytes);

	// 目录总大小（直接子文件之和）
	static qint64
	directory_total_size(const QString &v_dir_path);

	static bool
	ensure_directory_exists(const QString &v_dir_path);
	static bool
	delete_file(const QString &v_file_path);

	static QString
	unique_output_path(const QString &v_base_path);

  private:
	static const QStringList S_IMAGE_EXTENSIONS;
	static const QStringList S_VIDEO_EXTENSIONS;
};

#endif // FILESERVICE_H