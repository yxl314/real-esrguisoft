#ifndef PROCESSSERVICE_H
#define PROCESSSERVICE_H

// ============================================================================
// 模型层 - 处理编排服务
//
// 把「一项处理任务」编排为可暂停 / 恢复 / 取消的执行流程：
//   - 校验输入并按媒体种类分派给原生处理服务
//     （ImageProcessService / VideoProcessService）；
//   - 汇总子服务的进度 / 当前文件 / 文件计数，收敛为统一的 ProcessStatus；
//   - 向下游广播状态快照，屏蔽图片 / 视频链路差异。
//
// 不包含任何 UI 逻辑；控制器 / 视图通过信号消费其状态。
// ============================================================================

#include <QObject>
#include <QString>

#include "processrequest.h"
#include "processstatus.h"

class ImageProcessService;
class MediaProcessService;
class VideoProcessService;

class ProcessService : public QObject
{
	Q_OBJECT

  public:
	explicit ProcessService(QObject *v_parent = nullptr);
	~ProcessService() override;

	// 启动任务；校验不通过时返回 false 并通过 finished(false, ...) 报告
	bool
	start(const ProcessRequest &v_request);
	void
	pause();
	void
	resume();
	void
	stop();

	ProcessState
	state() const;
	bool
	is_active() const;
	// 是否占用处理资源：运行/暂停中，或已 stop 但后台取消尚未真正收尾。
	// 控制器用它做重入保护；start() 在 busy 时拒绝启动。
	bool
	is_busy() const;
	ProcessStatus
	current_status() const;

	// 手工指定引擎 bin 目录（默认自动搜索，一般无需调用）
	void
	set_exe_directory(const QString &v_dir);

  signals:
	// 状态变化（进度、文件、生命周期、输出路径）的统一广播
	void
	status_changed(const ProcessStatus &v_status);
	// 任务结束：成功或失败各发射一次（message 可直接展示给用户）
	void
	finished(bool v_success, const QString &v_message);
	// 取消已真正收尾：后台处理资源已释放（关闭流程据此继续 force_close）
	void
	stopped();
	// 单个任务输出就绪（用于界面预览）
	void
	output_ready(const QString &v_output_path);

  private slots:
	void
	on_service_progress_changed(int v_percent);
	void
	on_service_current_file_changed(const QString &v_file_name);
	void
	on_service_file_counts_changed(int v_processed, int v_total);
	void
	on_service_state_changed(ProcessState v_state);
	void
	on_service_output_ready(const QString &v_output_path);
	void
	on_service_finished(bool v_success, const QString &v_message);
	void
	on_service_stopped();

  private:
	void
	connect_service(MediaProcessService *v_service);
	void
	reset();
	void
	publish(const QString &v_explicit_message = QString());
	void
	fail_with(const QString &v_reason);
	QString
	default_message() const;

	ImageProcessService *m_image_service;
	VideoProcessService *m_video_service;
	MediaProcessService *m_active_service = nullptr; // 仅借用，不拥有

	ProcessRequest m_request;
	ProcessState m_state = ProcessState::IDLE;
	// 后台服务是否仍在收尾（视频取消是异步的）；运行期重置为 false
	bool m_backend_busy = false;
	int m_progress = 0;
	int m_current_index = 0;
	int m_total_files = 0;
	QString m_current_file;
	QString m_output_path;
	// 最近一次显式状态描述（失败 / 取消 / 处理完成等）。非 RUNNING 状态下
	// current_status() 复用它，避免语言切换等重下发场景把详细原因退化成
	// 通用文案（「处理失败」/「待处理」）。
	QString m_next_message;
	QString m_last_message; // 最近一次广播出去的 message（用于 current_status）
};

#endif // PROCESSSERVICE_H
