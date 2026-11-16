#include "appcontroller.h"

#include "language_manager.h"

#include "appsettings.h"
#include "execommand.h"
#include "fileservice.h"
#include "idialogservice.h"
#include "logger.h"
#include "mainviewinterface.h"
#include "processservice.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

AppController::AppController(IMainView *v_view, IDialogService *v_dialogs,
							 QObject *v_parent)
	: QObject(v_parent), m_view(v_view), m_dialogs(v_dialogs),
	  m_process(new ProcessService(this))
{
}

AppController::~AppController() = default;

void
AppController::initialize()
{
	Logger::instance()->set_log_dir(QStringLiteral("logs"));
	Logger::instance()->set_log_to_file(true);
	Logger::instance()->set_log_to_console(true);
	Logger::instance()->set_log_level(LogLevel::INFO);

	bind_view();
	update_control_state();
	// 设置页：下发引擎当前生效位置（自动发现 / 手动指定）
	update_engine_path_info();

	// 实时语言切换：MainWindow 在 AppController 之前构造并连接，因此视图先
	// retranslate，本控制器随后重下发动态状态文案（顺序保证不被旧文案覆盖）。
	connect(&LanguageManager::instance(), &LanguageManager::language_changed,
			this, &AppController::on_language_changed);

	LOG_INFO("Application initialized");
}

void
AppController::bind_view()
{
	// 用户手势
	connect(m_view, &IMainView::select_input_requested, this,
			&AppController::on_select_input_requested);
	connect(m_view, &IMainView::delete_input_requested, this,
			&AppController::on_delete_input_requested);
	connect(m_view, &IMainView::set_output_path_requested, this,
			&AppController::on_set_output_path_requested);
	connect(m_view, &IMainView::open_output_folder_requested, this,
			&AppController::on_open_output_folder_requested);
	connect(m_view, &IMainView::save_as_requested, this,
			&AppController::on_save_as_requested);
	connect(m_view, &IMainView::refresh_requested, this,
			&AppController::on_refresh_requested);
	// 关闭窗口：由控制器确认 / 停止任务后再 force_close
	connect(m_view, &IMainView::close_requested, this,
			&AppController::on_close_requested);

	connect(m_view, &IMainView::start_requested, this,
			&AppController::on_start_requested);
	connect(m_view, &IMainView::pause_resume_requested, this,
			&AppController::on_pause_resume_requested);
	connect(m_view, &IMainView::stop_requested, this,
			&AppController::on_stop_requested);

	connect(m_view, &IMainView::files_dropped, this,
			&AppController::on_files_dropped);
	connect(m_view, &IMainView::mode_changed, this,
			&AppController::on_mode_changed);
	connect(m_view, &IMainView::batch_mode_changed, this,
			&AppController::on_batch_mode_changed);

	// 设置页：超分引擎路径 / 日志目录
	connect(m_view, &IMainView::engine_executable_browse_requested, this,
			&AppController::on_engine_executable_browse_requested);
	connect(m_view, &IMainView::engine_directory_browse_requested, this,
			&AppController::on_engine_directory_browse_requested);
	connect(m_view, &IMainView::engine_path_reset_requested, this,
			&AppController::on_engine_path_reset_requested);
	connect(m_view, &IMainView::open_log_directory_requested, this,
			&AppController::on_open_log_directory_requested);

	// 模型状态
	connect(m_process, &ProcessService::status_changed, this,
			&AppController::on_status_changed);
	connect(m_process, &ProcessService::finished, this,
			&AppController::on_process_finished);
	connect(m_process, &ProcessService::output_ready, this,
			&AppController::on_output_ready);
	// 取消真正收尾（视频后台线程退出）：驱动关闭流程继续
	connect(m_process, &ProcessService::stopped, this,
			&AppController::on_process_stopped);
}

// ---------------------------------------------------------------------------
// 输入 / 输出选择
// ---------------------------------------------------------------------------
void
AppController::on_select_input_requested()
{
	if (!ensure_idle_for(tr("更换输入")))
	{
		return;
	}

	if (m_session.m_batch_mode)
	{
		const QString dir
			= m_dialogs->get_existing_directory(m_view, tr("选择输入目录"));
		if (!dir.isEmpty())
		{
			const QStringList files = FileService::files_in_directory(
				dir, FileService::filters(m_session.m_kind));
			apply_input_selection(dir, true, files);
			LOG_INFO(QStringLiteral("Selected input directory: %1 (%2 files)")
						 .arg(dir)
						 .arg(files.size()));
		}
	}
	else
	{
		const QString file = m_dialogs->get_open_file_name(
			m_view, tr("选择输入文件"),
			ExeCommand::file_dialog_filter(m_session.m_kind));
		if (!file.isEmpty())
		{
			if (!is_supported_input_file(file, m_session.m_kind))
			{
				m_dialogs->show_information(
					m_view, tr("提示"),
					tr("所选文件不是当前模式支持的%1文件，"
					   "请重新选择")
						.arg(m_session.m_kind == MediaKind::VIDEO
								 ? tr("视频")
								 : tr("图片")));
				update_control_state();
				return;
			}
			apply_input_selection(file, false, QStringList());
			LOG_INFO("Selected input file: " + file);
		}
	}

	update_control_state();
}

void
AppController::apply_input_selection(const QString &v_path, bool v_is_directory,
									 const QStringList &v_file_list)
{
	m_session.m_input_path = v_path;
	m_session.m_input_file_count = v_is_directory ? v_file_list.size() : 0;
	m_view->set_input_selection(v_path, m_session.m_input_file_count,
								v_is_directory);
	// 新输入代表新任务：清掉上一任务的完成/失败状态、百分比和输出预览，
	// 但保留用户已选择的输出目录设置。
	reset_task_view_state();

	if (v_is_directory)
	{
		m_view->set_progress_detail(0, m_session.m_input_file_count, 0);
	}
}

void
AppController::reset_task_view_state()
{
	m_view->clear_output();
	if (!m_session.m_output_path.isEmpty())
	{
		m_view->set_output_path(m_session.m_output_path);
	}
	m_view->apply_status(ProcessStatus(ProcessState::IDLE, 0, 0, 0, QString(),
									   tr("待处理"), QString()));
}

void
AppController::on_delete_input_requested()
{
	if (!ensure_idle_for(tr("移除输入")))
	{
		return;
	}

	reset_session();
	m_view->clear_input_selection();
	m_view->clear_output();
	m_view->apply_status(ProcessStatus(ProcessState::IDLE, 0, 0, 0, QString(),
									   tr("待处理"), QString()));
	update_control_state();
	LOG_INFO("Cleared input path");
}

void
AppController::on_set_output_path_requested()
{
	if (!ensure_idle_for(tr("修改输出路径")))
	{
		return;
	}

	const QString output_path
		= m_dialogs->get_existing_directory(m_view, tr("选择输出目录"));
	if (output_path.isEmpty())
	{
		return;
	}

	if (!QFileInfo(output_path).isWritable())
	{
		m_dialogs->show_critical(m_view, tr("错误"),
								 tr("所选目录没有写入权限，请选择其他目录"));
		return;
	}

	m_session.m_output_path = output_path;
	m_view->set_output_path(output_path);
	LOG_INFO("Set output path: " + output_path);
}

void
AppController::on_open_output_folder_requested()
{
	QString target = m_session.m_output_path;
	if (target.isEmpty())
	{
		target = m_session.m_input_path;
	}
	if (target.isEmpty())
	{
		m_dialogs->show_information(m_view, tr("提示"),
									tr("请先选择输入文件或设置输出路径"));
		return;
	}

	const QFileInfo info(target);
	const QString path_to_open = info.isDir() ? target : info.absolutePath();

	if (!QDir(path_to_open).exists())
	{
		m_dialogs->show_critical(m_view, tr("错误"), tr("输出目录不存在"));
		return;
	}

	m_dialogs->open_folder(path_to_open);
	LOG_INFO("Opened output folder: " + path_to_open);
}

void
AppController::on_save_as_requested()
{
	if (!ensure_idle_for(tr("另存为")))
	{
		return;
	}
	if (m_session.m_batch_mode)
	{
		// 批量模式的输出是目录（{输入目录名_模型}/），「另存为单个文件」语义不成立。
		m_dialogs->show_information(
			m_view, tr("提示"),
			tr("批量模式下请使用「设置输出目录」选择输出父目录，"
			   "不支持另存为单个文件"));
		return;
	}
	if (m_session.m_input_path.isEmpty())
	{
		m_dialogs->show_critical(m_view, tr("错误"), tr("请先选择输入文件"));
		return;
	}

	const QFileInfo input_info(m_session.m_input_path);
	const QString default_name
		= input_info.completeBaseName() + QStringLiteral("_output");

	const QString output_path = m_dialogs->get_save_file_name(
		m_view, tr("另存为"), default_name, save_as_filter());

	if (!output_path.isEmpty())
	{
		m_session.m_output_path = output_path;
		m_view->set_output_path(output_path);
		LOG_INFO("Save as path set: " + output_path);
	}
}

void
AppController::on_refresh_requested()
{
	if (!ensure_idle_for(tr("重置")))
	{
		return;
	}
	if (!m_dialogs->ask_confirmation(
			m_view, tr("重置会话"),
			tr("确定要清空当前输入/输出并恢复默认参数吗？此操作不可撤销。")))
	{
		return;
	}
	reset_session();
	m_view->clear_input_selection();
	m_view->clear_output();
	m_view->apply_status(ProcessStatus(ProcessState::IDLE, 0, 0, 0, QString(),
									   tr("待处理"), QString()));
	m_view->reset_to_defaults();

	update_control_state();
	LOG_INFO("Settings refreshed");
}

// ---------------------------------------------------------------------------
// 设置页：超分引擎路径 / 日志目录
// ---------------------------------------------------------------------------
void
AppController::on_engine_executable_browse_requested()
{
	const QString selected = m_dialogs->get_open_file_name(
		m_view, tr("选择超分引擎可执行文件"),
		tr("引擎可执行文件 (realesrgan-ncnn-vulkan.exe);;"
		   "可执行文件 (*.exe);;所有文件 (*.*)"));
	if (selected.isEmpty())
	{
		return;
	}
	apply_engine_path_override(selected);
}

void
AppController::on_engine_directory_browse_requested()
{
	const QString selected = m_dialogs->get_existing_directory(
		m_view, tr("选择超分引擎目录（bin 或 bin/realesrgan）"));
	if (selected.isEmpty())
	{
		return;
	}
	apply_engine_path_override(selected);
}

void
AppController::on_engine_path_reset_requested()
{
	AppSettings::set_engine_path(QString());
	update_engine_path_info();
	LOG_INFO("Engine path reset to auto discovery");
}

void
AppController::on_open_log_directory_requested()
{
	const QString log_dir = Logger::instance()->log_directory();
	// 尚未写过日志时目录可能不存在；先创建再打开，避免文件管理器静默失败。
	if (!QDir().mkpath(log_dir))
	{
		m_dialogs->show_critical(m_view, tr("错误"),
								 tr("日志目录不可用：%1").arg(log_dir));
		return;
	}
	m_dialogs->open_folder(log_dir);
	LOG_INFO("Opened log directory: " + log_dir);
}

// 校验用户选择的路径并持久化；无效时提示并保持原设置不变。
void
AppController::apply_engine_path_override(const QString &v_selected_path)
{
	const QString normalized
		= QDir::cleanPath(QFileInfo(v_selected_path).absoluteFilePath());
	if (ExeCommand::bin_directory_for_path(normalized).isEmpty())
	{
		m_dialogs->show_information(
			m_view, tr("无法识别引擎路径"),
			tr("所选位置未找到超分引擎。\n\n"
			   "请选择 realesrgan-ncnn-vulkan.exe 可执行文件，或包含 "
			   "realesrgan/ 的 bin 目录（引擎可执行文件位于 "
			   "bin/realesrgan/ 下）。"));
		return;
	}

	AppSettings::set_engine_path(normalized);
	update_engine_path_info();
	LOG_INFO("Engine path set: " + normalized);
}

// 把当前生效的引擎位置下发给设置页（路径 + 来源说明）。
void
AppController::update_engine_path_info()
{
	const ExeCommand::EngineLocation location = ExeCommand::engine_location();

	QString status;
	switch (location.m_source)
	{
		case ExeCommand::EnginePathSource::MANUAL:
			status = tr("来源：手动指定（优先于自动发现）");
			break;
		case ExeCommand::EnginePathSource::MANUAL_INVALID:
			status = tr("来源：手动指定路径无效（%1），已回退自动发现")
						 .arg(location.m_configured_path);
			break;
		case ExeCommand::EnginePathSource::AUTO_DISCOVERED:
		default:
			status = tr("来源：自动发现（未手动指定）");
			break;
	}
	if (!QFileInfo(location.m_executable_path).isFile())
	{
		status += tr(
			"；未找到引擎可执行文件，请检查路径或 bin/realesrgan 是否完整");
	}

	m_view->apply_engine_path_info(location.m_executable_path,
								   location.m_configured_path, status);
}

// ---------------------------------------------------------------------------
// 任务控制
// ---------------------------------------------------------------------------
void
AppController::on_start_requested()
{
	// 二次保护：视图 busy 时已禁用开始按钮，这里覆盖快捷键 / 程序化入口。
	// 背景：视频取消是异步的，UI 状态已回 IDLE 但 worker 可能仍在退出；
	// ProcessService::is_busy() 覆盖「取消收尾中」窗口，避免「停止→立即开始」
	// 被旧 worker 的迟到信号污染。
	if (m_process->is_busy())
	{
		m_dialogs->show_information(
			m_view, tr("提示"),
			tr("已有任务在运行（或正在停止），请稍候再开始"));
		return;
	}

	QString error;
	if (!validate_start_request(&error))
	{
		m_dialogs->show_critical(m_view, tr("错误"), error);
		return;
	}

	ProcessRequest request;
	request.m_kind = m_session.m_kind;
	request.m_mode = m_session.m_batch_mode ? ProcessMode::BATCH_PROCESS
											: ProcessMode::SINGLE_FILE;
	request.m_input_path = m_session.m_input_path;
	request.m_output_path = m_session.m_output_path;
	request.m_settings = m_view->collect_settings();
	request.m_settings.m_kind = request.m_kind;
	request.m_settings.m_process_mode = request.m_mode;

	// 新任务开始时先清掉上一次的输出预览与结束状态（保留输出目录设置），
	// 避免旧结果角标、旧百分比和新任务进度同时出现。
	reset_task_view_state();

	// 引擎目录以「开始处理」这一刻的设置为准：设置页改动无需重启程序，
	// ProcessService 在构造时就缓存过自动发现结果，这里按当前配置重设。
	m_process->set_exe_directory(ExeCommand::discover_bin_directory());

	const bool accepted = m_process->start(request);
	update_control_state();
	if (!accepted)
	{
		// 同步失败：ProcessService 已通过 finished(false, ...) 弹出具体原因；
		// 若是 busy 竞态（预检之后又有任务进入），再明确提示一次。
		if (m_process->is_busy())
		{
			m_dialogs->show_information(m_view, tr("提示"),
										tr("已有任务在运行，请稍候再开始"));
		}
		LOG_WARNING("Process start rejected by ProcessService");
		return;
	}

	LOG_INFO("Process started");
}

void
AppController::on_pause_resume_requested()
{
	if (m_process->state() == ProcessState::RUNNING)
	{
		m_process->pause();
		LOG_INFO("Process paused");
	}
	else if (m_process->state() == ProcessState::PAUSED)
	{
		m_process->resume();
		LOG_INFO("Process resumed");
	}
}

void
AppController::on_stop_requested()
{
	m_process->stop();
	update_control_state();
	LOG_INFO("Process stopped");
}

// ---------------------------------------------------------------------------
// 拖放与模式切换
// ---------------------------------------------------------------------------
void
AppController::on_files_dropped(const QStringList &v_files)
{
	if (!ensure_idle_for(tr("更换输入")))
	{
		return;
	}
	if (v_files.isEmpty())
	{
		return;
	}

	const QString first_file = v_files.first();
	const QFileInfo info(first_file);

	if (info.isDir())
	{
		if (!m_session.m_batch_mode)
		{
			m_dialogs->show_information(
				m_view, tr("提示"),
				tr("当前是单文件模式，无法直接使用目录输入。"
				   "请切换到批量处理，或拖入单个文件"));
			return;
		}
		const QStringList dir_files = FileService::files_in_directory(
			first_file, FileService::filters(m_session.m_kind));
		apply_input_selection(first_file, true, dir_files);
		LOG_INFO(QStringLiteral("Dropped directory: %1 (%2 files)")
					 .arg(first_file)
					 .arg(dir_files.size()));
	}
	else
	{
		if (m_session.m_batch_mode)
		{
			m_dialogs->show_information(
				m_view, tr("提示"),
				tr("当前是批量模式，请拖入包含%1文件的目录")
					.arg(m_session.m_kind == MediaKind::VIDEO ? tr("视频")
															  : tr("图片")));
			return;
		}
		if (!is_supported_input_file(first_file, m_session.m_kind))
		{
			m_dialogs->show_information(
				m_view, tr("提示"),
				tr("拖入的文件不是当前模式支持的%1文件")
					.arg(m_session.m_kind == MediaKind::VIDEO ? tr("视频")
															  : tr("图片")));
			return;
		}
		apply_input_selection(first_file, false, QStringList());
		LOG_INFO("Dropped file: " + first_file);
	}

	update_control_state();
}

void
AppController::on_mode_changed(MediaKind v_kind)
{
	if (!ensure_idle_for(tr("切换处理模式")))
	{
		return;
	}
	m_session.m_kind = v_kind;
	LOG_INFO(QStringLiteral("Function mode changed to: %1")
				 .arg(media_kind_token(v_kind)));

	// 空闲时切换模式：已选输入若与新模式的语义不匹配，清空并明确提示，
	// 避免用户点「开始」后才收到「输入文件中没有视频流」这类滞后错误。
	if (m_session.m_input_path.isEmpty())
	{
		return;
	}
	const QFileInfo info(m_session.m_input_path);
	const bool input_matches
		= m_session.m_batch_mode
			  ? info.isDir()
			  : (info.isFile()
				 && is_supported_input_file(m_session.m_input_path, v_kind));
	if (!input_matches)
	{
		reset_session();
		m_view->clear_input_selection();
		reset_task_view_state();
		update_control_state();
		m_dialogs->show_information(
			m_view, tr("提示"),
			tr("已清除与当前模式不匹配的输入，请重新选择%1")
				.arg(m_session.m_batch_mode ? tr("输入目录") : tr("输入文件")));
		return;
	}
	// 批量目录在图片/视频模式都兼容：切换模式时清掉上一模式的结果/进度，
	// 避免视频模式继续显示图片任务输出。
	reset_task_view_state();
	if (m_session.m_batch_mode)
	{
		m_view->set_progress_detail(0, m_session.m_input_file_count, 0);
	}
}

void
AppController::on_batch_mode_changed(bool v_batch_mode)
{
	if (!ensure_idle_for(tr("切换处理模式")))
	{
		return;
	}
	m_session.m_batch_mode = v_batch_mode;
	LOG_INFO(QStringLiteral("Process mode changed to: %1")
				 .arg(v_batch_mode ? "Batch" : "Single"));

	// 空闲时切换单文件 / 批量：输入或输出路径语义可能已不匹配，尽早清理。
	if (!m_session.m_input_path.isEmpty())
	{
		const QFileInfo input_info(m_session.m_input_path);
		const bool input_matches
			= v_batch_mode ? input_info.isDir()
						   : (input_info.isFile()
							  && is_supported_input_file(m_session.m_input_path,
														 m_session.m_kind));
		if (!input_matches)
		{
			reset_session();
			m_view->clear_input_selection();
			reset_task_view_state();
			update_control_state();
			m_dialogs->show_information(
				m_view, tr("提示"),
				tr("已清除与当前模式不匹配的输入，请重新选择%1")
					.arg(v_batch_mode ? tr("输入目录") : tr("输入文件")));
			return;
		}
	}
	// 批量模式的输出必须是目录：另存为留下的文件路径在这里清掉。
	if (v_batch_mode && !m_session.m_output_path.isEmpty()
		&& QFileInfo(m_session.m_output_path).isFile())
	{
		m_session.m_output_path.clear();
		m_view->clear_output();
		LOG_INFO("Cleared file output path because batch mode requires a "
				 "directory");
	}
}

// ---------------------------------------------------------------------------
// 关闭流程
// ---------------------------------------------------------------------------
void
AppController::on_close_requested()
{
	if (m_close_pending)
	{
		return;
	}
	if (!m_process->is_busy())
	{
		m_view->force_close();
		return;
	}

	const bool confirmed = m_dialogs->ask_confirmation(
		m_view, tr("退出确认"),
		tr("当前有处理任务正在运行。\n"
		   "退出将停止该任务并清理临时文件，确定退出吗？"));
	if (!confirmed)
	{
		return;
	}

	m_close_pending = true;
	LOG_INFO("Close requested while task is running; stopping task first");
	if (m_process->is_active())
	{
		m_process->stop();
	}
	update_control_state();

	// 图片链路 stop 是同步的，后台立即可用；视频链路等待 stopped() 信号。
	if (!m_process->is_busy())
	{
		m_close_pending = false;
		m_view->force_close();
		return;
	}
	// 兜底：极端情况下后台迟迟不退出，超时后仍然关闭（析构里有 terminate 兜底，
	// 不会出现 QThread 仍在运行被 delete 的 abort）。
	QTimer::singleShot(10000, this,
					   [this]()
					   {
						   if (!m_close_pending)
						   {
							   return;
						   }
						   LOG_WARNING(
							   "Timed out waiting for background task to stop; "
							   "forcing close");
						   m_close_pending = false;
						   m_view->force_close();
					   });
}

void
AppController::on_process_stopped()
{
	// 后台已真正空闲：刷新按钮 / 输入可用状态（视频取消窗口期间界面保持 busy）。
	update_control_state();
	if (!m_close_pending)
	{
		return;
	}
	m_close_pending = false;
	LOG_INFO("Background task stopped; closing window");
	m_view->force_close();
}

// ---------------------------------------------------------------------------
// 输入 / 模式校验
// ---------------------------------------------------------------------------
bool
AppController::is_supported_input_file(const QString &v_path,
									   MediaKind v_kind) const
{
	if (FileService::is_file_of_kind(v_path, v_kind))
	{
		return true;
	}
	// 静态 GIF 在图片模式由链路用 QImage 预转 PNG 支持；FileService 的图片
	// 扩展名表暂不含 gif，这里按链路实际能力放行（批量扫描仍遵循 domain 表）。
	return v_kind == MediaKind::IMAGE
		   && FileService::file_extension(v_path) == QStringLiteral("gif");
}

bool
AppController::validate_start_request(QString *v_error) const
{
	const QString kind_label
		= m_session.m_kind == MediaKind::VIDEO ? tr("视频") : tr("图片");
	if (m_session.m_input_path.isEmpty())
	{
		*v_error = tr("请先选择输入文件或目录");
		return false;
	}

	const QFileInfo input_info(m_session.m_input_path);
	if (m_session.m_batch_mode)
	{
		if (!input_info.isDir())
		{
			*v_error = tr("批量模式需要选择输入目录，"
						  "当前输入不是目录，请重新选择");
			return false;
		}
		const QStringList files = FileService::files_in_directory(
			m_session.m_input_path, FileService::filters(m_session.m_kind));
		if (files.isEmpty())
		{
			*v_error = tr("输入目录中没有可处理的%1文件，"
						  "请重新选择目录")
						   .arg(kind_label);
			return false;
		}
		if (!m_session.m_output_path.isEmpty()
			&& QFileInfo(m_session.m_output_path).isFile())
		{
			*v_error = tr("批量模式的输出路径必须是目录"
						  "（当前指向文件），请重新设置输出目录");
			return false;
		}
	}
	else
	{
		if (!input_info.isFile())
		{
			*v_error = tr("单文件模式需要选择输入文件，"
						  "当前输入不是文件，请重新选择");
			return false;
		}
		if (!is_supported_input_file(m_session.m_input_path, m_session.m_kind))
		{
			*v_error = tr("所选文件不是当前模式支持的%1文件，"
						  "请重新选择")
						   .arg(kind_label);
			return false;
		}
	}
	return true;
}

bool
AppController::ensure_idle_for(const QString &v_action)
{
	if (!m_process->is_busy())
	{
		return true;
	}
	m_dialogs->show_information(m_view, tr("提示"),
								tr("处理中无法%1，请先停止处理").arg(v_action));
	return false;
}

// ---------------------------------------------------------------------------
// 模型状态 -> 视图
// ---------------------------------------------------------------------------
void
AppController::on_status_changed(const ProcessStatus &v_status)
{
	m_view->apply_status(v_status);
	update_control_state();
}

void
AppController::on_process_finished(bool v_success, const QString &v_message)
{
	if (m_close_pending)
	{
		// 正在关闭：不再弹完成 / 错误框。finished 表示后台任务已经终止
		// （正常完成或失败），此时立即关闭窗口，不再等待可能不会到来的
		// stopped()（QA round-2 复现过「Close requested 之后无后续」的卡顿；
		// 视频取消与"任务恰好失败"竞态时可能只发 finished 不发 stopped）。
		m_close_pending = false;
		LOG_INFO(QStringLiteral(
			"Task finished while close pending; closing window"));
		update_control_state();
		m_view->force_close();
		return;
	}
	if (v_success)
	{
		m_dialogs->show_information(m_view, tr("完成"), v_message);
	}
	else
	{
		m_dialogs->show_critical(m_view, tr("错误"), v_message);
	}
	update_control_state();
}

void
AppController::on_output_ready(const QString &v_output_path)
{
	m_view->show_output_preview(v_output_path);
	LOG_INFO("Output ready: " + v_output_path);
}

// ---------------------------------------------------------------------------
// 内部辅助
// ---------------------------------------------------------------------------
void
AppController::on_language_changed()
{
	// 非视图层不在切换时缓存翻译结果：这里用当前 translator 重新生成全部
	// 动态文案（暂停/继续按钮、引擎来源说明、进度/状态徽章）。
	// 注意：处理任务运行期间 LanguageManager 会延迟 translator 装卸，本回调
	// 因而总在 worker 退出后触发（避免与 worker 线程的 translate 数据竞争）。
	update_control_state();
	update_engine_path_info();
	m_view->apply_status(m_process->current_status());
}

void
AppController::update_control_state()
{
	const bool has_input = !m_session.m_input_path.isEmpty();
	// 批量目录必须至少有一个有效文件才允许开始；空目录在视图提示里
	// 会保持禁用，避免用户点击后才收到校验错误。
	const bool has_valid_input
		= has_input
		  && (!m_session.m_batch_mode || m_session.m_input_file_count > 0);
	const bool processing = (m_process->state() == ProcessState::RUNNING);
	const bool paused = (m_process->state() == ProcessState::PAUSED);
	const bool active = processing || paused;

	ControlState state;
	if (processing)
	{
		state.m_primary_action = PrimaryAction::PAUSE;
		state.m_primary_enabled = true;
	}
	else if (paused)
	{
		state.m_primary_action = PrimaryAction::RESUME;
		state.m_primary_enabled = true;
	}
	else
	{
		state.m_primary_action = PrimaryAction::START;
		state.m_primary_enabled = has_valid_input && !m_process->is_busy();
	}
	state.m_stop_enabled = active;
	// busy 覆盖「取消收尾中」窗口：界面不回落到可操作态，避免停止后立刻
	// 改输入/参数造成界面与后台脱节（后台线程可能仍在退出）。
	state.m_busy = active || m_process->is_busy();

	m_view->apply_control_state(state);
}

void
AppController::reset_session()
{
	m_session.m_input_path.clear();
	m_session.m_input_file_count = 0;
	m_session.m_output_path.clear();
}

void
AppController::reset_input_path()
{
	m_session.m_input_path.clear();
	m_session.m_input_file_count = 0;
}

QString
AppController::save_as_filter() const
{
	// 过滤器由领域枚举动态生成，与处理链路实际支持的输出格式保持一致。
	// 注意不能用 output_format_names() 显示文本反解扩展名：显示文本会被翻译，
	// 扩展名/通配符属于技术标识（OutputFormat::GIF 仅作为输入格式，跳过）。
	const QList<OutputFormat> formats = (m_session.m_kind == MediaKind::VIDEO)
											? video_output_format_options()
											: image_output_format_options();
	QStringList extensions;
	for (const OutputFormat format : formats)
	{
		if (format == OutputFormat::ORIGINAL || format == OutputFormat::GIF)
		{
			continue;
		}
		const QString extension = output_format_extension(format);
		if (!extension.isEmpty())
		{
			extensions << QStringLiteral("*.%1").arg(extension);
		}
	}
	if (extensions.isEmpty())
	{
		return tr("所有文件 (*.*)");
	}
	const QString label = m_session.m_kind == MediaKind::VIDEO ? tr("视频文件")
															   : tr("图片文件");
	return tr("%1 (%2);;所有文件 (*.*)")
		.arg(label, extensions.join(QLatin1Char(' ')));
}
