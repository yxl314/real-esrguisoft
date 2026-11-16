#ifndef MEDIAPROCESSSERVICE_H
#define MEDIAPROCESSSERVICE_H

// ============================================================================
// 模型层 - 原生媒体处理服务公共基类
//
// ImageProcessService / VideoProcessService 的共同对外契约：
//   - 输入：一次 ProcessRequest（单文件 / 批量）；
//   - 输出：统一的进度 / 当前文件 / 文件计数 / 生命周期 / 产物就绪信号。
// ProcessService 面向本基类做分发，不感知图片 / 视频的具体实现。
// ============================================================================

#include <QObject>
#include <QString>

#include "processrequest.h"
#include "processstatus.h"

class MediaProcessService : public QObject
{
	Q_OBJECT

  public:
	explicit MediaProcessService(QObject *v_parent = nullptr)
		: QObject(v_parent)
	{
	}
	~MediaProcessService() override = default;

	// 启动任务；校验失败时通过 finished(false, ...) 报告并返回 false
	virtual bool
	start(const ProcessRequest &v_request)
		= 0;
	virtual void
	pause()
		= 0;
	virtual void
	resume()
		= 0;
	virtual void
	stop()
		= 0;

	virtual ProcessState
	state() const
		= 0;
	virtual bool
	is_active() const
		= 0;
	virtual int
	progress() const
		= 0;
	virtual QString
	current_file() const
		= 0;
	virtual QString
	output_path() const
		= 0;

	// 手工指定引擎 bin 目录（默认自动搜索，一般无需调用）
	virtual void
	set_bin_directory(const QString &v_dir)
		= 0;

  signals:
	// 总体进度百分比（0-100）
	void
	progress_changed(int v_percent);
	// 正在处理的文件（批量目录模式为「N 个文件 · 目录名」摘要）
	void
	current_file_changed(const QString &v_file_name);
	// 已完成 / 总数（批量模式用于「处理中 x/y」）
	void
	file_counts_changed(int v_processed, int v_total);
	void
	state_changed(ProcessState v_state);
	// 产物就绪（成功时先发本信号，再发 finished）
	void
	output_ready(const QString &v_output_path);
	// 任务结束：成功或失败各发射一次
	void
	finished(bool v_success, const QString &v_message);
};

#endif // MEDIAPROCESSSERVICE_H
