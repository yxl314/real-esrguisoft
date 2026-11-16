#ifndef PROGRESSPANEL_H
#define PROGRESSPANEL_H

// ============================================================================
// 视图层 - 处理进度卡（cc-switch v4 卡片体系）
//
// 一张 CardWidget：标题右侧百分比 + 进度条（右侧为主操作图标按钮 + 停止
// 图标按钮）+ 详情行 + 当前文件行。主操作按钮在同一个位置按
// START/PAUSE/RESUME 切换图标，避免开始/暂停/继续三个按钮并列。
//
// 状态颜色走语义 token：处理中主操作橙、暂停警告、完成成功、失败危险；
// 任务生命周期状态统一由页头徽章展示，卡片内只留百分比，避免重复。
// i18n：文案走 tr()；retranslate_ui() 用上次状态快照重设详情/当前文件
// 文案（进度条数值与颜色不变）并重设主按钮/停止按钮文案。
// ============================================================================

#include "card_widget.h"

#include <QElapsedTimer>

#include "mainviewinterface.h"
#include "processstatus.h"

class QLabel;
class QProgressBar;
class HintLabel;
class ActionButton;

class ProgressPanel : public CardWidget
{
	Q_OBJECT

  public:
	explicit ProgressPanel(QWidget *v_parent = nullptr);

	// 渲染一帧状态快照：进度条 + 百分比 + 详情/状态/当前文件
	void
	apply_status(const ProcessStatus &v_status);

	// 无完整快照时的轻量更新（如选择输入目录后）
	void
	set_detail(int v_current, int v_total, int v_percent);

	// 输入就绪状态：无输入时详情给出「请选择输入文件或目录」引导
	void
	set_input_ready(bool v_ready);

	// 单文件 / 批量模式：无输入时提示用「文件」还是「文件夹」
	void
	set_batch_mode(bool v_batch_mode);

	// 主操作按钮（开始/暂停/继续）+ 停止按钮的状态
	void
	apply_control_state(const ControlState &v_state);

	// 语言切换：用上次快照重设徽章 / 详情 / 当前文件/按钮文案
	void
	retranslate_ui();

  signals:
	void
	start_requested();
	void
	pause_resume_requested();
	void
	stop_requested();

  private:
	void
	apply_progress_tone(ProcessState v_state);
	void
	update_detail_text(int v_current, int v_total, int v_percent);
	void
	refresh_current_file();
	void
	refresh_state_message();
	void
	refresh_percent_visibility();

	QProgressBar *m_progress_bar;
	QLabel *m_detail_label;
	QLabel *m_current_file_label;
	QLabel *m_percent_label;
	HintLabel *m_state_message_label;
	ActionButton *m_primary_btn;
	ActionButton *m_stop_btn;
	ControlState m_last_control_state;

	QElapsedTimer m_click_guard;
	ProcessStatus m_last_status;
	bool m_has_status = false;
	int m_detail_current = 0;
	int m_detail_total = 0;
	int m_detail_percent = 0;
	bool m_input_ready = false;
	bool m_batch_mode = false;
	QString m_current_file_path;
};

#endif // PROGRESSPANEL_H
