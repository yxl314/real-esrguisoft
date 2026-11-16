#include "outputpathresolver.h"

#include <QDir>
#include <QFileInfo>

namespace
{

// 输出扩展名规则（Python core.DetermineOutputPath:137-146）：
//   显式输出格式 > 输入扩展名 > png；
//   图片的 bmp / gif 引擎无法编码，统一转为 png（修复 Python 的隐式失败 bug）。
QString
effective_output_extension(const QFileInfo &v_input_info,
						   const MediaSettings &v_settings)
{
	QString extension
		= output_format_extension(v_settings.m_advanced.m_output_format)
			  .toLower();
	if (!extension.isEmpty())
	{
		return extension;
	}

	const QString input_ext = v_input_info.suffix().toLower();
	if (input_ext.isEmpty())
	{
		return QStringLiteral("png");
	}
	if (v_settings.m_kind == MediaKind::IMAGE
		&& (input_ext == QStringLiteral("bmp")
			|| input_ext == QStringLiteral("gif")))
	{
		return QStringLiteral("png");
	}
	return input_ext;
}

// 单文件默认文件名：{stem}[_{输入扩展名}]_{model}[_x].{输出扩展名}
QString
default_single_name(const QFileInfo &v_input_info,
					const MediaSettings &v_settings,
					const QString &v_output_ext)
{
	QString name = v_input_info.completeBaseName();
	const QString input_ext = v_input_info.suffix().toLower();
	if (!input_ext.isEmpty())
	{
		name += QStringLiteral("_") + input_ext;
	}
	name += QStringLiteral("_")
			+ model_short_name(v_settings.m_basic.m_model_type);
	if (v_settings.m_advanced.m_tta_mode)
	{
		name += QStringLiteral("_x");
	}
	name += QStringLiteral(".") + v_output_ext;
	return name;
}

bool
ends_with_separator(const QString &v_path)
{
	return v_path.endsWith(QLatin1Char('/'))
		   || v_path.endsWith(QLatin1Char('\\'));
}

// 批量图片产物的扩展名候选（按优先级）：
//   显式输出格式 > 输入扩展名（jpeg/jpg 互相兼容）> png；
//   图片链路的 bmp / gif 输入引擎无法编码，统一视为 png。
QStringList
output_extension_candidates(const QFileInfo &v_input_info,
							const MediaSettings &v_settings)
{
	const QString explicit_ext
		= output_format_extension(v_settings.m_advanced.m_output_format)
			  .toLower();
	if (!explicit_ext.isEmpty())
	{
		if (explicit_ext == QStringLiteral("jpg"))
		{
			return { QStringLiteral("jpg"), QStringLiteral("jpeg") };
		}
		return { explicit_ext };
	}

	QString input_ext = v_input_info.suffix().toLower();
	if (v_settings.m_kind == MediaKind::IMAGE
		&& (input_ext == QStringLiteral("bmp")
			|| input_ext == QStringLiteral("gif")))
	{
		input_ext = QStringLiteral("png");
	}
	if (input_ext.isEmpty())
	{
		input_ext = QStringLiteral("png");
	}
	if (input_ext == QStringLiteral("jpeg"))
	{
		return { QStringLiteral("jpeg"), QStringLiteral("jpg") };
	}
	if (input_ext == QStringLiteral("jpg"))
	{
		return { QStringLiteral("jpg"), QStringLiteral("jpeg") };
	}
	return { input_ext };
}

bool
file_updated_since_snapshot(
	const QString &v_path,
	const OutputPathResolver::OutputSnapshotMap &v_before)
{
	const QFileInfo info(v_path);
	if (!info.isFile() || info.size() <= 0)
	{
		return false;
	}
	const auto it
		= v_before.constFind(QDir::cleanPath(info.absoluteFilePath()));
	if (it == v_before.constEnd())
	{
		return true; // 快照中没有的路径 = 本次任务新增
	}
	return info.size() != it->m_size
		   || info.lastModified() != it->m_last_modified;
}

} // namespace

QString
OutputPathResolver::resolve_single_output(const QString &v_input_file,
										  const MediaSettings &v_settings,
										  const QString &v_user_output_path)
{
	const QFileInfo input_info(v_input_file);
	const QString output_ext
		= effective_output_extension(input_info, v_settings);
	const QString default_name
		= default_single_name(input_info, v_settings, output_ext);

	// 未指定输出：写到输入文件所在目录（Python core.py:152-153）
	if (v_user_output_path.isEmpty())
	{
		return QDir(input_info.absolutePath()).filePath(default_name);
	}

	const QString cleaned_path = QDir::cleanPath(v_user_output_path);
	const QFileInfo user_info(cleaned_path);

	// 判定顺序（阶段二修复）：已存在的目录 → 尾分隔符 → 有扩展名视为文件 →
	// 其余视为目录。旧实现先看后缀，导致「目录名带点」（如 out.v1 / 2024.11）
	// 被误判成完整文件路径，输出直接失败。
	if (user_info.exists() && user_info.isDir())
	{
		return QDir(cleaned_path).filePath(default_name);
	}

	// 以分隔符结尾（可能尚未创建）：视为目录
	if (ends_with_separator(v_user_output_path))
	{
		return QDir(cleaned_path).filePath(default_name);
	}

	// 带扩展名（路径不存在）：视为完整文件路径（另存为场景）
	if (!user_info.suffix().isEmpty())
	{
		const QString user_ext = user_info.suffix().toLower();
		const bool explicit_format
			= !output_format_extension(v_settings.m_advanced.m_output_format)
				   .isEmpty();
		if (v_settings.m_kind == MediaKind::IMAGE && !explicit_format
			&& (user_ext == QStringLiteral("bmp")
				|| user_ext == QStringLiteral("gif")))
		{
			return QDir(user_info.absolutePath())
				.filePath(user_info.completeBaseName()
						  + QStringLiteral(".png"));
		}
		return cleaned_path;
	}

	// 不存在且无扩展名：视为目录
	return QDir(cleaned_path).filePath(default_name);
}

QString
OutputPathResolver::resolve_batch_output_dir(const QString &v_input_dir,
											 const MediaSettings &v_settings,
											 const QString &v_user_output_dir)
{
	const QFileInfo input_info(QDir::cleanPath(v_input_dir));

	QString dir_name = input_info.fileName();
	if (dir_name.isEmpty())
	{
		dir_name = QDir(QDir::cleanPath(v_input_dir)).dirName();
	}

	QString out_dir_name = dir_name + QStringLiteral("_")
						   + model_short_name(v_settings.m_basic.m_model_type);
	if (v_settings.m_advanced.m_tta_mode)
	{
		out_dir_name += QStringLiteral("_x");
	}

	// 用户指定输出：一律视为父目录（Python core.py:205-216）
	if (!v_user_output_dir.isEmpty())
	{
		return QDir(QDir::cleanPath(v_user_output_dir)).filePath(out_dir_name);
	}

	// 未指定：输入目录的父目录下
	return QDir(input_info.absolutePath()).filePath(out_dir_name);
}

QStringList
OutputPathResolver::batch_output_file_names(const QString &v_input_file,
											const MediaSettings &v_settings)
{
	const QFileInfo input_info(v_input_file);
	const QStringList extensions
		= output_extension_candidates(input_info, v_settings);

	QStringList names;
	for (const QString &extension : extensions)
	{
		names << input_info.completeBaseName() + QStringLiteral(".")
					 + extension;
	}
	return names;
}

OutputPathResolver::OutputSnapshotMap
OutputPathResolver::snapshot_batch_outputs(const QStringList &v_input_files,
										   const MediaSettings &v_settings,
										   const QString &v_output_dir)
{
	OutputSnapshotMap snapshot;
	const QDir output_dir(v_output_dir);
	for (const QString &input_file : v_input_files)
	{
		for (const QString &name :
			 batch_output_file_names(input_file, v_settings))
		{
			const QFileInfo info(output_dir.filePath(name));
			if (!info.isFile())
			{
				continue;
			}
			OutputFileSnapshot state;
			state.m_size = info.size();
			state.m_last_modified = info.lastModified();
			snapshot.insert(QDir::cleanPath(info.absoluteFilePath()), state);
		}
	}
	return snapshot;
}

QStringList
OutputPathResolver::unresolved_batch_outputs(const QStringList &v_input_files,
											 const MediaSettings &v_settings,
											 const QString &v_output_dir,
											 const OutputSnapshotMap &v_before)
{
	QStringList unresolved;
	const QDir output_dir(v_output_dir);
	for (const QString &input_file : v_input_files)
	{
		bool ready = false;
		for (const QString &name :
			 batch_output_file_names(input_file, v_settings))
		{
			if (file_updated_since_snapshot(output_dir.filePath(name),
											v_before))
			{
				ready = true;
				break;
			}
		}
		if (!ready)
		{
			unresolved << input_file;
		}
	}
	return unresolved;
}
