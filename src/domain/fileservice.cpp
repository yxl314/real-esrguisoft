#include "fileservice.h"

#include "logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

const QStringList FileService::S_IMAGE_EXTENSIONS
	= { QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
		QStringLiteral("webp"), QStringLiteral("bmp") };

const QStringList FileService::S_VIDEO_EXTENSIONS
	= { QStringLiteral("mp4"), QStringLiteral("mkv"),  QStringLiteral("avi"),
		QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("wmv"),
		QStringLiteral("flv"), QStringLiteral("ts"),   QStringLiteral("gif") };

QStringList
FileService::image_filters()
{
	QStringList filters;
	for (const QString &ext : S_IMAGE_EXTENSIONS)
	{
		filters << QStringLiteral("*.%1").arg(ext);
	}
	return filters;
}

QStringList
FileService::video_filters()
{
	QStringList filters;
	for (const QString &ext : S_VIDEO_EXTENSIONS)
	{
		filters << QStringLiteral("*.%1").arg(ext);
	}
	return filters;
}

QStringList
FileService::filters(MediaKind v_kind)
{
	return v_kind == MediaKind::VIDEO ? video_filters() : image_filters();
}

QStringList
FileService::all_media_filters()
{
	return image_filters() + video_filters();
}

bool
FileService::is_image_file(const QString &v_file_path)
{
	return S_IMAGE_EXTENSIONS.contains(file_extension(v_file_path));
}

bool
FileService::is_video_file(const QString &v_file_path)
{
	return S_VIDEO_EXTENSIONS.contains(file_extension(v_file_path));
}

bool
FileService::is_media_file(const QString &v_file_path)
{
	return is_image_file(v_file_path) || is_video_file(v_file_path);
}

bool
FileService::is_file_of_kind(const QString &v_file_path, MediaKind v_kind)
{
	return v_kind == MediaKind::VIDEO ? is_video_file(v_file_path)
									  : is_image_file(v_file_path);
}

QString
FileService::file_extension(const QString &v_file_path)
{
	return QFileInfo(v_file_path).suffix().toLower();
}

QString
FileService::file_name(const QString &v_file_path)
{
	return QFileInfo(v_file_path).fileName();
}

QString
FileService::file_name_without_extension(const QString &v_file_path)
{
	return QFileInfo(v_file_path).completeBaseName();
}

QString
FileService::directory(const QString &v_file_path)
{
	return QFileInfo(v_file_path).absolutePath();
}

bool
FileService::is_valid_input_path(const QString &v_path)
{
	if (v_path.isEmpty())
	{
		return false;
	}
	return QFileInfo::exists(v_path) || QDir(v_path).exists();
}

bool
FileService::is_valid_output_path(const QString &v_path)
{
	if (v_path.isEmpty())
	{
		return false;
	}
	QDir dir(QFileInfo(v_path).absolutePath());
	return dir.exists() || dir.mkpath(QStringLiteral("."));
}

QStringList
FileService::files_in_directory(const QString &v_dir_path,
								const QStringList &v_name_filters)
{
	QStringList files;
	QDir dir(v_dir_path);

	if (!dir.exists())
	{
		LOG_WARNING(
			QStringLiteral("Directory does not exist: %1").arg(v_dir_path));
		return files;
	}

	const QStringList effective_filters
		= v_name_filters.isEmpty() ? all_media_filters() : v_name_filters;

	const QFileInfoList entries
		= dir.entryInfoList(effective_filters, QDir::Files | QDir::Readable);
	for (const QFileInfo &info : entries)
	{
		files << info.absoluteFilePath();
	}
	return files;
}

qint64
FileService::file_size(const QString &v_file_path)
{
	return QFileInfo(v_file_path).size();
}

QString
FileService::format_file_size(qint64 v_bytes)
{
	const qint64 kb = 1024;
	const qint64 mb = kb * 1024;
	const qint64 gb = mb * 1024;

	if (v_bytes >= gb)
	{
		return QStringLiteral("%1 GB").arg(v_bytes / static_cast<double>(gb), 0,
										   'f', 2);
	}
	if (v_bytes >= mb)
	{
		return QStringLiteral("%1 MB").arg(v_bytes / static_cast<double>(mb), 0,
										   'f', 2);
	}
	if (v_bytes >= kb)
	{
		return QStringLiteral("%1 KB").arg(v_bytes / static_cast<double>(kb), 0,
										   'f', 1);
	}
	return QStringLiteral("%1 B").arg(v_bytes);
}

qint64
FileService::directory_total_size(const QString &v_dir_path)
{
	qint64 total_size = 0;
	const QFileInfoList entries
		= QDir(v_dir_path).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
	for (const QFileInfo &info : entries)
	{
		total_size += info.size();
	}
	return total_size;
}

bool
FileService::ensure_directory_exists(const QString &v_dir_path)
{
	QDir dir(v_dir_path);
	if (dir.exists())
	{
		return true;
	}
	if (dir.mkpath(QStringLiteral(".")))
	{
		LOG_INFO(QStringLiteral("Created directory: %1").arg(v_dir_path));
		return true;
	}
	LOG_ERROR(QStringLiteral("Failed to create directory: %1").arg(v_dir_path));
	return false;
}

bool
FileService::delete_file(const QString &v_file_path)
{
	QFile file(v_file_path);
	if (file.remove())
	{
		LOG_INFO(QStringLiteral("Deleted file: %1").arg(v_file_path));
		return true;
	}
	LOG_ERROR(QStringLiteral("Failed to delete file: %1").arg(v_file_path));
	return false;
}

QString
FileService::unique_output_path(const QString &v_base_path)
{
	QString path = v_base_path;
	const QFileInfo info(path);
	const QString dir = info.absolutePath();
	const QString base_name = info.completeBaseName();
	const QString ext = info.suffix();

	int counter = 1;
	while (QFile::exists(path))
	{
		path = QStringLiteral("%1/%2_%3.%4")
				   .arg(dir, base_name)
				   .arg(counter)
				   .arg(ext);
		++counter;
	}
	return path;
}