#include "inputscanner.h"

#include "logger.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace
{

const QStringList K_IMAGE_EXTENSIONS
	= { QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
		QStringLiteral("webp"), QStringLiteral("bmp") };

const QStringList K_VIDEO_EXTENSIONS
	= { QStringLiteral("mp4"), QStringLiteral("mkv"),  QStringLiteral("avi"),
		QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("wmv"),
		QStringLiteral("flv"), QStringLiteral("ts"),   QStringLiteral("gif") };

// 扫描目录下一层文件（不递归），按路径字典序排序（对应 Python sorted()）
QStringList
scan_one_level(const QString &v_dir_path)
{
	QStringList files;
	QDir dir(v_dir_path);
	if (!dir.exists())
	{
		LOG_WARNING(QStringLiteral("Input directory does not exist: %1")
						.arg(v_dir_path));
		return files;
	}

	const QFileInfoList entries
		= dir.entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort);
	for (const QFileInfo &info : entries)
	{
		files << info.absoluteFilePath();
	}

	std::sort(files.begin(), files.end());
	return files;
}

} // namespace

const QStringList &
InputScanner::image_extensions()
{
	return K_IMAGE_EXTENSIONS;
}

const QStringList &
InputScanner::video_extensions()
{
	return K_VIDEO_EXTENSIONS;
}

QStringList
InputScanner::image_files(const QString &v_dir_path)
{
	QStringList files;
	for (const QString &file_path : scan_one_level(v_dir_path))
	{
		if (K_IMAGE_EXTENSIONS.contains(
				QFileInfo(file_path).suffix().toLower()))
		{
			files << file_path;
		}
	}
	return files;
}

bool
InputScanner::is_processed_video(const QString &v_file_path,
								 ModelType v_model_type, bool v_tta_enabled)
{
	const QFileInfo info(v_file_path);
	const QString extension = info.suffix().toLower();
	if (extension.isEmpty())
	{
		return false;
	}

	// Python core.GetVideoFiles: 已处理后缀 = "_{ext}_{model}{_x}"
	QString processed_suffix = QStringLiteral("_") + extension;
	processed_suffix += QStringLiteral("_") + model_short_name(v_model_type);
	if (v_tta_enabled)
	{
		processed_suffix += QStringLiteral("_x");
	}

	return info.completeBaseName().endsWith(processed_suffix);
}

QStringList
InputScanner::video_files(const QString &v_dir_path, ModelType v_model_type,
						  bool v_tta_enabled)
{
	QStringList files;
	for (const QString &file_path : scan_one_level(v_dir_path))
	{
		if (!K_VIDEO_EXTENSIONS.contains(
				QFileInfo(file_path).suffix().toLower()))
		{
			continue;
		}
		// 排除已处理的文件，避免批量重跑时重复放大
		if (is_processed_video(file_path, v_model_type, v_tta_enabled))
		{
			continue;
		}
		files << file_path;
	}
	return files;
}

QStringList
InputScanner::files_for_kind(MediaKind v_kind, const QString &v_dir_path,
							 const MediaSettings &v_settings)
{
	if (v_kind == MediaKind::VIDEO)
	{
		return video_files(v_dir_path, v_settings.m_basic.m_model_type,
						   v_settings.m_advanced.m_tta_mode);
	}
	return image_files(v_dir_path);
}
