#ifndef PROCESSSTATUS_H
#define PROCESSSTATUS_H

// ============================================================================
// 领域层 - 处理状态模型
//
// 处理管线（ProcessService / ExeRunner）向下游（视图 / 控制器）广播的
// 统一状态快照。原先散落的 7 个进度信号（progressChanged / stateChanged /
// statusMessageChanged / currentFileChanged ...）合并为一个结构化快照，
// 下游只消费一个信号即可完整重建界面状态。
// ============================================================================

#include <QString>

// 处理生命周期状态
enum class ProcessState
{
	IDLE,	   // 空闲 / 就绪
	RUNNING,   // 处理中
	PAUSED,	   // 已暂停
	COMPLETED, // 处理完成
	FAILED	   // 处理失败
};

// 处理状态快照
struct ProcessStatus
{
	ProcessState m_state = ProcessState::IDLE; // 生命周期状态
	int m_progress_percent = 0;				   // 当前文件处理百分比 0-100
	int m_current_index = 0;				   // 当前处理到第几个（1 起）
	int m_total_files = 0;					   // 文件总数
	QString m_current_file;					   // 正在处理的文件名
	QString m_message;						   // 状态描述（界面直接展示）
	QString m_output_path;					   // 当前输出路径（完成后可用）

	ProcessStatus() = default;

	ProcessStatus(ProcessState v_s, int v_percent, int v_index, int v_total,
				  const QString &v_file, const QString &v_msg,
				  const QString &v_out)
		: m_state(v_s), m_progress_percent(v_percent), m_current_index(v_index),
		  m_total_files(v_total), m_current_file(v_file), m_message(v_msg),
		  m_output_path(v_out)
	{
	}
};

// 状态对应的短标签（界面状态栏左侧展示），如 "待处理" / "处理中"
QString
process_state_label(ProcessState v_state);

// 判断状态是否「正在占用处理资源」（运行中或已暂停）
bool
process_is_active(ProcessState v_state);

#endif // PROCESSSTATUS_H