#ifndef APPCONTROLLER_H
#define APPCONTROLLER_H

// ============================================================================
// 控制器层 - 应用控制器
//
// 视图与模型之间的协调者：
//   - 订阅视图的用户手势信号；
//   - 维护会话状态（输入 / 输出选择、处理模式）；
//   - 驱动 ProcessService 执行任务；
//   - 把处理状态快照渲染到视图；
//   - 维护设置页的超分引擎路径（校验 / 持久化 / 状态下发）与日志目录入口。
//
// 视图依赖（IMainView）与对话框（IDialogService）均为接口，
// 本控制器不直接依赖具体窗口 / 对话框实现。
// ============================================================================

#include <QObject>
#include <QString>
#include <QStringList>

#include "processstatus.h"
#include "sessionstate.h"

class IMainView;
class IDialogService;
class ProcessService;

class AppController : public QObject
{
	Q_OBJECT

  public:
	AppController(IMainView *v_view, IDialogService *v_dialogs,
				  QObject *v_parent = nullptr);
	~AppController() override;

	void
	initialize();

  private slots:
	void
	on_select_input_requested();
	void
	on_delete_input_requested();
	void
	on_set_output_path_requested();
	void
	on_open_output_folder_requested();
	void
	on_save_as_requested();
	void
	on_refresh_requested();
	// 视图请求关闭窗口：处理中先确认并停止任务，收尾完成后再 force_close
	void
	on_close_requested();

	void
	on_start_requested();
	void
	on_pause_resume_requested();
	void
	on_stop_requested();

	void
	on_files_dropped(const QStringList &v_files);
	void
	on_mode_changed(MediaKind v_kind);
	void
	on_batch_mode_changed(bool v_batch_mode);

	// 设置页：超分引擎路径 / 日志目录
	void
	on_engine_executable_browse_requested();
	void
	on_engine_directory_browse_requested();
	void
	on_engine_path_reset_requested();
	void
	on_open_log_directory_requested();

	// 界面语言切换：视图 retranslate 之后重下发动态状态文案（不新增视图接口）
	void
	on_language_changed();

	void
	on_status_changed(const ProcessStatus &v_status);
	void
	on_process_finished(bool v_success, const QString &v_message);
	// ProcessService：取消真正收尾（视频后台线程已退出）
	void
	on_process_stopped();
	void
	on_output_ready(const QString &v_output_path);

  private:
	void
	bind_view();
	void
	update_control_state();
	void
	update_engine_path_info();
	void
	apply_engine_path_override(const QString &v_selected_path);
	void
	apply_input_selection(const QString &v_path, bool v_is_directory,
						  const QStringList &v_file_list);
	void
	reset_session();
	void
	reset_input_path();
	// 清空上一任务的输出预览与进度/状态，保留会话里的输出目录设置
	void
	reset_task_view_state();
	// 处理中拒绝会改变任务输入 / 输出 / 模式的操作，并给出提示
	bool
	ensure_idle_for(const QString &v_action);
	// 开始前校验输入与 kind / mode 匹配；不合法时写入可展示的原因
	bool
	validate_start_request(QString *v_error) const;
	// 输入文件是否属于指定媒体种类（图片模式额外放行静态 GIF，链路可预转 PNG）
	bool
	is_supported_input_file(const QString &v_path, MediaKind v_kind) const;
	QString
	save_as_filter() const;

	IMainView *m_view;
	IDialogService *m_dialogs;
	ProcessService *m_process;
	SessionState m_session;
	// 关闭流程进行中：等待 ProcessService::stopped() 后调用 force_close
	bool m_close_pending = false;
};

#endif // APPCONTROLLER_H