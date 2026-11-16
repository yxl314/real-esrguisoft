#include "videoprocessservice.h"

#include "execommand.h"
#include "exerunner.h"
#include "inputscanner.h"
#include "language_manager.h"
#include "logger.h"
#include "outputpathresolver.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#ifdef RESR_HAS_MEDIA
#include "batchprogresswatcher.h"
#include "fileservice.h"
#include "framedecoder.h"
#include "mediaprobe.h"
#include "videoencoder.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>

namespace
{

// 视频帧引擎调用的批大小：与旧 Python 版 image_batch.py 的
// DEFAULT_BATCH_SIZE(50) 保持一致——每批独立引擎进程，避免单个引擎进程
// 连续处理整段视频帧（例如 181 帧）导致 GPU 长时间 dispatch 触发驱动
// TDR/看门狗。批之间由 ExeRunner 全局串行 gate 保证 >=500ms 冷却。
constexpr int K_VIDEO_ENGINE_BATCH_FRAMES = 50;

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

bool
file_ready(const QString &v_file_path)
{
	const QFileInfo info(v_file_path);
	return info.isFile() && info.size() > 0;
}

bool
is_supported_container(const QString &v_extension)
{
	static const QStringList k_containers
		= { QStringLiteral("mp4"), QStringLiteral("mkv"),
			QStringLiteral("webm"), QStringLiteral("avi"),
			QStringLiteral("mov") };
	return k_containers.contains(v_extension);
}

// 输出容器 × 编码格式兼容性：与 media 层 videoencoder 的 muxer / 编码器选择一致。
//   webm: 仅 VP9 / AV1（H.264/H.265 无法被 webm 封装器接受）
//   mov:  仅 H.264 / H.265（VP9/AV1 无 mov 封装支持）
//   avi:  仅 H.264（其余会写出探测不可用的非标准文件）
//   mp4 / mkv: 四种编码均可
bool
encoding_supported_by_container(const QString &v_container,
								VideoEncoding v_encoding)
{
	const bool is_vp = (v_encoding == VideoEncoding::VP9
						|| v_encoding == VideoEncoding::AV1);
	if (v_container == QStringLiteral("webm"))
	{
		return is_vp;
	}
	if (v_container == QStringLiteral("mov"))
	{
		return !is_vp;
	}
	if (v_container == QStringLiteral("avi"))
	{
		return v_encoding == VideoEncoding::H264;
	}
	return true;
}

QString
container_encoding_error(const QString &v_container)
{
	if (v_container == QStringLiteral("webm"))
	{
		return QCoreApplication::translate("VideoJobWorker",
										   "webm 容器仅支持 VP9 / AV1 编码，"
										   "请修改「编码格式」或更换输出格式");
	}
	if (v_container == QStringLiteral("mov"))
	{
		return QCoreApplication::translate("VideoJobWorker",
										   "mov 容器仅支持 H.264 / H.265 编码，"
										   "请修改「编码格式」或更换输出格式");
	}
	if (v_container == QStringLiteral("avi"))
	{
		return QCoreApplication::translate("VideoJobWorker",
										   "avi 容器仅支持 H.264 编码，"
										   "请修改「编码格式」或更换输出格式");
	}
	return QCoreApplication::translate("VideoJobWorker",
									   "输出容器与编码格式不兼容");
}

// 引擎实际加载的模型文件名（与 realesrgan-ncnn-vulkan 的命名规则一致）：
//   - realesr-animevideov3：按倍数取 realesr-animevideov3-x{2,3,4}
//     （随包 models 中没有无后缀的 realesr-animevideov3.{bin,param}）；
//   - realesrgan-x4plus / realesrgan-x4plus-anime：无后缀，固定 4 倍。
// 注意 -s 参数只在 ≠4 时传给引擎（4 倍是引擎默认），文件名校验必须按
// “模型短名 + 当前倍数”推导，不能直接拿 model_file_name() 拼 .bin/.param。
QString
engine_model_file_name(ModelType v_model, int v_scale)
{
	if (v_model == ModelType::REAL_ESR_ANIMEVIDEOV3)
	{
		const int scale
			= (v_scale == 2 || v_scale == 3 || v_scale == 4) ? v_scale : 4;
		return QStringLiteral("realesr-animevideov3-x%1").arg(scale);
	}
	return model_file_name(v_model);
}

// 拆帧前校验引擎与所选模型文件，避免缺引擎/模型时白拆数 GB 帧才失败。
bool
engine_ready_for_model(const QString &v_bin_dir, ModelType v_model, int v_scale,
					   QString *v_error)
{
	const QString engine = ExeCommand::realesrgan_executable(v_bin_dir);
	const QFileInfo engine_info(engine);
	if (!engine_info.isFile())
	{
		*v_error = QCoreApplication::translate("VideoJobWorker",
											   "找不到超分引擎可执行文件：%1")
					   .arg(engine);
		return false;
	}
	if (!engine_info.isExecutable())
	{
		// Linux 上随包的引擎是 Windows PE：在拆帧前就给出明确原因，
		// 避免用户等待整段拆帧后才发现不可用。
		*v_error
			= QCoreApplication::translate(
				  "VideoJobWorker", "超分引擎不可执行（当前平台无法运行）：%1")
				  .arg(engine);
		return false;
	}
	const QString models_dir = ExeCommand::models_directory(v_bin_dir);
	const QString model_file = engine_model_file_name(v_model, v_scale);
	const QStringList required = {
		QStringLiteral("%1.param").arg(model_file),
		QStringLiteral("%1.bin").arg(model_file),
	};
	for (const QString &name : required)
	{
		const QString path = QDir(models_dir).filePath(name);
		if (!QFileInfo(path).isFile())
		{
			*v_error = QCoreApplication::translate("VideoJobWorker",
												   "超分模型文件缺失：%1")
						   .arg(path);
			return false;
		}
	}
	return true;
}

const int K_VIDEO_SHUTDOWN_WAIT_MS = 30000;

int
count_png_files(const QString &v_dir_path)
{
	const QFileInfoList entries
		= QDir(v_dir_path)
			  .entryInfoList({ QStringLiteral("*.png") },
							 QDir::Files | QDir::Readable);
	// 只数非空文件：引擎崩溃/磁盘满可能留下 0 字节残缺 PNG，若只数文件名，
	// 帧数校验会通过并把损坏帧送入编码阶段（A-8 回归检查）。
	int count = 0;
	for (const QFileInfo &info : entries)
	{
		if (info.size() > 0)
		{
			++count;
		}
	}
	return count;
}

// 引擎崩溃退出码（Windows 0xC0000409 / STATUS_STACK_BUFFER_OVERRUN 的 int 值）。
bool
is_engine_crash_exit(int v_exit_code)
{
	static constexpr int k_stack_buffer_overrun = -1073740791; // 0xC0000409
	return v_exit_code == k_stack_buffer_overrun;
}

} // namespace

// ============================================================================
// 工作线程对象：同步执行完整的「拆帧 → 帧超分 → 编码」流程
// ============================================================================
class VideoJobWorker : public QObject
{
	Q_OBJECT

  public:
	explicit VideoJobWorker(QObject *v_parent = nullptr) : QObject(v_parent) {}
	~VideoJobWorker() override = default;

	void
	set_bin_directory(const QString &v_dir)
	{
		m_bin_directory = v_dir;
	}

	// 由调度方在创建 worker 时注入共享控制块（shared_ptr 保证 worker 存活期间
	// 控制块有效，即使服务对象已被销毁）。
	void
	set_control(std::shared_ptr<VideoJobControl> v_control)
	{
		m_control = std::move(v_control);
	}

  public slots:
	void
	run_job(const ProcessRequest &v_request)
	{
		// 包装层负责 job_running 标志与「取消 / 正常结束」二选一的终止信号：
		//   - 正常完成 / 失败：run_job_impl 内 emit finished(...)
		//   - 用户取消：包装层 emit cancelled()，由服务层转成 stopped()
		if (m_control != nullptr)
		{
			m_control->m_job_running = true;
		}
		const bool finished_emitted = run_job_impl(v_request);
		m_running = false;
		if (m_control != nullptr)
		{
			m_control->m_job_running = false;
		}
		if (!finished_emitted)
		{
			emit cancelled();
		}
	}

  private:
	bool
	run_job_impl(const ProcessRequest &v_request)
	{
		if (m_running)
		{
			return finish(
				false, QCoreApplication::translate(
						   "VideoJobWorker", "处理失败: 已有视频任务在执行"));
		}

		if (cancel_requested())
		{
			return cancelled_exit();
		}

		m_running = true;
		m_request = v_request;
		m_request.m_settings.m_kind = MediaKind::VIDEO;
		m_request.m_settings.m_process_mode = v_request.m_mode;
		m_success_count = 0;
		m_failed_files.clear();
		m_output_path.clear();

		if (m_bin_directory.isEmpty())
		{
			m_bin_directory = ExeCommand::discover_bin_directory();
		}

		if (!collect_inputs())
		{
			return true; // collect_inputs 已 emit finished(false, ...)
		}

		// 拆帧前先确认引擎与所选模型文件存在：缺引擎/模型时立即失败，
		// 不让用户白等整段拆帧（4K 长视频帧序列可达数 GB）。
		QString engine_error;
		if (!engine_ready_for_model(
				m_bin_directory, m_request.m_settings.m_basic.m_model_type,
				scale_multiplier(m_request.m_settings.m_basic.m_scale_factor),
				&engine_error))
		{
			return finish(false, QCoreApplication::translate("VideoJobWorker",
															 "处理失败: %1")
									 .arg(engine_error));
		}

		m_processed_files = 0;
		publish_progress(0);
		emit file_counts_changed(0, m_total_files);
		set_state(ProcessState::RUNNING);

		for (int index = 0; index < m_pending_files.size(); ++index)
		{
			if (cancel_requested())
			{
				return cancelled_exit();
			}
			wait_while_paused();

			const QString input_file = m_pending_files.at(index);
			m_current_file = QFileInfo(input_file).fileName();
			emit current_file_changed(m_current_file);
			emit file_counts_changed(index, m_total_files);

			if (process_single_video(input_file, index))
			{
				++m_success_count;
			}
			else
			{
				m_failed_files << m_current_file;
				if (cancel_requested())
				{
					return cancelled_exit();
				}
			}

			m_processed_files = index + 1;
			emit file_counts_changed(m_processed_files, m_total_files);
		}

		if (m_success_count != m_total_files)
		{
			QString message;
			if (m_total_files == 1 && !m_last_error.isEmpty())
			{
				message = QCoreApplication::translate("VideoJobWorker",
													  "处理失败: %1")
							  .arg(m_last_error);
			}
			else
			{
				// 合并为单条可翻译串（英文语序无法逐段拼接）；中文输出与
				// 旧实现逐字一致。
				const QString failed
					= m_failed_files.join(QStringLiteral(", "));
				if (!failed.isEmpty() && !m_last_error.isEmpty())
				{
					message = QCoreApplication::translate(
								  "VideoJobWorker",
								  "处理不完整: 成功 %1/%2，失败: %3（%4）")
								  .arg(m_success_count)
								  .arg(m_total_files)
								  .arg(failed, m_last_error);
				}
				else if (!failed.isEmpty())
				{
					message = QCoreApplication::translate(
								  "VideoJobWorker",
								  "处理不完整: 成功 %1/%2，失败: %3")
								  .arg(m_success_count)
								  .arg(m_total_files)
								  .arg(failed);
				}
				else
				{
					message = QCoreApplication::translate(
								  "VideoJobWorker", "处理不完整: 成功 %1/%2")
								  .arg(m_success_count)
								  .arg(m_total_files);
				}
				if (!m_last_error.isEmpty() && failed.isEmpty())
				{
					message += QCoreApplication::translate("VideoJobWorker",
														   "（%1）")
								   .arg(m_last_error);
				}
			}
			return finish(false, message);
		}

		publish_progress(100);
		const QString message
			= m_total_files > 1
				  ? QCoreApplication::translate("VideoJobWorker",
												"处理完成: 共 %1 个文件")
						.arg(m_total_files)
				  : QCoreApplication::translate("VideoJobWorker", "处理完成");
		return finish(true, message);
	}

  signals:
	void
	progress_changed(int v_percent);
	void
	current_file_changed(const QString &v_file_name);
	void
	file_counts_changed(int v_processed, int v_total);
	void
	state_changed(ProcessState v_state);
	void
	output_ready(const QString &v_output_path);
	void
	finished(bool v_success, const QString &v_message);
	// 用户取消后本次 run_job 已退出（不发 finished，避免被当成失败）
	void
	cancelled();

  private:
	// ---- 共享控制标志（线程安全，直接从调度方读写） ----
	bool
	cancel_requested() const
	{
		return m_control != nullptr && m_control->m_cancel_requested.load();
	}

	bool
	pause_requested() const
	{
		return m_control != nullptr && m_control->m_pause_requested.load();
	}

	// 正常 / 失败结束：emit finished 并返回 true（表示已发终止信号）
	bool
	finish(bool v_success, const QString &v_message)
	{
		m_running = false;
		emit finished(v_success, v_message);
		return true;
	}

	// 取消结束：不发 finished，由包装层 emit cancelled()
	bool
	cancelled_exit()
	{
		m_running = false;
		return false;
	}

	bool
	collect_inputs()
	{
		if (m_request.m_mode == ProcessMode::BATCH_PROCESS)
		{
			if (!QDir(m_request.m_input_path).exists())
			{
				emit finished(false, QCoreApplication::translate(
										 "VideoJobWorker",
										 "处理失败: 输入路径不是文件夹: %1")
										 .arg(m_request.m_input_path));
				return false;
			}

			m_pending_files = InputScanner::video_files(
				m_request.m_input_path,
				m_request.m_settings.m_basic.m_model_type,
				m_request.m_settings.m_advanced.m_tta_mode);
			m_total_files = m_pending_files.size();
			if (m_total_files == 0)
			{
				emit finished(false,
							  QCoreApplication::translate(
								  "VideoJobWorker",
								  "处理失败: 输入目录中没有找到视频文件"));
				return false;
			}

			m_output_dir = OutputPathResolver::resolve_batch_output_dir(
				m_request.m_input_path, m_request.m_settings,
				m_request.m_output_path);
		}
		else
		{
			if (!QFileInfo(m_request.m_input_path).isFile())
			{
				emit finished(
					false, QCoreApplication::translate(
							   "VideoJobWorker", "处理失败: 输入视频不存在: %1")
							   .arg(m_request.m_input_path));
				return false;
			}

			m_pending_files = QStringList{ m_request.m_input_path };
			m_total_files = 1;
			m_output_dir.clear();
		}

		return true;
	}

	QString
	resolve_output_path(const QString &v_input_file,
						const MediaSettings &v_settings) const
	{
		// 批量：用户输出目录作为父目录；单文件：用户输出目录 / 另存为文件
		const QString user_output
			= (m_request.m_mode == ProcessMode::BATCH_PROCESS)
				  ? m_output_dir
				  : m_request.m_output_path;
		return OutputPathResolver::resolve_single_output(
			v_input_file, v_settings, user_output);
	}

	// 处理单个视频；返回 true 表示成功（失败时已把可读原因写入 m_last_error）
	bool
	process_single_video(const QString &v_input_file, int v_index)
	{
		if (cancel_requested())
		{
			return false;
		}

		// 「原格式」的输出容器沿用输入扩展名；输入容器不在输出白名单
		// （mp4/mkv/avi/mov/webm）时默认改输出 mp4 并记录提示，避免直接失败。
		// GIF 输入同样走该分支（GIF 拆帧后按 mp4 输出）。
		MediaSettings effective_settings = m_request.m_settings;
		const QString input_ext = QFileInfo(v_input_file).suffix().toLower();
		if (effective_settings.m_advanced.m_output_format
				== OutputFormat::ORIGINAL
			&& !is_supported_container(input_ext))
		{
			effective_settings.m_advanced.m_output_format = OutputFormat::MP4;
			LOG_INFO(QStringLiteral(
						 "Input container '.%1' is not an output container; "
						 "falling back to mp4 for: %2")
						 .arg(input_ext, v_input_file));
		}

		const QString output_path
			= resolve_output_path(v_input_file, effective_settings);
		const QString output_ext = QFileInfo(output_path).suffix().toLower();
		if (output_ext == QStringLiteral("gif"))
		{
			LOG_ERROR(
				"GIF output is not supported by the native video pipeline");
			m_last_error = QCoreApplication::translate(
				"VideoJobWorker",
				"当前原生链路暂不支持 GIF 视频输出，请选择 mp4/webm/mkv 等格式");
			return false;
		}
		if (!is_supported_container(output_ext))
		{
			m_last_error = QCoreApplication::translate(
							   "VideoJobWorker", "不支持的视频输出格式: .%1")
							   .arg(output_ext);
			return false;
		}
		// 输出格式 × 编码格式兼容性：在探测 / 拆帧前拦截，而不是等到写文件头
		// 失败（那时已经白跑完整条链路）。
		if (!encoding_supported_by_container(
				output_ext, effective_settings.m_video_encoding))
		{
			m_last_error = container_encoding_error(output_ext);
			return false;
		}

		const QString output_dir = QFileInfo(output_path).absolutePath();
		if (!QDir().mkpath(output_dir))
		{
			m_last_error = QCoreApplication::translate("VideoJobWorker",
													   "无法创建输出目录: %1")
							   .arg(output_dir);
			return false;
		}

		// 阶段进度映射：拆帧 0-10%，帧超分 10-85%，编码 85-100%
		const auto map_progress = [this, v_index](double v_stage_percent)
		{
			const double file_weight
				= 100.0 / (m_total_files > 0 ? m_total_files : 1);
			const double overall
				= v_index * file_weight + v_stage_percent * file_weight / 100.0;
			publish_progress(qRound(overall));
		};

		const MediaProbeResult probe = MediaProbe::probe(v_input_file);
		if (!probe.m_ok)
		{
			m_last_error = QCoreApplication::translate("VideoJobWorker",
													   "视频探测失败: %1")
							   .arg(probe.m_error.m_message);
			return false;
		}
		if (!probe.m_info.m_has_video)
		{
			m_last_error = QCoreApplication::translate("VideoJobWorker",
													   "输入文件中没有视频流");
			return false;
		}

		const MediaInfo &media_info = probe.m_info;
		double frame_rate = media_info.m_video.m_frame_rate;
		if (frame_rate <= 0.0)
		{
			frame_rate = 30.0;
		}
		const bool has_audio = media_info.m_has_audio;

		QString temp_base = m_request.m_temp_directory;
		if (temp_base.isEmpty())
		{
			temp_base = QDir::tempPath();
		}
		if (!QDir().mkpath(temp_base))
		{
			m_last_error
				= QCoreApplication::translate("VideoJobWorker",
											  "无法创建视频处理临时目录根: %1")
					  .arg(temp_base);
			return false;
		}
		QTemporaryDir temp_dir(temp_base
							   + QStringLiteral("/resr_video_XXXXXX"));
		if (!temp_dir.isValid())
		{
			m_last_error = QCoreApplication::translate(
				"VideoJobWorker", "无法创建视频处理临时目录");
			return false;
		}
		const QString frames_dir = temp_dir.filePath(QStringLiteral("frames"));
		const QString processed_dir
			= temp_dir.filePath(QStringLiteral("processed"));
		if (!QDir().mkpath(frames_dir) || !QDir().mkpath(processed_dir))
		{
			m_last_error = QCoreApplication::translate(
				"VideoJobWorker", "无法创建拆帧 / 超分临时目录");
			return false;
		}

		// ------------------------------------------------------------------
		// 阶段 1/3：拆帧为 PNG 序列
		// ------------------------------------------------------------------
		FrameDecoder decoder;
		decoder.set_progress_callback(
			[this, &map_progress](int v_done, int v_total)
			{
				wait_while_paused();
				if (v_total > 0)
				{
					map_progress(10.0 * v_done / v_total);
				}
			});
		decoder.set_cancel_checker([this]() { return cancel_requested(); });

		FrameDecoder::Options decode_options;
		decode_options.m_frame_prefix = QStringLiteral("frame");
		decode_options.m_frame_extension = QStringLiteral("png");
		decode_options.m_max_frames = m_request.m_max_video_frames;

		const FrameDecoder::Result decode_result
			= decoder.decode_to_png_sequence(v_input_file, frames_dir,
											 decode_options);
		if (!decode_result.m_ok)
		{
			if (cancel_requested())
			{
				return false;
			}
			m_last_error
				= QCoreApplication::translate("VideoJobWorker", "拆帧失败: %1")
					  .arg(decode_result.m_error.m_message);
			return false;
		}
		const int frame_count = decode_result.m_frames_written;
		if (frame_count <= 0)
		{
			m_last_error = QCoreApplication::translate(
				"VideoJobWorker", "拆帧结果为空（视频无有效帧）");
			return false;
		}
		map_progress(10.0);

		// 用户可能在拆帧刚结束 / 引擎尚未启动时点了停止：此处必须再检查一次，
		// 否则会继续启动 realesrgan 子进程（QA 证据：停止 5s 后仍在启动引擎）。
		if (cancel_requested())
		{
			return false;
		}

		// ------------------------------------------------------------------
		// 阶段 2/3：调用 realesrgan 对帧目录批量超分
		//
		// TDR 加固（task-34）：
		//   - 参数由 ExeCommand 固定为安全默认（-t 256 / -j 1:1:2，总是显式）；
		//   - 引擎异常退出（0xC0000409/crash）时清理半成品帧，用更保守的
		//     -t 128 / -j 1:1:1 自动重试一次；成功按正常路径收尾，不重复计数；
		//   - 重试只覆盖本次引擎调用，不写回用户设置。
		// ------------------------------------------------------------------
		const QString engine_exe
			= ExeCommand::realesrgan_executable(m_bin_directory);

		// 视频帧按批调用引擎（对齐旧 Python 版 image_batch.py 的 50 帧/批）。
		const QStringList frame_files
			= QDir(frames_dir)
				  .entryList(QStringList() << QStringLiteral("frame*.png"),
							 QDir::Files, QDir::Name);
		if (frame_files.size() != frame_count)
		{
			m_last_error = QCoreApplication::translate(
							   "VideoJobWorker",
							   "拆帧结果数量不一致（应为 %1 帧，实际 %2 帧）")
							   .arg(frame_count)
							   .arg(frame_files.size());
			return false;
		}
		const int batch_count
			= (frame_files.size() + K_VIDEO_ENGINE_BATCH_FRAMES - 1)
			  / K_VIDEO_ENGINE_BATCH_FRAMES;
		const QString batches_root
			= temp_dir.filePath(QStringLiteral("batches"));
		if (!QDir().mkpath(batches_root))
		{
			m_last_error = QCoreApplication::translate(
							   "VideoJobWorker", "无法创建视频帧批处理目录: %1")
							   .arg(batches_root);
			return false;
		}
		LOG_INFO(QStringLiteral("Video engine batching: %1 frames in %2 "
								"batch(es) of <= %3")
					 .arg(frame_count)
					 .arg(batch_count)
					 .arg(K_VIDEO_ENGINE_BATCH_FRAMES));

		const auto build_engine_args
			= [this](const QString &v_input_dir, const QString &v_output_dir,
					 bool v_safe_retry)
		{
			return ExeCommand::upscale_arguments(
				v_input_dir, v_output_dir,
				model_file_name(m_request.m_settings.m_basic.m_model_type),
				scale_multiplier(m_request.m_settings.m_basic.m_scale_factor),
				QStringLiteral("png"),
				m_request.m_settings.m_advanced.m_gpu_device,
				m_request.m_settings.m_advanced.m_tta_mode,
				m_request.m_settings.m_advanced.m_tile_size,
				m_request.m_settings.m_advanced.m_thread_count,
				ExeCommand::models_directory(m_bin_directory), v_safe_retry);
		};

		struct EngineAttemptResult
		{
			bool m_started = false;
			bool m_ok = false;
			int m_exit_code = 0;
			// 批准备阶段（建批目录/复制帧）的具体失败原因：与引擎 stderr 失败
			// 标记分开保存，避免最终通用文案覆盖磁盘/权限类可操作原因。
			QString m_prepare_error;
			QStringList m_failures;
		};

		const auto run_engine_attempt
			= [this, &map_progress, frame_count, &engine_exe, &processed_dir,
			   &frames_dir, &build_engine_args, &frame_files, &batches_root,
			   batch_count](bool v_safe_retry)
		{
			EngineAttemptResult result;
			BatchProgressWatcher watcher;
			watcher.start(processed_dir, frame_count, MediaKind::IMAGE);
			QObject::connect(
				&watcher, &BatchProgressWatcher::processed_count_changed, this,
				[this, &map_progress, frame_count](int v_done)
				{
					if (frame_count > 0)
					{
						map_progress(10.0 + 75.0 * v_done / frame_count);
					}
				},
				Qt::DirectConnection);

			// 暂停 / 取消在每一批引擎运行期间通过定时轮询生效。
			QTimer control_timer;
			control_timer.setInterval(100);
			ExeRunner *active_runner = nullptr;
			QObject::connect(
				&control_timer, &QTimer::timeout, this,
				[this, &active_runner]()
				{
					if (active_runner == nullptr)
					{
						return;
					}
					if (cancel_requested())
					{
						active_runner->stop();
						return;
					}
					if (pause_requested()
						&& active_runner->state() == ProcessState::RUNNING)
					{
						active_runner->pause();
					}
					else if (!pause_requested()
							 && active_runner->state() == ProcessState::PAUSED)
					{
						active_runner->resume();
					}
				});
			control_timer.start();

			for (int batch_index = 0; batch_index < batch_count; ++batch_index)
			{
				if (cancel_requested())
				{
					break;
				}
				const int first = batch_index * K_VIDEO_ENGINE_BATCH_FRAMES;
				const int last = qMin(first + K_VIDEO_ENGINE_BATCH_FRAMES,
									  frame_files.size());
				const QString batch_dir
					= QDir(batches_root)
						  .filePath(
							  QStringLiteral("batch_%1").arg(batch_index));
				QDir(batch_dir).removeRecursively();
				if (!QDir().mkpath(batch_dir))
				{
					result.m_ok = false;
					result.m_prepare_error
						= QCoreApplication::translate(
							  "VideoJobWorker", "无法创建视频帧批处理目录: %1")
							  .arg(batch_dir);
					result.m_failures << result.m_prepare_error;
					break;
				}
				bool copied = true;
				for (int file_index = first; file_index < last; ++file_index)
				{
					// 复制 50 帧可能是数十~数百 MB 的慢盘 IO：逐帧响应暂停/
					// 取消，避免停止/关闭要等整批复制完成（A-8 取消粒度）。
					wait_while_paused();
					if (cancel_requested())
					{
						break;
					}
					const QString name = frame_files.at(file_index);
					const QString source = QDir(frames_dir).filePath(name);
					const QString target = QDir(batch_dir).filePath(name);
					if (!QFile::copy(source, target))
					{
						copied = false;
						break;
					}
				}
				if (!copied)
				{
					result.m_ok = false;
					result.m_prepare_error
						= QCoreApplication::translate(
							  "VideoJobWorker",
							  "复制视频帧到批处理目录失败: %1")
							  .arg(batch_dir);
					result.m_failures << result.m_prepare_error;
					QDir(batch_dir).removeRecursively();
					break;
				}
				// 复制期间用户可能点了停止：不能再启动下一批引擎。
				if (cancel_requested())
				{
					QDir(batch_dir).removeRecursively();
					break;
				}

				LOG_INFO(QStringLiteral("Video engine batch %1/%2: %3 frame(s)")
							 .arg(batch_index + 1)
							 .arg(batch_count)
							 .arg(last - first));

				ExeRunner engine_runner;
				active_runner = &engine_runner;
				QEventLoop engine_loop;
				bool engine_ok = false;
				int engine_exit_code = 0;
				QStringList engine_failures;
				QObject::connect(
					&engine_runner, &ExeRunner::finished, &engine_loop,
					[&engine_loop, &engine_ok, &engine_exit_code,
					 &engine_failures](bool v_ok, int v_exit_code,
									   const QStringList &v_failures)
					{
						engine_ok = v_ok;
						engine_exit_code = v_exit_code;
						engine_failures = v_failures;
						engine_loop.quit();
					});
				QObject::connect(
					&engine_runner, &ExeRunner::stopped, &engine_loop,
					[&engine_loop]() { engine_loop.quit(); },
					Qt::DirectConnection);

				result.m_started = true;
				engine_runner.start(
					engine_exe,
					build_engine_args(batch_dir, processed_dir, v_safe_retry));
				engine_loop.exec();
				active_runner = nullptr;
				QDir(batch_dir).removeRecursively();

				if (!engine_ok)
				{
					result.m_ok = false;
					result.m_exit_code = engine_exit_code;
					result.m_failures += engine_failures;
					break;
				}
				// 引擎退出码不可信（失败也常返回 0），成功批次同样累积 stderr
				// 失败标记，由最终判定按 "产物 + 关键字" 策略统一处理；A-8 分批
				// 不得丢弃任一成功批次里的失败标记。
				result.m_failures += engine_failures;
				result.m_ok = true;
			}
			control_timer.stop();
			watcher.stop();
			return result;
		};

		EngineAttemptResult attempt = run_engine_attempt(false);
		if (cancel_requested())
		{
			return false;
		}

		int upscaled_count = count_png_files(processed_dir);
		bool retried = false;
		if (!attempt.m_ok && is_engine_crash_exit(attempt.m_exit_code))
		{
			retried = true;
			LOG_WARNING(
				QStringLiteral("Engine crashed (exit code %1) while upscaling "
							   "frames, retrying once with safe args "
							   "(-t %2, -j %3)")
					.arg(attempt.m_exit_code)
					.arg(safe_retry_tile_size())
					.arg(safe_retry_thread_count()));
			// 清理半成品帧（保留拆帧结果），进度回到引擎阶段起点。
			QDir(processed_dir).removeRecursively();
			(void)QDir().mkpath(processed_dir);
			map_progress(10.0);
			attempt = run_engine_attempt(true);
			if (cancel_requested())
			{
				return false;
			}
			upscaled_count = count_png_files(processed_dir);
		}

		const bool engine_ok = attempt.m_ok;
		const int engine_exit_code = attempt.m_exit_code;
		const QStringList engine_failures = attempt.m_failures;
		if (!engine_ok || !engine_failures.isEmpty()
			|| upscaled_count != frame_count)
		{
			// 批准备失败（建批目录/复制帧，典型磁盘满）优先使用具体原因：
			// 不能被下面的通用帧数/退出码文案覆盖。
			QString reason = attempt.m_prepare_error;
			if (reason.isEmpty())
			{
				for (const QString &failure : engine_failures)
				{
					if (failure.contains(QStringLiteral("decode image"),
										 Qt::CaseInsensitive))
					{
						reason = QCoreApplication::translate(
							"VideoJobWorker", "引擎无法解码帧图片");
						break;
					}
					if (failure.contains(QStringLiteral("encode image"),
										 Qt::CaseInsensitive))
					{
						reason = QCoreApplication::translate(
							"VideoJobWorker", "引擎无法编码超分帧图片");
						break;
					}
				}
			}
			if (reason.isEmpty() && !engine_failures.isEmpty())
			{
				reason = engine_failures.first();
			}
			if (reason.isEmpty())
			{
				reason = QCoreApplication::translate(
							 "VideoJobWorker",
							 "帧超分未完成（应为 %1 帧，实际 %2 帧，"
							 "退出码 %3）")
							 .arg(frame_count)
							 .arg(upscaled_count)
							 .arg(engine_exit_code);
			}
			if (retried)
			{
				reason = QCoreApplication::translate(
							 "VideoJobWorker",
							 "引擎崩溃，已用安全参数重试仍失败（%1）")
							 .arg(reason);
			}
			m_last_error = reason;
			return false;
		}

		map_progress(85.0);

		if (cancel_requested())
		{
			return false;
		}

		// ------------------------------------------------------------------
		// 阶段 3/3：PNG 序列 + 原音频 → 目标容器
		// ------------------------------------------------------------------
		const VideoEncodeParams encode_params
			= video_encode_params(m_request.m_settings.m_video_encoding,
								  m_request.m_settings.m_video_quality);

		VideoEncoder encoder;
		encoder.set_progress_callback(
			[this, &map_progress](int v_done, int v_total)
			{
				wait_while_paused();
				if (v_total > 0)
				{
					map_progress(85.0 + 15.0 * v_done / v_total);
				}
			});
		encoder.set_cancel_checker([this]() { return cancel_requested(); });

		VideoEncoder::Options encode_options;
		encode_options.m_encoding = m_request.m_settings.m_video_encoding;
		encode_options.m_crf = encode_params.m_crf;
		encode_options.m_preset = encode_params.m_preset;
		encode_options.m_frame_rate = frame_rate;
		if (media_info.m_video.m_frame_rate_num > 0
			&& media_info.m_video.m_frame_rate_den > 0)
		{
			// 用有理数原样传递帧率，避免 23.976（1199/50）这类帧率浮点漂移
			encode_options.m_frame_rate_num
				= media_info.m_video.m_frame_rate_num;
			encode_options.m_frame_rate_den
				= media_info.m_video.m_frame_rate_den;
		}
		encode_options.m_audio_source_path
			= has_audio ? v_input_file : QString();
		encode_options.m_copy_audio = true;

		const VideoEncoder::Result encode_result = encoder.encode_png_sequence(
			processed_dir, output_path, encode_options);
		if (!encode_result.m_ok)
		{
			if (cancel_requested())
			{
				return false;
			}
			m_last_error = QCoreApplication::translate("VideoJobWorker",
													   "视频编码失败: %1")
							   .arg(encode_result.m_error.m_message);
			return false;
		}
		if (!file_ready(output_path))
		{
			m_last_error = QCoreApplication::translate(
				"VideoJobWorker", "编码结束但输出文件不存在或为空");
			return false;
		}

		m_output_path = output_path;
		map_progress(100.0);
		LOG_INFO(
			QStringLiteral("Video processing finished: %1").arg(output_path));
		emit output_ready(output_path);
		return true;
	}

	void
	wait_while_paused()
	{
		// 拆帧 / 编码进度回调逐帧调用本函数：暂停时在此等待，恢复或取消即返回，
		// 因此阻塞阶段也能暂停 / 停止（不再依赖 worker 事件循环投递 queued 槽）。
		if (!pause_requested() || cancel_requested())
		{
			return;
		}
		if (m_state != ProcessState::PAUSED)
		{
			set_state(ProcessState::PAUSED);
		}
		while (pause_requested() && !cancel_requested())
		{
			QThread::msleep(30);
		}
		if (!cancel_requested() && m_state == ProcessState::PAUSED)
		{
			set_state(ProcessState::RUNNING);
		}
	}

	void
	publish_progress(int v_percent)
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
	set_state(ProcessState v_state)
	{
		m_state = v_state;
		emit state_changed(m_state);
	}

	QString m_bin_directory;
	ProcessRequest m_request;
	QStringList m_pending_files;
	QString m_output_dir;
	QStringList m_failed_files;
	QString m_last_error;
	QString m_current_file;
	QString m_output_path;
	ProcessState m_state = ProcessState::IDLE;
	int m_progress = 0;
	int m_total_files = 0;
	int m_processed_files = 0;
	int m_success_count = 0;
	std::shared_ptr<VideoJobControl> m_control;
	bool m_running = false;
};

// ============================================================================
// VideoProcessService：线程调度与状态转发
// ============================================================================
VideoProcessService::VideoProcessService(QObject *v_parent)
	: MediaProcessService(v_parent)
{
	m_bin_directory = ExeCommand::discover_bin_directory();
}

VideoProcessService::~VideoProcessService()
{
	shutdown(K_VIDEO_SHUTDOWN_WAIT_MS);
}

void
VideoProcessService::shutdown(int v_timeout_ms)
{
	if (m_thread == nullptr)
	{
		return;
	}

	// 先请求取消（原子标志，worker 在逐帧回调 / 等待循环里轮询），再退出事件循环。
	// 引擎阶段会由 worker 内的轮询定时器 stop 掉引擎进程。
	m_control->m_cancel_requested = true;
	m_control->m_pause_requested = false;
	m_thread->requestInterruption();
	m_thread->quit();

	QElapsedTimer timer;
	timer.start();
	bool finished = m_thread->wait(100);
	while (!finished && timer.elapsed() < v_timeout_ms)
	{
		finished = m_thread->wait(100);
	}
	if (!finished)
	{
		// 兜底：解码 / 编码单帧不会无限阻塞，正常不应走到这里。
		LOG_ERROR(QStringLiteral(
					  "Video worker did not stop within %1 ms; terminating")
					  .arg(v_timeout_ms));
		m_thread->terminate();
		finished = m_thread->wait(2000);
	}
	if (!finished)
	{
		// 绝不 delete 仍在运行的 QThread（会 qFatal abort）；宁可泄漏线程对象。
		LOG_ERROR("Video worker still running after terminate; leaking thread "
				  "object to avoid abort");
		m_worker = nullptr;
		m_thread = nullptr;
		return;
	}

	delete m_thread;
	m_thread = nullptr;
	m_worker = nullptr;

	// 退出清理：不广播语言切换（窗口可能正在销毁），只丢弃待应用选择。
	release_translation_deferral(false);
}

void
VideoProcessService::release_translation_deferral(bool v_apply_pending)
{
	if (!m_translation_deferred)
	{
		return;
	}
	m_translation_deferred = false;
	LanguageManager::end_translation_deferral(v_apply_pending);
}

void
VideoProcessService::ensure_worker()
{
	if (m_thread != nullptr)
	{
		return;
	}

	m_thread = new QThread();
	m_worker = new VideoJobWorker();
	m_worker->moveToThread(m_thread);
	m_worker->set_bin_directory(m_bin_directory);
	m_worker->set_control(m_control);

	connect(m_worker, &VideoJobWorker::progress_changed, this,
			&VideoProcessService::on_worker_progress_changed);
	connect(m_worker, &VideoJobWorker::current_file_changed, this,
			&VideoProcessService::on_worker_current_file_changed);
	connect(m_worker, &VideoJobWorker::file_counts_changed, this,
			&VideoProcessService::on_worker_file_counts_changed);
	connect(m_worker, &VideoJobWorker::state_changed, this,
			&VideoProcessService::on_worker_state_changed);
	connect(m_worker, &VideoJobWorker::output_ready, this,
			&VideoProcessService::on_worker_output_ready);
	connect(m_worker, &VideoJobWorker::finished, this,
			&VideoProcessService::on_worker_finished);
	connect(m_worker, &VideoJobWorker::cancelled, this,
			&VideoProcessService::on_worker_cancelled);

	connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
	m_thread->start();
}

void
VideoProcessService::set_bin_directory(const QString &v_dir)
{
	m_bin_directory = v_dir;
	if (m_worker != nullptr)
	{
		QMetaObject::invokeMethod(
			m_worker,
			[this]() { m_worker->set_bin_directory(m_bin_directory); },
			Qt::QueuedConnection);
	}
}

bool
VideoProcessService::start(const ProcessRequest &v_request)
{
	if (is_active() || m_control->m_job_running.load())
	{
		LOG_WARNING("Video processing task is already active");
		return false;
	}

	ensure_worker();

	// 清空上一轮的取消 / 暂停标志；这两个标志直接由调度方写、worker 轮询。
	m_control->m_cancel_requested = false;
	m_control->m_pause_requested = false;

	m_state = ProcessState::RUNNING;
	m_progress = 0;
	m_current_file.clear();
	m_output_path.clear();
	emit progress_changed(0);
	emit state_changed(m_state);

	// worker 线程整段处理期间会调用 QCoreApplication::translate（模型/媒体层
	// 错误文案）；Qt 6 的 translate 无锁读翻译器列表。任务期间延迟语言切换的
	// translator 装卸，任务结束（finished/cancelled/shutdown）后再应用。
	LanguageManager::begin_translation_deferral();
	m_translation_deferred = true;

	const bool posted = QMetaObject::invokeMethod(
		m_worker, [this, v_request]() { m_worker->run_job(v_request); },
		Qt::QueuedConnection);
	if (!posted)
	{
		release_translation_deferral(true);
		m_state = ProcessState::FAILED;
		emit state_changed(m_state);
		emit finished(false, tr("处理失败: 无法投递视频处理任务到工作线程"));
		return false;
	}
	return true;
}

void
VideoProcessService::pause()
{
	if (m_state != ProcessState::RUNNING || m_worker == nullptr)
	{
		return;
	}
	// 直接写原子标志：拆帧 / 编码期间 worker 不转事件循环，queued 槽不会执行。
	m_control->m_pause_requested = true;
	m_state = ProcessState::PAUSED;
	emit state_changed(m_state);
}

void
VideoProcessService::resume()
{
	if (m_state != ProcessState::PAUSED || m_worker == nullptr)
	{
		return;
	}
	m_control->m_pause_requested = false;
	m_state = ProcessState::RUNNING;
	emit state_changed(m_state);
}

void
VideoProcessService::stop()
{
	// 取消立即生效（worker 逐帧轮询）；状态先回 IDLE 让界面退出忙碌态，
	// 真正的后台收尾由 worker 结束时 emit stopped() 通知 ProcessService。
	m_control->m_cancel_requested = true;
	m_control->m_pause_requested = false;
	if (m_state != ProcessState::IDLE)
	{
		m_state = ProcessState::IDLE;
		emit state_changed(m_state);
	}
}

ProcessState
VideoProcessService::state() const
{
	return m_state;
}

bool
VideoProcessService::is_active() const
{
	return m_state == ProcessState::RUNNING || m_state == ProcessState::PAUSED;
}

int
VideoProcessService::progress() const
{
	return m_progress;
}

QString
VideoProcessService::current_file() const
{
	return m_current_file;
}

QString
VideoProcessService::output_path() const
{
	return m_output_path;
}

void
VideoProcessService::on_worker_progress_changed(int v_percent)
{
	if (m_state == ProcessState::IDLE)
	{
		return; // 取消后丢弃迟到的进度
	}
	m_progress = v_percent;
	emit progress_changed(m_progress);
}

void
VideoProcessService::on_worker_current_file_changed(const QString &v_file_name)
{
	if (m_state == ProcessState::IDLE)
	{
		return;
	}
	m_current_file = v_file_name;
	emit current_file_changed(m_current_file);
}

void
VideoProcessService::on_worker_file_counts_changed(int v_processed, int v_total)
{
	if (m_state == ProcessState::IDLE)
	{
		return;
	}
	emit file_counts_changed(v_processed, v_total);
}

void
VideoProcessService::on_worker_state_changed(ProcessState v_state)
{
	if (m_state == ProcessState::IDLE && v_state != ProcessState::IDLE)
	{
		return; // 取消后忽略工作线程的迟到状态
	}
	if (m_state == v_state)
	{
		return; // 服务层已先行置位（pause/resume），避免重复广播
	}
	m_state = v_state;
	emit state_changed(m_state);
}

void
VideoProcessService::on_worker_output_ready(const QString &v_output_path)
{
	if (m_state == ProcessState::IDLE)
	{
		return;
	}
	m_output_path = v_output_path;
	emit output_ready(m_output_path);
}

void
VideoProcessService::on_worker_finished(bool v_success,
										const QString &v_message)
{
	const bool was_cancelled = (m_state == ProcessState::IDLE);
	if (was_cancelled)
	{
		// 用户停止与正常结束竞态：若 worker 先结束、stop() 后置 IDLE，
		// 这次结果按“取消已收尾”处理，通知 ProcessService 后台已空闲。
		emit stopped();
		// worker 已退出：此时才允许应用任务期间被延迟的语言切换（不再与
		// worker 线程的 translate 并发）。
		release_translation_deferral(true);
		return;
	}

	m_state = v_success ? ProcessState::COMPLETED : ProcessState::FAILED;
	emit state_changed(m_state);
	emit finished(v_success, v_message);
	release_translation_deferral(true);
}

void
VideoProcessService::on_worker_cancelled()
{
	LOG_INFO("Video processing stopped by user");
	if (m_state != ProcessState::IDLE)
	{
		m_state = ProcessState::IDLE;
		emit state_changed(m_state);
	}
	// 通知 ProcessService：取消已真正落地，后台线程已退出本次任务。
	emit stopped();
	release_translation_deferral(true);
}

#include "videoprocessservice.moc"

#else // !RESR_HAS_MEDIA

// ============================================================================
// 媒体层尚未交付时的占位实现：明确报错，绝不静默成功
// ============================================================================
class VideoJobWorker : public QObject
{
  public:
	explicit VideoJobWorker(QObject *v_parent = nullptr) : QObject(v_parent) {}
};

VideoProcessService::VideoProcessService(QObject *v_parent)
	: MediaProcessService(v_parent)
{
}

VideoProcessService::~VideoProcessService() = default;

void
VideoProcessService::shutdown(int v_timeout_ms)
{
	Q_UNUSED(v_timeout_ms);
}

void
VideoProcessService::ensure_worker()
{
}

void
VideoProcessService::set_bin_directory(const QString &v_dir)
{
	m_bin_directory = v_dir;
}

bool
VideoProcessService::start(const ProcessRequest &v_request)
{
	Q_UNUSED(v_request);
	m_state = ProcessState::FAILED;
	emit state_changed(m_state);
	emit finished(
		false,
		tr("处理失败: 视频原生处理组件尚未就绪（媒体基础设施层未接入）"));
	return false;
}

void
VideoProcessService::pause()
{
}

void
VideoProcessService::resume()
{
}

void
VideoProcessService::stop()
{
	m_state = ProcessState::IDLE;
	emit state_changed(m_state);
}

void
VideoProcessService::on_worker_cancelled()
{
	emit stopped();
}

ProcessState
VideoProcessService::state() const
{
	return m_state;
}

bool
VideoProcessService::is_active() const
{
	return m_state == ProcessState::RUNNING || m_state == ProcessState::PAUSED;
}

int
VideoProcessService::progress() const
{
	return m_progress;
}

QString
VideoProcessService::current_file() const
{
	return m_current_file;
}

QString
VideoProcessService::output_path() const
{
	return m_output_path;
}

void
VideoProcessService::on_worker_progress_changed(int v_percent)
{
	m_progress = v_percent;
	emit progress_changed(m_progress);
}

void
VideoProcessService::on_worker_current_file_changed(const QString &v_file_name)
{
	m_current_file = v_file_name;
	emit current_file_changed(m_current_file);
}

void
VideoProcessService::on_worker_file_counts_changed(int v_processed, int v_total)
{
	emit file_counts_changed(v_processed, v_total);
}

void
VideoProcessService::on_worker_state_changed(ProcessState v_state)
{
	m_state = v_state;
	emit state_changed(m_state);
}

void
VideoProcessService::on_worker_output_ready(const QString &v_output_path)
{
	m_output_path = v_output_path;
	emit output_ready(m_output_path);
}

void
VideoProcessService::on_worker_finished(bool v_success,
										const QString &v_message)
{
	m_state = v_success ? ProcessState::COMPLETED : ProcessState::FAILED;
	emit state_changed(m_state);
	emit finished(v_success, v_message);
}

#endif // RESR_HAS_MEDIA
