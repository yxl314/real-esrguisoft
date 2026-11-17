#include "imageprocessservice.h"

#include "batchprogresswatcher.h"
#include "execommand.h"
#include "exerunner.h"
#include "inputscanner.h"
#include "logger.h"
#include "outputpathresolver.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>

namespace
{

int
scale_multiplier(ScaleFactor v_factor)
{
	switch (v_factor)
	{
		case ScaleFactor::SCALE2X:
			return 2;
		case ScaleFactor::SCALE3X:
			return 3;
		case ScaleFactor::SCALE4X:
			return 4;
	}
	return 4;
}

// 输出路径扩展名 → 引擎 -f 参数（jpeg 归一到 jpg；空串表示引擎不支持）
QString
engine_format_for_output(const QString &v_output_path)
{
	const QString extension = QFileInfo(v_output_path).suffix().toLower();
	if (extension == QStringLiteral("jpg")
		|| extension == QStringLiteral("jpeg"))
	{
		return QStringLiteral("jpg");
	}
	if (extension == QStringLiteral("png"))
	{
		return QStringLiteral("png");
	}
	if (extension == QStringLiteral("webp"))
	{
		return QStringLiteral("webp");
	}
	return QString();
}

// 产物是否有效（存在且非空）——引擎失败时退出码仍可能是 0，必须看产物
bool
file_ready(const QString &v_file_path)
{
	const QFileInfo info(v_file_path);
	return info.isFile() && info.size() > 0;
}

// 输出图片的编码质量（jpg / webp，0-100）。
// realesrgan-ncnn-vulkan 没有质量参数：webp 输出固定为 VP8L 无损（体积大），
// jpg 在输入含 alpha 时还会被改写成 "<name>.jpg.png"。因此产物统一在这里
// 按目标格式以 95 质量重编码一次，保证扩展名、内容、体积三者一致。
constexpr int K_OUTPUT_IMAGE_QUALITY = 95;

// 目标路径扩展名 → Qt 编码器名（不支持的扩展名返回空）
QByteArray
output_image_format(const QString &v_path)
{
	const QString suffix = QFileInfo(v_path).suffix().toLower();
	if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
	{
		return QByteArrayLiteral("jpg");
	}
	if (suffix == QStringLiteral("png"))
	{
		return QByteArrayLiteral("png");
	}
	if (suffix == QStringLiteral("webp"))
	{
		return QByteArrayLiteral("webp");
	}
	return QByteArray();
}

// 先写临时文件再替换，避免编码失败时把已有产物写坏。
bool
encode_image_to(const QImage &v_image, const QString &v_path,
				const QByteArray &v_format)
{
	const QString temp_path = v_path + QStringLiteral(".resr-enc");
	bool encoded = false;
	{
		// QImageWriter 在作用域内持有临时文件句柄：Windows 上文件仍打开时
		// rename 会失败，先让 writer 析构关闭句柄，再替换目标文件。
		QImageWriter writer(temp_path, v_format);
		writer.setQuality(K_OUTPUT_IMAGE_QUALITY);
		encoded = writer.write(v_image);
	}
	if (!encoded)
	{
		QFile::remove(temp_path);
		return false;
	}
	QFile::remove(v_path);
	if (!QFile::rename(temp_path, v_path))
	{
		QFile::remove(temp_path);
		return false;
	}
	return true;
}

// 把引擎的失败标记翻译成用户可读文案
QString
describe_engine_failure(const QStringList &v_failures, int v_exit_code,
						bool v_process_ok)
{
	for (const QString &failure : v_failures)
	{
		if (failure.contains(QStringLiteral("decode image"),
							 Qt::CaseInsensitive))
		{
			return QCoreApplication::translate(
				"ImageProcessService",
				"引擎无法解码输入图片（文件损坏或格式不受支持）");
		}
		if (failure.contains(QStringLiteral("encode image"),
							 Qt::CaseInsensitive))
		{
			return QCoreApplication::translate(
				"ImageProcessService",
				"引擎无法编码输出图片（检查输出格式与目录写权限）");
		}
		if (failure.contains(QStringLiteral("_wfopen"), Qt::CaseInsensitive))
		{
			return QCoreApplication::translate(
				"ImageProcessService",
				"模型文件缺失（bin/realesrgan/models 目录不完整）");
		}
	}

	if (!v_failures.isEmpty())
	{
		return QCoreApplication::translate("ImageProcessService",
										   "引擎报告失败: %1")
			.arg(v_failures.first());
	}
	if (!v_process_ok)
	{
		return QCoreApplication::translate("ImageProcessService",
										   "引擎异常退出（退出码 %1）")
			.arg(v_exit_code);
	}
	return QString();
}

// 引擎崩溃退出码（Windows 0xC0000409 / STATUS_STACK_BUFFER_OVERRUN 的 int 值）。
bool
is_engine_crash_exit(int v_exit_code)
{
	static constexpr int k_stack_buffer_overrun = -1073740791; // 0xC0000409
	return v_exit_code == k_stack_buffer_overrun;
}

} // namespace

ImageProcessService::ImageProcessService(QObject *v_parent)
	: MediaProcessService(v_parent), m_runner(new ExeRunner(this)),
	  m_watcher(new BatchProgressWatcher(this))
{
	m_bin_directory = ExeCommand::discover_bin_directory();

	connect(m_runner, &ExeRunner::progress_changed, this,
			&ImageProcessService::on_runner_progress_changed);
	connect(m_runner, &ExeRunner::state_changed, this,
			&ImageProcessService::on_runner_state_changed);
	connect(m_runner, &ExeRunner::finished, this,
			&ImageProcessService::on_runner_finished);
	connect(m_watcher, &BatchProgressWatcher::processed_count_changed, this,
			&ImageProcessService::on_watcher_count_changed);
}

ImageProcessService::~ImageProcessService()
{
	cleanup_temp();
}

void
ImageProcessService::set_bin_directory(const QString &v_dir)
{
	m_bin_directory = v_dir;
}

ProcessState
ImageProcessService::state() const
{
	return m_state;
}

bool
ImageProcessService::is_active() const
{
	return m_state == ProcessState::RUNNING || m_state == ProcessState::PAUSED;
}

int
ImageProcessService::progress() const
{
	return m_progress;
}

QString
ImageProcessService::current_file() const
{
	return m_current_file;
}

QString
ImageProcessService::output_path() const
{
	return m_output_path;
}

bool
ImageProcessService::start(const ProcessRequest &v_request)
{
	if (is_active())
	{
		LOG_WARNING("Image processing task is already active");
		return false;
	}

	reset_job();
	m_request = v_request;
	m_request.m_settings.m_kind = MediaKind::IMAGE;
	m_request.m_settings.m_process_mode = v_request.m_mode;

	if (m_request.m_input_path.isEmpty())
	{
		fail_job(tr("没有指定输入文件或目录"));
		return false;
	}

	if (m_request.m_mode == ProcessMode::BATCH_PROCESS)
	{
		begin_batch();
	}
	else
	{
		begin_single();
	}

	// 输入校验 / 创建输出目录等可能同步失败（已 emit state_changed(FAILED) +
	// finished）：此时必须返回 false，不能让调用方以为任务已启动。
	return m_state == ProcessState::RUNNING;
}

void
ImageProcessService::pause()
{
	if (m_state == ProcessState::RUNNING)
	{
		m_runner->pause();
	}
}

void
ImageProcessService::resume()
{
	if (m_state == ProcessState::PAUSED)
	{
		m_runner->resume();
	}
}

void
ImageProcessService::stop()
{
	m_cancelled = true;
	m_runner->stop();
	m_watcher->stop();
	discard_staged_output();
	cleanup_temp();
	m_job_mode = JobMode::NONE;
	m_state = ProcessState::IDLE;
	emit state_changed(m_state);
	LOG_INFO("Image processing stopped by user");
}

void
ImageProcessService::reset_job()
{
	m_runner->stop();
	m_watcher->stop();
	cleanup_temp();

	m_job_mode = JobMode::NONE;
	m_state = ProcessState::IDLE;
	m_progress = 0;
	m_total_files = 0;
	m_processed_files = 0;
	m_batch_index = 0;
	m_batch_success_count = 0;
	m_current_file.clear();
	m_output_path.clear();
	m_current_output_file.clear();
	m_pending_files.clear();
	m_needs_convert.clear();
	m_batch_failed_files.clear();
	m_batch_failed_details.clear();
	m_output_snapshot.clear();
	discard_staged_output();
	m_engine_retry_used = false;
	m_engine_program.clear();
	m_engine_input.clear();
	m_engine_output.clear();
	m_engine_format.clear();
	m_cancelled = false;
}

void
ImageProcessService::begin_single()
{
	m_job_mode = JobMode::SINGLE;

	const QFileInfo input_info(m_request.m_input_path);
	if (!input_info.isFile())
	{
		fail_job(tr("输入文件不存在: %1").arg(m_request.m_input_path));
		return;
	}

	m_output_path = OutputPathResolver::resolve_single_output(
		m_request.m_input_path, m_request.m_settings, m_request.m_output_path);

	const QString output_dir = QFileInfo(m_output_path).absolutePath();
	if (!QDir().mkpath(output_dir))
	{
		fail_job(tr("无法创建输出目录: %1").arg(output_dir));
		return;
	}

	const QString format = engine_format_for_output(m_output_path);
	if (format.isEmpty())
	{
		fail_job(tr("不支持的图片输出格式: %1")
					 .arg(QFileInfo(m_output_path).suffix()));
		return;
	}

	const QString engine_input = engine_input_for(m_request.m_input_path);
	if (engine_input.isEmpty())
	{
		fail_job(tr("无法读取输入图片（已尝试转为临时 PNG）"));
		return;
	}

	m_state = ProcessState::RUNNING;
	m_total_files = 1;
	m_processed_files = 0;
	m_progress = 0;
	m_current_file = input_info.fileName();

	// 引擎先写同目录临时产物，成功后才原子改名到最终路径：失败/取消不会
	// 破坏已存在的旧成品（历史实现引擎直接写最终路径，失败即截断）。
	m_staged_output_path = staged_output_path(m_output_path);
	QFile::remove(m_staged_output_path);

	emit state_changed(m_state);
	emit current_file_changed(m_current_file);
	emit file_counts_changed(0, 1);
	publish_progress(0);

	start_engine(engine_input, m_staged_output_path, format);
}

void
ImageProcessService::begin_batch()
{
	if (!QDir(m_request.m_input_path).exists())
	{
		fail_job(tr("输入路径不是文件夹: %1").arg(m_request.m_input_path));
		return;
	}

	m_pending_files = InputScanner::image_files(m_request.m_input_path);
	// 输入文件数在 UI/进度语义里是 int；扫描结果远超 int 上限前不可能进入
	// 实际处理，这里显式收窄并保持既有行为。
	m_total_files = static_cast<int>(m_pending_files.size());
	if (m_total_files == 0)
	{
		fail_job(tr("输入目录中没有找到图片文件"));
		return;
	}

	m_output_path = OutputPathResolver::resolve_batch_output_dir(
		m_request.m_input_path, m_request.m_settings, m_request.m_output_path);
	if (!QDir().mkpath(m_output_path))
	{
		fail_job(tr("无法创建输出目录: %1").arg(m_output_path));
		return;
	}

	// 引擎无法解码 bmp / gif 等格式：这类批次退回「逐文件 + QImage 预转码」
	m_needs_convert.clear();
	for (const QString &file_path : m_pending_files)
	{
		if (!is_engine_image_format(QFileInfo(file_path).suffix()))
		{
			m_needs_convert << file_path;
		}
	}

	m_state = ProcessState::RUNNING;
	m_processed_files = 0;
	m_batch_index = 0;
	m_progress = 0;
	m_current_file = tr("%1 个文件 · %2")
						 .arg(m_total_files)
						 .arg(QFileInfo(m_request.m_input_path).fileName());

	emit state_changed(m_state);
	emit current_file_changed(m_current_file);
	emit file_counts_changed(0, m_total_files);
	publish_progress(0);

	m_batch_success_count = 0;
	m_batch_failed_files.clear();
	m_batch_failed_details.clear();
	m_output_snapshot.clear();

	if (m_needs_convert.isEmpty())
	{
		m_job_mode = JobMode::BATCH_DIRECTORY;
		// 同 stem 不同扩展名（如 a.jpg + a.png）在批量命名规则下会映射到同一
		// 产物名；目录模式后写覆盖先写且逐输入校验都会"成功"，必须启动前拒绝。
		const QString collision
			= batch_output_collision_error(batch_expected_settings());
		if (!collision.isEmpty())
		{
			fail_job(collision);
			return;
		}
		// 引擎目录模式：显式指定输出格式时按 -f 产出；「原格式」未传 -f 时
		// 引擎统一输出 PNG（不保留 jpg/webp 输入扩展名）。期望产物必须按
		// 引擎实际格式计算，否则 Windows 真机上「原格式」批量会被误判失败。
		const MediaSettings expected_settings = batch_expected_settings();
		const QString format
			= output_format_extension(
				  m_request.m_settings.m_advanced.m_output_format)
				  .toLower();
		// 任务开始前给「期望产物路径」拍快照：输出目录里可能有上一次运行的
		// 同名/旧格式残留，成功判定必须只认本次确实被写入的产物。
		m_output_snapshot = OutputPathResolver::snapshot_batch_outputs(
			m_pending_files, expected_settings, m_output_path);
		m_watcher->start(m_output_path, m_total_files, MediaKind::IMAGE);

		start_engine(m_request.m_input_path, m_output_path, format);
	}
	else
	{
		LOG_INFO(QStringLiteral("%1 image(s) need QImage pre-conversion "
								"(bmp/gif)")
					 .arg(m_needs_convert.size()));
		m_job_mode = JobMode::BATCH_PER_FILE;
		// 逐文件模式同样可能重名（bmp 预转 png 后与原 png 输入同名，或显式
		// 输出格式统一扩展名）；此时后提交会覆盖前一个产物，必须启动前拒绝。
		const QString collision
			= batch_output_collision_error(m_request.m_settings);
		if (!collision.isEmpty())
		{
			fail_job(collision);
			return;
		}
		begin_next_batch_file();
	}
}

void
ImageProcessService::begin_next_batch_file()
{
	if (m_cancelled)
	{
		return;
	}

	if (m_batch_index >= m_pending_files.size())
	{
		finish_batch_per_file();
		return;
	}

	const QString input_file = m_pending_files.at(m_batch_index);
	const QFileInfo input_info(input_file);

	m_current_file = input_info.fileName();
	m_current_output_file = batch_output_file_path(input_file);
	// 逐文件模式同样防「同名旧产物未更新」被当作成功：先拍当前文件快照
	m_output_snapshot = OutputPathResolver::snapshot_batch_outputs(
		QStringList{ input_file }, m_request.m_settings, m_output_path);

	// 准备阶段失败也计入聚合：记录后继续处理下一个文件，最后统一汇报
	const auto skip_current_file = [this](const QString &v_reason)
	{
		m_batch_failed_files << m_current_file;
		m_batch_failed_details << tr("%1（%2）").arg(m_current_file, v_reason);
		LOG_ERROR(QStringLiteral("Batch file skipped: %1").arg(v_reason));
		++m_batch_index;
		publish_counts(m_batch_index);
		publish_progress(m_total_files > 0 ? m_batch_index * 100 / m_total_files
										   : 0);
		begin_next_batch_file();
	};

	const QString output_dir = QFileInfo(m_current_output_file).absolutePath();
	if (!QDir().mkpath(output_dir))
	{
		skip_current_file(tr("无法创建输出目录: %1").arg(output_dir));
		return;
	}

	const QString format = engine_format_for_output(m_current_output_file);
	if (format.isEmpty())
	{
		skip_current_file(tr("不支持的图片输出格式: %1")
							  .arg(QFileInfo(m_current_output_file).suffix()));
		return;
	}

	const QString engine_input = engine_input_for(input_file);
	if (engine_input.isEmpty())
	{
		skip_current_file(tr("无法读取输入图片: %1").arg(m_current_file));
		return;
	}

	// 逐文件模式同样先写临时产物，成功后改名到目标文件名。
	m_staged_output_path = staged_output_path(m_current_output_file);
	QFile::remove(m_staged_output_path);

	emit current_file_changed(m_current_file);
	start_engine(engine_input, m_staged_output_path, format);
}

void
ImageProcessService::start_engine(const QString &v_input_path,
								  const QString &v_output_path,
								  const QString &v_output_format)
{
	m_engine_program = ExeCommand::realesrgan_executable(m_bin_directory);
	m_engine_input = v_input_path;
	m_engine_output = v_output_path;
	// 引擎只产出无损 PNG：它既没有质量参数（webp 恒为 VP8L 无损），输入带
	// alpha 时还会把 jpg 改名成 "<name>.jpg.png"。目标格式统一由本项目在
	// 收尾阶段转换（见 commit_staged_output / finalize_batch_directory_output）。
	Q_UNUSED(v_output_format);
	m_engine_format = QStringLiteral("png");
	m_engine_retry_used = false;
	start_engine_attempt(false);
}

void
ImageProcessService::start_engine_attempt(bool v_safe_retry)
{
	const QStringList arguments = ExeCommand::upscale_arguments(
		m_engine_input, m_engine_output,
		model_file_name(m_request.m_settings.m_basic.m_model_type),
		scale_multiplier(m_request.m_settings.m_basic.m_scale_factor),
		m_engine_format, m_request.m_settings.m_advanced.m_gpu_device,
		m_request.m_settings.m_advanced.m_tta_mode,
		m_request.m_settings.m_advanced.m_tile_size,
		m_request.m_settings.m_advanced.m_thread_count,
		ExeCommand::models_directory(m_bin_directory), v_safe_retry);

	m_runner->start(m_engine_program, arguments);
}

bool
ImageProcessService::engine_crash_retry_available(bool v_process_ok,
												  int v_exit_code) const
{
	return !v_process_ok && !m_engine_retry_used
		   && is_engine_crash_exit(v_exit_code);
}

QString
ImageProcessService::with_engine_retry_hint(const QString &v_reason) const
{
	if (!m_engine_retry_used)
	{
		return v_reason;
	}
	return tr("引擎崩溃，已用安全参数重试仍失败（%1）").arg(v_reason);
}

QString
ImageProcessService::engine_input_for(const QString &v_file_path)
{
	const QFileInfo info(v_file_path);
	if (is_engine_image_format(info.suffix()))
	{
		return info.absoluteFilePath();
	}

	// bmp / gif 等：QImage 解码（gif 取首帧）后另存为临时 PNG
	const QImage image(v_file_path);
	if (image.isNull())
	{
		LOG_ERROR(QStringLiteral("Failed to decode image with QImage: %1")
					  .arg(v_file_path));
		return QString();
	}

	if (m_temp_dir == nullptr)
	{
		QString temp_base = m_request.m_temp_directory;
		if (temp_base.isEmpty())
		{
			temp_base = QDir::tempPath();
		}
		if (!QDir().mkpath(temp_base))
		{
			LOG_ERROR(QStringLiteral("Failed to create temporary base: %1")
						  .arg(temp_base));
			return QString();
		}
		m_temp_dir = new QTemporaryDir(temp_base
									   + QStringLiteral("/resr_image_XXXXXX"));
		if (!m_temp_dir->isValid())
		{
			LOG_ERROR("Failed to create temporary directory for conversion");
			delete m_temp_dir;
			m_temp_dir = nullptr;
			return QString();
		}
	}

	const QString temp_path = m_temp_dir->filePath(info.completeBaseName()
												   + QStringLiteral(".png"));
	if (!image.save(temp_path, "PNG"))
	{
		LOG_ERROR(
			QStringLiteral("Failed to save temporary PNG: %1").arg(temp_path));
		return QString();
	}
	LOG_INFO(QStringLiteral("Converted %1 to temporary PNG for engine")
				 .arg(v_file_path));
	return temp_path;
}

MediaSettings
ImageProcessService::batch_expected_settings() const
{
	MediaSettings settings = m_request.m_settings;
	if (m_job_mode == JobMode::BATCH_DIRECTORY)
	{
		const QString explicit_format
			= output_format_extension(settings.m_advanced.m_output_format)
				  .toLower();
		if (explicit_format.isEmpty())
		{
			// 引擎目录模式未传 -f 时默认输出 PNG。
			settings.m_advanced.m_output_format = OutputFormat::PNG;
		}
	}
	return settings;
}

QString
ImageProcessService::batch_output_file_path(const QString &v_input_file)
{
	// 与引擎目录模式命名一致：<输入 stem>.<输出格式>；「原格式」沿用各输入
	// 自己的扩展名（bmp/gif 引擎无法编码 → 强制 png），显式格式统一用该格式。
	const QStringList candidates = OutputPathResolver::batch_output_file_names(
		v_input_file, m_request.m_settings);
	if (!candidates.isEmpty())
	{
		return QDir(m_output_path).filePath(candidates.first());
	}

	// 理论上不会走到（helper 至少返回 png），保底避免空输出路径
	const QFileInfo input_info(v_input_file);
	return QDir(m_output_path)
		.filePath(input_info.completeBaseName() + QStringLiteral(".png"));
}

QString
ImageProcessService::batch_output_collision_error(
	const MediaSettings &v_settings) const
{
	// 产物名 = <输入 stem>.<输出扩展名>（不带输入扩展名/模型名，与引擎目录
	// 模式命名一致）；同 stem 不同扩展名输入会落到同一路径。
	QHash<QString, QStringList> owners;
	for (const QString &input_file : m_pending_files)
	{
		const QStringList names = OutputPathResolver::batch_output_file_names(
			input_file, v_settings);
		if (names.isEmpty())
		{
			continue;
		}
		const QString output_path
			= QDir::cleanPath(QDir(m_output_path).filePath(names.first()));
		owners[output_path] << QFileInfo(input_file).fileName();
	}

	QStringList collisions;
	for (auto it = owners.constBegin(); it != owners.constEnd(); ++it)
	{
		if (it.value().size() > 1)
		{
			collisions << tr("%1（来自 %2）")
							  .arg(QFileInfo(it.key()).fileName(),
								   it.value().join(QStringLiteral(", ")));
		}
	}
	if (collisions.isEmpty())
	{
		return QString();
	}
	return tr("批量输入的产物文件名重名，无法安全处理: %1")
		.arg(collisions.join(QStringLiteral("; ")));
}

void
ImageProcessService::cleanup_crashed_batch_outputs()
{
	const MediaSettings expected = batch_expected_settings();
	for (const QString &input_file : m_pending_files)
	{
		const QStringList names
			= OutputPathResolver::batch_output_file_names(input_file, expected);
		if (names.isEmpty())
		{
			continue;
		}
		const QString path = QDir(m_output_path).filePath(names.first());
		const QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
		if (m_output_snapshot.contains(key))
		{
			// 任务开始前已存在的旧成品/用户文件：无法恢复被崩溃覆盖的内容，
			// 但也不能删除（可能仍可用），重试会重新覆盖；保留不动。
			continue;
		}
		if (QFileInfo::exists(path) && !QFile::remove(path))
		{
			LOG_WARNING(
				QStringLiteral("Failed to remove crashed partial output: "
							   "%1")
					.arg(path));
		}
	}
}

void
ImageProcessService::remove_staged_output_file()
{
	if (!m_staged_output_path.isEmpty())
	{
		QFile::remove(m_staged_output_path);
	}
}

void
ImageProcessService::finish_batch_per_file()
{
	if (m_batch_failed_files.isEmpty())
	{
		finish_job(true, tr("处理完成: 共 %1 个文件").arg(m_total_files));
		return;
	}

	// 合并为单条可翻译串（英文语序无法逐段拼接）；中文输出与旧实现一致。
	const QString message
		= m_batch_failed_details.isEmpty()
			  ? tr("处理不完整: 成功 %1/%2")
					.arg(m_batch_success_count)
					.arg(m_total_files)
			  : tr("处理不完整: 成功 %1/%2，失败: %3")
					.arg(m_batch_success_count)
					.arg(m_total_files)
					.arg(m_batch_failed_details.join(QStringLiteral("; ")));
	finish_job(false, message);
}

void
ImageProcessService::publish_progress(int v_percent)
{
	if (v_percent < 0)
	{
		v_percent = 0;
	}
	if (v_percent > 100)
	{
		v_percent = 100;
	}
	m_progress = v_percent;
	emit progress_changed(m_progress);
}

void
ImageProcessService::publish_counts(int v_processed)
{
	if (v_processed < 0)
	{
		v_processed = 0;
	}
	if (v_processed > m_total_files)
	{
		v_processed = m_total_files;
	}
	m_processed_files = v_processed;
	emit file_counts_changed(m_processed_files, m_total_files);
}

void
ImageProcessService::finish_job(bool v_success, const QString &v_message)
{
	m_watcher->stop();
	discard_staged_output();
	cleanup_temp();
	m_job_mode = JobMode::NONE;

	if (v_success)
	{
		m_processed_files = m_total_files;
		m_state = ProcessState::COMPLETED;
		emit state_changed(m_state);
		publish_counts(m_total_files);
		publish_progress(100);
		LOG_INFO(
			QStringLiteral("Image processing completed: %1").arg(v_message));
		emit output_ready(m_output_path);
	}
	else
	{
		m_state = ProcessState::FAILED;
		emit state_changed(m_state);
		LOG_ERROR(QStringLiteral("Image processing failed: %1").arg(v_message));
	}

	emit finished(v_success, v_message);
}

void
ImageProcessService::fail_job(const QString &v_reason)
{
	const QString message = v_reason.startsWith(tr("处理失败"))
								? v_reason
								: tr("处理失败: %1").arg(v_reason);
	finish_job(false, message);
}

void
ImageProcessService::cleanup_temp()
{
	if (m_temp_dir != nullptr)
	{
		m_temp_dir->remove();
		delete m_temp_dir;
		m_temp_dir = nullptr;
	}
}

QString
ImageProcessService::staged_output_path(const QString &v_final_path) const
{
	// 与最终产物同目录（保证 rename 原子/同卷），隐藏前缀 + 固定后缀便于
	// 崩溃后下一次任务自动清理残留。
	// 扩展名恒为 .png：引擎只负责产出无损 PNG（见 start_engine），目标格式
	// jpg / webp 由 commit_staged_output 在内存中转换，避免引擎在带 alpha
	// 的输入上把 jpg 输出改名成 "<name>.jpg.png"。
	const QFileInfo final_info(v_final_path);
	const QString name = QStringLiteral(".%1.resr-part.png")
							 .arg(final_info.completeBaseName());
	return QDir(final_info.absolutePath()).filePath(name);
}

bool
ImageProcessService::commit_staged_output(const QString &v_final_path,
										  QString *v_error)
{
	if (!file_ready(m_staged_output_path))
	{
		discard_staged_output();
		if (v_error != nullptr)
		{
			*v_error = tr("引擎未生成输出文件: %1").arg(v_final_path);
		}
		return false;
	}

	// 暂存文件永远是 PNG：目标也是 PNG 时直接改名（无损、不重编码），
	// 否则解码到内存后按目标格式以固定质量编码，格式与扩展名严格一致。
	const QByteArray format = output_image_format(v_final_path);
	bool committed = false;
	if (format.isEmpty() || format == QByteArrayLiteral("png"))
	{
		// QFile::rename 在 Windows 上不能覆盖已存在的目标：先直接试（POSIX
		// 原子覆盖），失败且目标存在时再移除旧文件重试。避免“先删旧文件再
		// 发现改名失败”把旧成品也弄丢。
		committed = QFile::rename(m_staged_output_path, v_final_path);
		if (!committed && QFileInfo::exists(v_final_path))
		{
			QFile::remove(v_final_path);
			committed = QFile::rename(m_staged_output_path, v_final_path);
		}
	}
	else
	{
		const QImage image(m_staged_output_path);
		committed
			= !image.isNull() && encode_image_to(image, v_final_path, format);
		if (committed)
		{
			QFile::remove(m_staged_output_path);
		}
	}

	if (!committed)
	{
		LOG_ERROR(QStringLiteral("Failed to commit staged output: %1 -> %2")
					  .arg(m_staged_output_path, v_final_path));
		discard_staged_output();
		if (v_error != nullptr)
		{
			*v_error = tr("无法写入输出文件: %1").arg(v_final_path);
		}
		return false;
	}
	m_staged_output_path.clear();
	return true;
}

void
ImageProcessService::discard_staged_output()
{
	if (!m_staged_output_path.isEmpty())
	{
		QFile::remove(m_staged_output_path);
		m_staged_output_path.clear();
	}
}

void
ImageProcessService::on_runner_progress_changed(int v_percent)
{
	// 批量目录模式的进度由输出目录轮询给出，引擎的逐文件百分比会回退，忽略
	if (m_job_mode != JobMode::SINGLE)
	{
		return;
	}
	publish_progress(v_percent);
}

void
ImageProcessService::on_runner_state_changed(ProcessState v_runner_state)
{
	switch (v_runner_state)
	{
		case ProcessState::PAUSED:
			m_state = ProcessState::PAUSED;
			emit state_changed(m_state);
			break;
		case ProcessState::RUNNING:
			if (m_state != ProcessState::RUNNING)
			{
				m_state = ProcessState::RUNNING;
				emit state_changed(m_state);
			}
			break;
		default:
			// IDLE / COMPLETED / FAILED 由 finished / stop 路径统一处理
			break;
	}
}

void
ImageProcessService::on_watcher_count_changed(int v_processed_count)
{
	if (m_job_mode != JobMode::BATCH_DIRECTORY || m_total_files == 0)
	{
		return;
	}

	int processed = v_processed_count;
	if (processed > m_total_files)
	{
		processed = m_total_files;
	}
	publish_counts(processed);
	publish_progress(processed * 100 / m_total_files);
}

void
ImageProcessService::on_runner_finished(bool v_process_ok, int v_exit_code,
										const QStringList &v_failures)
{
	if (m_cancelled)
	{
		return;
	}

	// 引擎崩溃（0xC0000409 / crash）自动降级重试一次：-t 128 / -j 1:1:1。
	// 重试期间任务状态保持 RUNNING（进度不清零/不重复计数），只重试已崩溃的
	// 这一次引擎调用；成功按正常路径收尾，避免批量失败聚合重复计数。
	if (engine_crash_retry_available(v_process_ok, v_exit_code))
	{
		m_engine_retry_used = true;
		LOG_WARNING(QStringLiteral("Engine crashed (exit code %1), retrying "
								   "once with safe args (-t %2, -j %3)")
						.arg(v_exit_code)
						.arg(safe_retry_tile_size())
						.arg(safe_retry_thread_count()));
		if (m_job_mode == JobMode::SINGLE
			|| m_job_mode == JobMode::BATCH_PER_FILE)
		{
			// 只删暂存文件、保留 m_staged_output_path：重试仍写同一路径，
			// 成功后收尾才能提交；否则成功重试会被判失败并遗留孤儿产物。
			remove_staged_output_file();
		}
		else if (m_job_mode == JobMode::BATCH_DIRECTORY)
		{
			// 目录模式直接写输出目录：删除本次崩溃前新增的半成品（快照之外的
			// 期望产物，不碰任务开始前已存在的旧成品），然后以清理后的状态
			// 作为重试校验基线并重启进度轮询（否则首次崩溃写出的产物会让
			// "未更新"校验提前通过，重试期间进度也会冻结）。
			cleanup_crashed_batch_outputs();
			m_watcher->stop();
			m_output_snapshot = OutputPathResolver::snapshot_batch_outputs(
				m_pending_files, batch_expected_settings(), m_output_path);
			m_watcher->start(m_output_path, m_total_files, MediaKind::IMAGE);
		}
		start_engine_attempt(true);
		return;
	}

	switch (m_job_mode)
	{
		case JobMode::SINGLE:
		{
			bool success = v_process_ok && v_failures.isEmpty()
						   && file_ready(m_staged_output_path);
			if (success)
			{
				QString commit_error;
				if (!commit_staged_output(m_output_path, &commit_error))
				{
					success = false;
					fail_job(commit_error);
					return;
				}
			}
			else
			{
				discard_staged_output();
			}
			if (!success)
			{
				QString reason = describe_engine_failure(
					v_failures, v_exit_code, v_process_ok);
				if (reason.isEmpty())
				{
					reason = tr("引擎未生成输出文件: %1").arg(m_output_path);
				}
				fail_job(with_engine_retry_hint(reason));
				return;
			}
			finish_job(true, tr("处理完成"));
			break;
		}
		case JobMode::BATCH_DIRECTORY:
		{
			m_watcher->stop();
			// 逐输入校验产物：不再用「目录内图片总数 == 输入数」——输出目录里
			// 上一次运行的同名旧产物或旧格式残留会把它算进去，导致改格式重跑
			// 误报失败。这里要求每个输入对应的产物存在、非空且相对任务开始
			// 时的快照确实更新。
			// 目录模式：引擎以 -f png 直写输出目录，产物名为 "<stem>.png"。
			// 这里逐个转成用户选择的目标格式（png 目标保持原名），再做产物校验，
			// 避免把"引擎只写 PNG"误判成处理失败。
			const MediaSettings batch_settings = batch_expected_settings();
			for (const QString &batch_input : m_pending_files)
			{
				const QStringList expected_names
					= OutputPathResolver::batch_output_file_names(
						batch_input, batch_settings);
				if (expected_names.isEmpty())
				{
					continue;
				}
				const QString source_png
					= QDir(m_output_path)
						  .filePath(QFileInfo(batch_input).completeBaseName()
									+ QStringLiteral(".png"));
				const QString target_path
					= QDir(m_output_path).filePath(expected_names.first());
				if (!file_ready(source_png))
				{
					continue;
				}
				if (QFileInfo(source_png).absoluteFilePath()
					== QFileInfo(target_path).absoluteFilePath())
				{
					continue;
				}
				const QImage image(source_png);
				const QByteArray target_format
					= output_image_format(target_path);
				if (!image.isNull() && !target_format.isEmpty()
					&& encode_image_to(image, target_path, target_format))
				{
					QFile::remove(source_png);
				}
			}

			const QStringList unresolved
				= OutputPathResolver::unresolved_batch_outputs(
					m_pending_files, batch_expected_settings(), m_output_path,
					m_output_snapshot);

			const bool success
				= v_process_ok && v_failures.isEmpty() && unresolved.isEmpty();
			if (!success)
			{
				QString reason = describe_engine_failure(
					v_failures, v_exit_code, v_process_ok);
				if (reason.isEmpty())
				{
					reason = tr("输入 %1 个文件，有 %2 个产物缺失或未更新: %3")
								 .arg(m_total_files)
								 .arg(unresolved.size())
								 .arg(unresolved.join(QStringLiteral(", ")));
				}
				fail_job(with_engine_retry_hint(reason));
				return;
			}
			finish_job(true, tr("处理完成: 共 %1 个文件").arg(m_total_files));
			break;
		}
		case JobMode::BATCH_PER_FILE:
		{
			bool success = v_process_ok && v_failures.isEmpty()
						   && file_ready(m_staged_output_path);
			QString reason;
			if (success)
			{
				QString commit_error;
				if (!commit_staged_output(m_current_output_file, &commit_error))
				{
					success = false;
					reason = commit_error;
				}
			}
			else
			{
				discard_staged_output();
			}
			if (success)
			{
				const QStringList unresolved
					= OutputPathResolver::unresolved_batch_outputs(
						QStringList{ m_pending_files.at(m_batch_index) },
						m_request.m_settings, m_output_path, m_output_snapshot);
				if (!unresolved.isEmpty())
				{
					success = false;
					reason = tr("引擎未生成输出文件: %1")
								 .arg(m_current_output_file);
				}
			}
			if (success)
			{
				++m_batch_success_count;
			}
			else
			{
				// 失败聚合：单个文件失败不终止整批，跑完后再统一报"处理不完整"。
				if (reason.isEmpty())
				{
					reason = describe_engine_failure(v_failures, v_exit_code,
													 v_process_ok);
				}
				if (reason.isEmpty())
				{
					reason = tr("引擎未生成输出文件: %1")
								 .arg(m_current_output_file);
				}
				reason = with_engine_retry_hint(reason);
				m_batch_failed_files << m_current_file;
				m_batch_failed_details
					<< tr("%1（%2）").arg(m_current_file, reason);
				LOG_ERROR(QStringLiteral("Batch file failed: %1").arg(reason));
			}

			++m_batch_index;
			publish_counts(m_batch_index);
			publish_progress(
				m_total_files > 0 ? m_batch_index * 100 / m_total_files : 0);
			begin_next_batch_file();
			break;
		}
		case JobMode::NONE:
			break;
	}
}
