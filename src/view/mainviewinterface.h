#ifndef MAINVIEWINTERFACE_H
#define MAINVIEWINTERFACE_H

// ============================================================================
// 视图层 - 主视图接口
//
// 控制器只依赖本接口，而非具体的 MainWindow，从而：
//   - 视图实现可以整体替换 / 便于单元测试；
//   - 界面细节（布局、控件类型）被隔离在实现内部。
//
// 约定：
//   - 「信号」= 用户手势，由视图发射、控制器订阅；
//   - 「虚方法」= 状态下发，由控制器调用、视图渲染。
// ============================================================================

#include <QString>
#include <QStringList>
#include <QWidget>

#include "mediasettings.h"
#include "processstatus.h"

// 进度卡右侧主操作按钮的语义状态（开始 / 暂停 / 继续）
enum class PrimaryAction
{
	START,
	PAUSE,
	RESUME
};

// 任务控制按钮的可用状态
struct ControlState
{
	PrimaryAction m_primary_action = PrimaryAction::START;
	bool m_primary_enabled = false;
	bool m_stop_enabled = false;
	// 任务运行中：视图禁用工作台输入 / 输出 / 参数 / 刷新 / 模式切换，
	// 只保留停止等任务控制；设置页、侧栏、窗口按钮不受影响。
	bool m_busy = false;
};

class IMainView : public QWidget
{
	Q_OBJECT

  public:
	explicit IMainView(QWidget *v_parent = nullptr);
	~IMainView() override;

	// ------------------------------------------------------------------
	// 用户手势（视图 -> 控制器）
	// ------------------------------------------------------------------
  signals:
	void
	select_input_requested(); // 点击「选择文件 / 选择目录」
	void
	delete_input_requested(); // 点击「删除文件」
	void
	set_output_path_requested(); // 点击「设置输出路径」
	void
	open_output_folder_requested(); // 点击「打开输出文件夹」
	void
	save_as_requested(); // 点击「另存为」
	void
	refresh_requested(); // 点击「刷新」
	void
	start_requested(); // 点击「开始处理」
	void
	pause_resume_requested(); // 点击「暂停 / 继续处理」
	void
	stop_requested(); // 点击「取消处理」
	void
	files_dropped(const QStringList &v_files); // 拖放文件 / 目录
	// 图片 / 视频模式切换（视图内部已做好界面联动，控制器仅同步会话）
	void
	mode_changed(MediaKind v_kind);
	// 单文件 / 批量模式切换
	void
	batch_mode_changed(bool v_batch_mode);

	// 设置页：超分引擎路径与日志目录
	void
	engine_executable_browse_requested(); // 点击「选择引擎可执行文件」
	void
	engine_directory_browse_requested(); // 点击「选择引擎目录」
	void
	engine_path_reset_requested(); // 点击「恢复自动发现」
	void
	open_log_directory_requested(); // 点击「打开日志目录」

	// 用户请求关闭窗口（控制器负责确认 / 停止运行中任务，再调用 force_close）
	void
	close_requested();

	// ------------------------------------------------------------------
	// 状态下发（控制器 -> 视图）
	// ------------------------------------------------------------------
  public:
	// 输入选择变化（isDirectory 表示选择的是目录；validFileCount
	// 为目录内的有效文件数）
	virtual void
	set_input_selection(const QString &v_path, int v_valid_file_count,
						bool v_is_directory)
		= 0;
	virtual void
	clear_input_selection()
		= 0;

	// 输出路径变化
	virtual void
	set_output_path(const QString &v_path)
		= 0;
	virtual void
	clear_output()
		= 0;
	virtual void
	show_output_preview(const QString &v_output_path)
		= 0;

	// 处理状态快照渲染（进度 / 详情 / 状态栏 / 当前文件）
	virtual void
	apply_status(const ProcessStatus &v_status)
		= 0;

	// 仅更新进度详情文案（如选中目录后显示「第0个/共N个」），不动进度条
	virtual void
	set_progress_detail(int v_current, int v_total, int v_percent)
		= 0;

	// 操作按钮可用状态
	virtual void
	apply_control_state(const ControlState &v_state)
		= 0;

	// 恢复默认设置
	virtual void
	reset_to_defaults()
		= 0;

	// 真正关闭窗口（控制器完成确认 / 停止任务后调用；closeEvent 会放行）
	virtual void
	force_close()
		= 0;

	// 设置页：超分引擎当前生效位置
	//   v_effective_executable：当前实际使用的引擎可执行文件路径
	//   v_configured_path：用户在 QSettings 中手动指定的值（空 = 自动发现）
	//   v_status_text：来源说明（自动发现 / 手动指定 / 指定无效已回退）
	virtual void
	apply_engine_path_info(const QString &v_effective_executable,
						   const QString &v_configured_path,
						   const QString &v_status_text)
		= 0;

	// 从界面收集当前标量设置（不含输入输出路径 —— 路径由控制器会话管理）
	virtual MediaSettings
	collect_settings() const
		= 0;

	// 读取界面当前状态（供控制器会话使用）
	virtual bool
	is_video_mode() const
		= 0;
	virtual bool
	is_batch_mode() const
		= 0;
};

#endif // MAINVIEWINTERFACE_H