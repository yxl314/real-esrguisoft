#include "processservice.h"

#include "fileservice.h"
#include "imageprocessservice.h"
#include "inputscanner.h"
#include "logger.h"
#include "mediaprocessservice.h"
#include "outputpathresolver.h"
#include "videoprocessservice.h"

#include <QFileInfo>

ProcessService::ProcessService(QObject *v_parent)
	: QObject(v_parent), m_image_service(new ImageProcessService(this)),
	  m_video_service(new VideoProcessService(this))
{
	connect_service(m_image_service);
	connect_service(m_video_service);
}

ProcessService::~ProcessService() = default;

void
ProcessService::connect_service(MediaProcessService *v_service)
{
	connect(v_service, &MediaProcessService::progress_changed, this,
			&ProcessService::on_service_progress_changed);
	connect(v_service, &MediaProcessService::current_file_changed, this,
			&ProcessService::on_service_current_file_changed);
	connect(v_service, &MediaProcessService::file_counts_changed, this,
			&ProcessService::on_service_file_counts_changed);
	connect(v_service, &MediaProcessService::state_changed, this,
			&ProcessService::on_service_state_changed);
	connect(v_service, &MediaProcessService::output_ready, this,
			&ProcessService::on_service_output_ready);
	connect(v_service, &MediaProcessService::finished, this,
			&ProcessService::on_service_finished);
	// 视频取消是异步的：显式的「取消已收尾」信号用于释放后台占用。
	if (v_service == m_video_service)
	{
		connect(m_video_service, &VideoProcessService::stopped, this,
				&ProcessService::on_service_stopped);
	}
}

void
ProcessService::set_exe_directory(const QString &v_dir)
{
	// 兼容旧接口：现在传入的是引擎 bin 目录（内含 realesrgan/）
	m_image_service->set_bin_directory(v_dir);
	m_video_service->set_bin_directory(v_dir);
}

bool
ProcessService::start(const ProcessRequest &v_request)
{
	if (is_active() || m_backend_busy)
	{
		// 旧任务尚未真正收尾（尤其是视频取消窗口）时拒绝启动：
		// 否则旧进程/旧 worker 的迟到信号会污染新任务。
		LOG_WARNING("Cannot start while another task is active or finishing");
		return false;
	}
	if (v_request.m_input_path.isEmpty())
	{
		fail_with(tr("没有指定输入文件或目录"));
		return false;
	}

	reset();
	m_request = v_request;
	m_request.m_settings.m_kind = v_request.m_kind;
	m_request.m_settings.m_process_mode = v_request.m_mode;

	// 预扫描输入，给出初始文件总数 / 摘要（批量模式下与服务层扫描规则一致）
	if (m_request.m_mode == ProcessMode::BATCH_PROCESS)
	{
		const QStringList files = InputScanner::files_for_kind(
			m_request.m_kind, m_request.m_input_path, m_request.m_settings);

		if (files.isEmpty())
		{
			fail_with(tr("输入目录中没有可处理的文件"));
			return false;
		}

		m_total_files = files.size();
		m_current_index = 0;
		m_current_file
			= tr("%1 个文件 · %2")
				  .arg(m_total_files)
				  .arg(FileService::file_name(m_request.m_input_path));
		m_output_path = OutputPathResolver::resolve_batch_output_dir(
			m_request.m_input_path, m_request.m_settings,
			m_request.m_output_path);
	}
	else
	{
		m_total_files = 1;
		m_current_index = 0;
		m_current_file = FileService::file_name(m_request.m_input_path);
		m_output_path = OutputPathResolver::resolve_single_output(
			m_request.m_input_path, m_request.m_settings,
			m_request.m_output_path);
	}

	m_state = ProcessState::RUNNING;
	m_next_message.clear();
	publish();

	LOG_INFO(QStringLiteral("Dispatching %1 task: %2 -> %3")
				 .arg(media_kind_token(m_request.m_kind),
					  m_request.m_input_path, m_output_path));

	m_active_service
		= (m_request.m_kind == MediaKind::VIDEO)
			  ? static_cast<MediaProcessService *>(m_video_service)
			  : static_cast<MediaProcessService *>(m_image_service);

	m_backend_busy = true;
	const bool accepted = m_active_service->start(m_request);
	if (!accepted)
	{
		// 子服务同步失败：可能已经 emit 过 finished（由 on_service_finished
		// 收尾），也可能只是拒绝（状态仍 RUNNING），两种情况都不能返回 true。
		if (m_state == ProcessState::RUNNING)
		{
			fail_with(tr("无法启动处理任务"));
		}
		m_backend_busy = false;
		return false;
	}
	// 子服务返回 true 但可能已同步 emit finished（契约较弱的实现，如历史
	// ImageProcessService）：状态已离开 RUNNING 说明任务在 start() 内就结束了，
	// 不能对外报告「已启动」。控制器据此避免打印 "Process started"。
	if (m_state != ProcessState::RUNNING)
	{
		m_backend_busy = false;
		return false;
	}

	return true;
}

void
ProcessService::pause()
{
	if (m_state == ProcessState::RUNNING && m_active_service != nullptr)
	{
		m_active_service->pause();
	}
}

void
ProcessService::resume()
{
	if (m_state == ProcessState::PAUSED && m_active_service != nullptr)
	{
		m_active_service->resume();
	}
}

void
ProcessService::stop()
{
	if (!is_active())
	{
		// 已经 stop 过、后台仍在收尾：保持现状，由 on_service_stopped 收尾。
		return;
	}

	const MediaKind kind = m_request.m_kind;
	// 先摘除 active service 再调用其 stop()：停止期间子服务仍可能同步/异步
	// 发出 state/finished 信号（如 ExeRunner 的 IDLE、视频 worker 的迟到事件），
	// 这些信号属于已取消的任务，不能继续污染新状态（进度回跳/失败误报）。
	MediaProcessService *service = m_active_service;
	m_active_service = nullptr;
	if (service != nullptr)
	{
		service->stop();
	}

	// 图片链路 stop() 是同步的（kill 进程并清理），可立即释放；
	// 视频 worker 可能还在逐帧退出，等 on_service_stopped 再释放。
	if (kind != MediaKind::VIDEO)
	{
		m_backend_busy = false;
	}

	// 取消不是失败：状态回到 IDLE，并通过显式文案告诉界面取消进展。
	// 视频链路的后台线程是异步退出的：先显示「正在停止处理…」，等
	// on_service_stopped()（取消真正收尾）再显示「已取消处理」。
	m_state = ProcessState::IDLE;
	m_next_message.clear();
	if (kind == MediaKind::VIDEO && m_backend_busy)
	{
		publish(tr("正在停止处理…"));
	}
	else
	{
		publish(tr("已取消处理"));
	}
}

ProcessState
ProcessService::state() const
{
	return m_state;
}

bool
ProcessService::is_active() const
{
	return process_is_active(m_state);
}

bool
ProcessService::is_busy() const
{
	return is_active() || m_backend_busy;
}

ProcessStatus
ProcessService::current_status() const
{
	// RUNNING 期间的 message 是进度类文案，按当前翻译器实时重建；
	// 其他状态（失败/取消/完成/暂停）复用最近一次广播的显式消息，避免语言
	// 切换重下发时丢失去掉详细原因（P3-2）。
	QString message;
	if (m_state == ProcessState::RUNNING)
	{
		message = default_message();
	}
	else if (!m_last_message.isEmpty())
	{
		message = m_last_message;
	}
	else
	{
		message = default_message();
	}
	return ProcessStatus(m_state, m_progress, m_current_index, m_total_files,
						 m_current_file, message, m_output_path);
}

void
ProcessService::on_service_progress_changed(int v_percent)
{
	if (m_active_service == nullptr)
	{
		return; // 已 stop/收尾：丢弃旧任务的迟到进度
	}
	m_progress = v_percent;
	publish();
}

void
ProcessService::on_service_current_file_changed(const QString &v_file_name)
{
	if (m_active_service == nullptr)
	{
		return;
	}
	m_current_file = v_file_name;
	publish();
}

void
ProcessService::on_service_file_counts_changed(int v_processed, int v_total)
{
	if (m_active_service == nullptr)
	{
		return;
	}
	m_total_files = v_total;
	if (v_total <= 0)
	{
		m_current_index = 0;
	}
	else
	{
		int current_index = v_processed + 1;
		if (current_index > v_total)
		{
			current_index = v_total;
		}
		m_current_index = current_index;
	}
	publish();
}

void
ProcessService::on_service_state_changed(ProcessState v_state)
{
	if (m_active_service == nullptr)
	{
		return; // 已 stop：不再让旧任务改变生命周期状态
	}
	switch (v_state)
	{
		case ProcessState::PAUSED:
			m_state = ProcessState::PAUSED;
			publish(tr("已暂停"));
			break;
		case ProcessState::RUNNING:
			if (m_state != ProcessState::RUNNING)
			{
				m_state = ProcessState::RUNNING;
				// 恢复运行（resume）后不能让暂停/停止阶段的显式文案继续
				// sticky，否则进度文案会一直显示「已暂停」/「正在停止处理…」。
				m_next_message.clear();
				publish();
			}
			break;
		case ProcessState::IDLE:
			if (m_state != ProcessState::IDLE)
			{
				m_state = ProcessState::IDLE;
				m_next_message.clear();
				publish();
			}
			break;
		case ProcessState::FAILED:
			if (m_state != ProcessState::FAILED)
			{
				m_state = ProcessState::FAILED;
				publish(tr("处理失败"));
			}
			break;
		case ProcessState::COMPLETED:
			// 完成由服务层的 finished 信号统一处理
			break;
	}
}

void
ProcessService::on_service_output_ready(const QString &v_output_path)
{
	if (m_active_service == nullptr)
	{
		return;
	}
	m_output_path = v_output_path;
	emit output_ready(m_output_path);
}

void
ProcessService::on_service_finished(bool v_success, const QString &v_message)
{
	if (m_active_service == nullptr)
	{
		// 已 stop 后旧任务迟到的 finished：不参与新任务状态，但停止收尾窗口
		// （m_backend_busy）内必须补发 stopped，否则关闭流程会一直等不到
		// 「后台已空闲」而超时（视频取消与完成竞态）。
		if (m_backend_busy)
		{
			m_backend_busy = false;
			if (m_state == ProcessState::IDLE)
			{
				m_next_message.clear();
				publish(tr("已取消处理"));
			}
			emit stopped();
		}
		return;
	}
	m_active_service = nullptr;
	m_backend_busy = false;

	if (!v_success)
	{
		fail_with(v_message);
		return;
	}

	m_progress = 100;
	m_current_index = m_total_files;
	m_state = ProcessState::COMPLETED;

	if (m_request.m_mode == ProcessMode::BATCH_PROCESS)
	{
		const QString message = tr("处理完成: 共 %1 个文件").arg(m_total_files);
		publish(message);
		emit finished(true, message);
	}
	else
	{
		publish(tr("处理完成"));
		emit finished(true, tr("处理完成"));
	}
	LOG_INFO("Processing completed successfully");
}

void
ProcessService::on_service_stopped()
{
	// 视频取消已真正落地：后台线程退出、临时目录清理完成，可以允许新任务 / 关闭。
	m_backend_busy = false;
	m_active_service = nullptr;
	if (m_state == ProcessState::IDLE)
	{
		m_next_message.clear();
		publish(tr("已取消处理"));
	}
	emit stopped();
}

void
ProcessService::reset()
{
	m_active_service = nullptr;
	m_backend_busy = false;
	m_current_index = 0;
	m_total_files = 0;
	m_current_file.clear();
	m_progress = 0;
	m_output_path.clear();
	m_next_message.clear();
	m_last_message.clear();
	m_state = ProcessState::IDLE;
}

void
ProcessService::publish(const QString &v_explicit_message)
{
	ProcessStatus status = current_status();
	QString message;
	if (!v_explicit_message.isEmpty())
	{
		message = v_explicit_message;
		m_next_message = v_explicit_message;
	}
	else if (!m_next_message.isEmpty())
	{
		// 显式文案（正在停止处理…/已取消处理/…）要一直保留到状态或文案
		// 再次变化；否则后续进度事件会把提示覆盖回「待处理」。
		message = m_next_message;
	}
	else
	{
		message = default_message();
	}
	status.m_message = message;
	m_last_message = message;
	emit status_changed(status);
}

void
ProcessService::fail_with(const QString &v_reason)
{
	m_active_service = nullptr;
	m_backend_busy = false;
	m_state = ProcessState::FAILED;
	const QString display_msg = v_reason.startsWith(tr("处理失败"))
									? v_reason
									: tr("处理失败: %1").arg(v_reason);
	publish(display_msg);
	emit finished(false, display_msg);
	LOG_ERROR("Processing failed: " + v_reason);
}

QString
ProcessService::default_message() const
{
	if (m_state == ProcessState::RUNNING)
	{
		if (m_request.m_mode == ProcessMode::BATCH_PROCESS)
		{
			return tr("批量处理 %1 个文件").arg(m_total_files);
		}
		return tr("处理中 %1/%2").arg(m_current_index).arg(m_total_files);
	}
	if (m_state == ProcessState::PAUSED)
	{
		return tr("已暂停");
	}
	if (m_state == ProcessState::IDLE)
	{
		return tr("待处理");
	}
	if (m_state == ProcessState::COMPLETED)
	{
		return tr("处理完成");
	}
	if (m_state == ProcessState::FAILED)
	{
		return tr("处理失败");
	}
	return tr("待处理");
}
