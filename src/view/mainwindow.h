#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// ============================================================================
// 视图层 - 主窗口（cc-switch v4 风格壳层）
//
// 结构：
//   ┌───────────┬──────────────────────────────────────────────┐
//   │ Sidebar   │ AppPageHeader 52（标题 / 副标题 / 操作 / 窗口按钮）│
//   │ 200 / 72  ├──────────────────────────────────────────────┤
//   │           │ QStackedWidget：工作台 / 设置 / 关于            │
//   └───────────┴──────────────────────────────────────────────┘
//
// 职责边界：只做「装配 + 布局 + 状态转发」，不含业务逻辑；
// 所有业务操作仍然通过 IMainView 的信号 / 虚方法与 AppController 交互。
//
// 无边框窗口：Qt::FramelessWindowHint + 自绘页头拖拽（startSystemMove）
// + Windows 原生 WM_NCHITTEST 做 8 方向边缘缩放；页头右侧三个自绘窗口按钮。
// ============================================================================

#include "mainviewinterface.h"
#include "processstatus.h"

#include <functional>
#include <QVector>

class AdvancedSettingsPanel;
class AppPageHeader;
class BasicSettingsPanel;
class CardWidget;
class FunctionModeGroup;
class MediaPreviewPanel;
class ProcessModeGroup;
class ProgressPanel;
class SegmentedControl;
class SidebarNav;
class StatusBadge;
class VideoSettingsGroup;

class ActionButton;
class HintLabel;
class QLabel;
class QLineEdit;
class QDragLeaveEvent;
class QScrollArea;
class QShortcut;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

class MainWindow : public IMainView
{
	Q_OBJECT

  public:
	explicit MainWindow(QWidget *v_parent = nullptr);
	~MainWindow() override;

	// IMainView 实现 ------------------------------------------------------
	void
	set_input_selection(const QString &v_path, int v_valid_file_count,
						bool v_is_directory) override;
	void
	clear_input_selection() override;

	void
	set_output_path(const QString &v_path) override;
	void
	clear_output() override;
	void
	show_output_preview(const QString &v_output_path) override;

	void
	apply_status(const ProcessStatus &v_status) override;
	void
	set_progress_detail(int v_current, int v_total, int v_percent) override;
	void
	apply_control_state(const ControlState &v_state) override;

	void
	reset_to_defaults() override;

	// 设置页：超分引擎当前生效位置
	void
	apply_engine_path_info(const QString &v_effective_executable,
						   const QString &v_configured_path,
						   const QString &v_status_text) override;

	MediaSettings
	collect_settings() const override;
	bool
	is_video_mode() const override;
	bool
	is_batch_mode() const override;

	// 控制器确认 / 停止运行中任务后调用：放行 closeEvent 并关闭窗口
	void
	force_close() override;

  protected:
	void
	resizeEvent(QResizeEvent *v_event) override;

	// 语言切换：遍历侧栏 / 页头 / 各面板 / 设置页 / 关于页重设全部文案
	// （连接 LanguageManager::language_changed；视图先于控制器连接）
	void
	retranslate_all();

  protected:
	void
	closeEvent(QCloseEvent *v_event) override;
	void
	dragEnterEvent(QDragEnterEvent *v_event) override;
	void
	dragLeaveEvent(QDragLeaveEvent *v_event) override;
	void
	dropEvent(QDropEvent *v_event) override;
	void
	changeEvent(QEvent *v_event) override;
	bool
	nativeEvent(const QByteArray &v_event_type, void *v_message,
				qintptr *v_result) override;

  private:
	// —— 界面搭建 ——
	void
	setup_shell();
	QWidget *
	create_workbench_page();
	QWidget *
	create_settings_page();
	QWidget *
	create_about_page();
	void
	setup_window_buttons();
	void
	setup_shortcuts();
	void
	apply_debug_hooks();

	// —— 交互 ——
	void
	on_nav_activated(const QString &v_id);
	void
	on_mode_changed_internal(MediaKind v_kind);
	void
	on_theme_changed();
	void
	show_page(int v_page_index);
	void
	toggle_maximized();
	void
	update_header_for_current_page();
	void
	update_mode_hint();
	void
	sync_language_control();
	// busy（任务运行中）时语言选择不可用，语言提示也切到「任务结束后可切换」；
	// 空闲时恢复「切换后立即生效并自动记忆」。
	void
	update_language_hint();
	void
	register_text_updater(std::function<void()> v_updater);
	void
	update_status_badge(const ProcessStatus &v_status);
	void
	update_task_chip();
	void
	update_window_button_icons();
	void
	sync_theme_control();
	// 任务运行中禁用 / 恢复工作台编辑控件（输入输出 / 参数 / 刷新 / 模式切换）
	void
	set_workbench_busy(bool v_busy);

	// —— 状态 ——
	SidebarNav *m_sidebar = nullptr;
	AppPageHeader *m_header = nullptr;
	QStackedWidget *m_pages = nullptr;

	QWidget *m_workbench_page = nullptr;
	QWidget *m_settings_page = nullptr;
	QWidget *m_about_page = nullptr;
	QScrollArea *m_workbench_scroll = nullptr;
	HintLabel *m_mode_hint = nullptr;

	StatusBadge *m_status_badge = nullptr;
	ActionButton *m_refresh_button = nullptr;
	ControlState m_last_control_state;
	// 非工作台页（设置/关于）运行中显示的任务回跳入口
	ActionButton *m_task_chip = nullptr;
	ActionButton *m_minimize_button = nullptr;
	ActionButton *m_maximize_button = nullptr;
	ActionButton *m_close_button = nullptr;
	// 会在 busy 时禁用的键盘入口（与页头按钮 / 预览卡按钮行为保持一致）
	QShortcut *m_open_shortcut = nullptr;
	QShortcut *m_output_shortcut = nullptr;
	QShortcut *m_save_as_shortcut = nullptr;
	QShortcut *m_refresh_shortcut = nullptr;
	SegmentedControl *m_theme_control = nullptr;
	QLabel *m_theme_label = nullptr;
	SegmentedControl *m_language_segment = nullptr;
	QLabel *m_language_label = nullptr;
	HintLabel *m_language_hint = nullptr;
	CardWidget *m_defaults_card = nullptr;
	// 设置页「恢复默认设置」按钮：任务运行中禁用（该按钮会直接改视图默认值，
	// 绕过控制器的 ensure_idle_for 保护，造成 UI 与任务会话不一致）
	ActionButton *m_defaults_reset_button = nullptr;

	// 设置页：超分引擎路径 / 日志目录
	QLineEdit *m_engine_path_field = nullptr;
	HintLabel *m_engine_status_label = nullptr;
	ActionButton *m_engine_reset_button = nullptr;
	QLineEdit *m_log_path_field = nullptr;

	// 业务面板（图片 / 视频共用同一组，由 Mode 切换）
	FunctionModeGroup *m_function_mode = nullptr;
	ProcessModeGroup *m_process_mode = nullptr;
	VideoSettingsGroup *m_video_settings = nullptr;
	MediaPreviewPanel *m_preview = nullptr;
	ProgressPanel *m_progress = nullptr;
	BasicSettingsPanel *m_basic = nullptr;
	// 图片 / 视频模式分别保存基本设置，切换模式不互相覆盖
	BasicSettings m_image_basic;
	BasicSettings m_video_basic;
	MediaKind m_current_media_kind = MediaKind::IMAGE;
	AdvancedSettingsPanel *m_advanced = nullptr;

	QString m_current_nav = QStringLiteral("image");
	// 窄窗口自动折叠侧栏（用户手动展开后仍以其操作为准）：
	// 1000px 最小宽度下若保持 192px 侧栏，右栏表单会被窗口边缘裁切。
	bool m_auto_collapsed_sidebar = false;
	ProcessStatus m_last_status;
	// 设置页 / 关于页等静态文案的「语言切换重设器」（页面只构建一次）
	QVector<std::function<void()>> m_text_updaters;
	bool m_force_close = false;	   // force_close() 已确认关闭
	bool m_workbench_busy = false; // 工作台编辑控件当前是否处于 busy 禁用态
};

#endif // MAINWINDOW_H
