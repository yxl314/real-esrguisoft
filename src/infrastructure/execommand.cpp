#include "execommand.h"

#include "appsettings.h"
#include "logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace
{

constexpr int K_BIN_SEARCH_MAX_DEPTH = 10;

// 引擎在 bin 目录下的相对布局
const QString K_REALESRGAN_DIR = QStringLiteral("realesrgan");
const QString K_REALESRGAN_EXE = QStringLiteral("realesrgan-ncnn-vulkan.exe");
const QString K_MODELS_DIR = QStringLiteral("models");

// 与 Python core.NormalizePath 一致：去掉成对引号
QString
normalize_path(const QString &v_path)
{
	QString path = v_path;
	if (path.size() >= 2)
	{
		const QChar first = path.at(0);
		const QChar last = path.at(path.size() - 1);
		if ((first == QLatin1Char('"') && last == QLatin1Char('"'))
			|| (first == QLatin1Char('\'') && last == QLatin1Char('\'')))
		{
			path = path.mid(1, path.size() - 2);
		}
	}
	return QDir::cleanPath(path);
}

bool
has_engine(const QString &v_bin_dir)
{
	const QString exe_path = ExeCommand::realesrgan_executable(v_bin_dir);
	return QFileInfo(exe_path).isFile();
}

// 线程配置格式：load:proc:save，三段整数（对应 Real-ESRGAN 的 -j）。
// X-02：旧实现只查正则，接受 0:0:0（引擎可能除零/无工作线程）和超大值
// （线程爆炸，放大 TDR 风险）。这里与 safe 默认值保持一致，限制每段
// [1, K_MAX_THREAD_SEGMENT]；非法值由调用方回退到 safe_default_thread_count()。
constexpr int K_MIN_THREAD_SEGMENT = 1;
constexpr int K_MAX_THREAD_SEGMENT = 16;

bool
is_valid_thread_count(const QString &v_value)
{
	static const QRegularExpression pattern(
		QStringLiteral("^(\\d+):(\\d+):(\\d+)$"));
	const QRegularExpressionMatch match = pattern.match(v_value);
	if (!match.hasMatch())
	{
		return false;
	}
	for (int index = 1; index <= 3; ++index)
	{
		bool ok = false;
		const int segment = match.captured(index).toInt(&ok);
		if (!ok || segment < K_MIN_THREAD_SEGMENT
			|| segment > K_MAX_THREAD_SEGMENT)
		{
			return false;
		}
	}
	return true;
}

// 把用户指定的路径解析为绝对路径；相对路径按程序目录解析。
QString
absolute_user_path(const QString &v_path)
{
	const QString path = normalize_path(v_path);
	if (QDir::isRelativePath(path))
	{
		return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath())
								   .absoluteFilePath(path));
	}
	return path;
}

// 用户指定路径 -> bin 目录；无法识别或找不到引擎时返回空串。
// 支持三种选择：bin 目录、bin/realesrgan 目录、引擎可执行文件本身。
QString
resolve_configured_bin(const QString &v_configured)
{
	if (v_configured.isEmpty())
	{
		return QString();
	}

	const QString path = absolute_user_path(v_configured);
	const QFileInfo info(path);
	QString bin_dir_path;
	if (info.isFile())
	{
		// 直接选中引擎可执行文件：<bin>/realesrgan/<exe>，bin 为上两级
		if (info.fileName().compare(K_REALESRGAN_EXE, Qt::CaseInsensitive) != 0)
		{
			return QString();
		}
		QDir bin_dir(info.absolutePath());
		if (!bin_dir.cdUp())
		{
			return QString();
		}
		bin_dir_path = bin_dir.absolutePath();
	}
	else if (info.isDir())
	{
		// 选中的就是 bin 目录（内含 realesrgan/）
		if (has_engine(path))
		{
			return path;
		}
		// 选中的是 bin/realesrgan 目录（引擎可执行文件在它下面）
		QDir bin_dir(path);
		if (!QFileInfo(bin_dir.filePath(K_REALESRGAN_EXE)).isFile()
			|| !bin_dir.cdUp())
		{
			return QString();
		}
		bin_dir_path = bin_dir.absolutePath();
	}
	else
	{
		return QString();
	}

	// 统一按标准布局校验：<bin>/realesrgan/realesrgan-ncnn-vulkan.exe 必须存在
	bin_dir_path = QDir::cleanPath(bin_dir_path);
	return has_engine(bin_dir_path) ? bin_dir_path : QString();
}

// 自动发现：从程序所在目录（及当前工作目录）向上搜索 bin 目录。
// 判定标准：<候选>/realesrgan/realesrgan-ncnn-vulkan.exe 存在。
QString
auto_discover_bin_directory()
{
	QDir dir(QCoreApplication::applicationDirPath());
	for (int depth = 0; dir.exists() && depth < K_BIN_SEARCH_MAX_DEPTH; ++depth)
	{
		const QString candidate
			= QDir::cleanPath(dir.absoluteFilePath(QStringLiteral("bin")));
		if (has_engine(candidate))
		{
			LOG_INFO(QStringLiteral("Found engine bin directory: %1")
						 .arg(candidate));
			return candidate;
		}
		const QString parent_path = dir.absolutePath();
		if (!dir.cdUp() || dir.absolutePath() == parent_path)
		{
			break;
		}
	}

	// 回退：当前工作目录下的 bin（从源码目录直接运行 / 测试时常见）
	const QString cwd_candidate
		= QDir::cleanPath(QDir::currentPath() + QStringLiteral("/bin"));
	if (has_engine(cwd_candidate))
	{
		LOG_INFO(QStringLiteral("Found engine bin directory from cwd: %1")
					 .arg(cwd_candidate));
		return cwd_candidate;
	}

	LOG_WARNING(
		QStringLiteral("Engine bin directory not found, using fallback: "
					   "%1")
			.arg(cwd_candidate));
	return cwd_candidate;
}

} // namespace

namespace ExeCommand
{

QString
bin_directory_for_path(const QString &v_path)
{
	return resolve_configured_bin(v_path.trimmed());
}

EngineLocation
engine_location()
{
	EngineLocation location;
	location.m_configured_path = AppSettings::engine_path();

	if (!location.m_configured_path.isEmpty())
	{
		location.m_bin_directory
			= resolve_configured_bin(location.m_configured_path);
		if (!location.m_bin_directory.isEmpty())
		{
			location.m_source = EnginePathSource::MANUAL;
			LOG_INFO(QStringLiteral("Using configured engine bin directory: %1")
						 .arg(location.m_bin_directory));
		}
		else
		{
			// 配置无效（路径被移动 / 删除 / 选错）：不静默改写用户设置，
			// 回退自动发现并在设置页如实提示。
			location.m_source = EnginePathSource::MANUAL_INVALID;
			LOG_WARNING(QStringLiteral(
							"Configured engine path is invalid, falling back "
							"to auto discovery: %1")
							.arg(location.m_configured_path));
		}
	}

	if (location.m_bin_directory.isEmpty())
	{
		location.m_bin_directory = auto_discover_bin_directory();
	}

	location.m_executable_path
		= realesrgan_executable(location.m_bin_directory);
	return location;
}

QString
discover_bin_directory()
{
	return engine_location().m_bin_directory;
}

QString
realesrgan_executable(const QString &v_bin_dir)
{
	return QDir::cleanPath(v_bin_dir + QStringLiteral("/") + K_REALESRGAN_DIR
						   + QStringLiteral("/") + K_REALESRGAN_EXE);
}

QString
models_directory(const QString &v_bin_dir)
{
	return QDir::cleanPath(v_bin_dir + QStringLiteral("/") + K_REALESRGAN_DIR
						   + QStringLiteral("/") + K_MODELS_DIR);
}

QStringList
upscale_arguments(const QString &v_input_path, const QString &v_output_path,
				  const QString &v_model_file_name, int v_scale,
				  const QString &v_output_format, GPUDevice v_gpu_device,
				  bool v_tta, const QString &v_tile_size,
				  const QString &v_thread_count, const QString &v_models_dir,
				  bool v_safe_retry)
{
	QStringList args;

	args << QStringLiteral("-i") << normalize_path(v_input_path);
	args << QStringLiteral("-o") << normalize_path(v_output_path);
	args << QStringLiteral("-n") << v_model_file_name;

	// 放大倍数：4 倍是引擎默认值，无需显式传（与 Python 一致）
	if (v_scale > 0 && v_scale != 4)
	{
		args << QStringLiteral("-s") << QString::number(v_scale);
	}

	// 输出格式：auto 时由输出路径扩展名决定，不传 -f
	if (!v_output_format.isEmpty()
		&& v_output_format.compare(QStringLiteral("auto"), Qt::CaseInsensitive)
			   != 0)
	{
		args << QStringLiteral("-f") << v_output_format.toLower();
	}

	switch (v_gpu_device)
	{
		case GPUDevice::GPU0:
			args << QStringLiteral("-g") << QStringLiteral("0");
			break;
		case GPUDevice::GPU1:
			args << QStringLiteral("-g") << QStringLiteral("1");
			break;
		case GPUDevice::GPU2:
			args << QStringLiteral("-g") << QStringLiteral("2");
			break;
		case GPUDevice::AUTO:
			break;
	}

	if (v_tta)
	{
		args << QStringLiteral("-x");
	}

	// 图块大小：task-34 TDR 加固——总是显式传 -t，不交给引擎自动选择。
	// 自动 / 0 / 非法值 → 安全默认 256；显式正整数值 clamp 到 [32, 1024]；
	// 崩溃降级重试固定使用更保守的 128（仅重试时，见 v_safe_retry）。
	int tile = safe_default_tile_size();
	if (v_safe_retry)
	{
		tile = safe_retry_tile_size();
	}
	else
	{
		const QString tile_text = v_tile_size.trimmed();
		bool tile_ok = false;
		const int parsed = tile_text.toInt(&tile_ok);
		if (tile_ok && parsed > 0 && tile_text != tile_size_auto_token())
		{
			tile = parsed;
			if (tile < safe_min_tile_size())
			{
				LOG_WARNING(QStringLiteral("Tile size %1 below minimum, "
										   "clamped to %2")
								.arg(tile)
								.arg(safe_min_tile_size()));
				tile = safe_min_tile_size();
			}
			else if (tile > safe_max_tile_size())
			{
				LOG_WARNING(QStringLiteral("Tile size %1 above maximum, "
										   "clamped to %2")
								.arg(tile)
								.arg(safe_max_tile_size()));
				tile = safe_max_tile_size();
			}
		}
	}
	args << QStringLiteral("-t") << QString::number(tile);

	// 线程配置：task-34 TDR 加固——总是显式传 -j；空 / 非法值回退安全默认
	// 1:1:2（而不是省略后使用引擎默认的 1:2:2）；崩溃降级重试固定 1:1:1。
	QString thread_count = v_thread_count.trimmed();
	if (v_safe_retry)
	{
		thread_count = safe_retry_thread_count();
	}
	else if (!is_valid_thread_count(thread_count))
	{
		if (!thread_count.isEmpty())
		{
			LOG_WARNING(QStringLiteral("Invalid thread count (expected "
									   "load:proc:save), fallback to %1")
							.arg(safe_default_thread_count()));
		}
		thread_count = safe_default_thread_count();
	}
	args << QStringLiteral("-j") << thread_count;

	// 显式锁定模型目录（旧 Python 依赖引擎相对 exe 目录解析 models/）
	if (!v_models_dir.isEmpty())
	{
		args << QStringLiteral("-m") << normalize_path(v_models_dir);
	}

	return args;
}

QString
file_dialog_filter(MediaKind v_kind)
{
	if (v_kind == MediaKind::VIDEO)
	{
		return QCoreApplication::translate(
			"ExeCommand", "视频文件 (*.mp4 *.mkv *.avi *.mov *.webm *.wmv "
						  "*.flv *.ts *.gif);;所有文件 (*.*)");
	}
	return QCoreApplication::translate(
		"ExeCommand",
		"图片文件 (*.jpg *.jpeg *.png *.webp *.bmp *.gif);;所有文件 (*.*)");
}

} // namespace ExeCommand
