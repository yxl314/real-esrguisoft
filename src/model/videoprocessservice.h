#ifndef VIDEOPROCESSSERVICE_H
#define VIDEOPROCESSSERVICE_H

// ============================================================================
// 模型层 - 视频原生处理服务
//
// 视频链路：探测 → 拆帧（PNG 序列）→ realesrgan 批量超分 → libav* 重新编码
// 合成（原音频优先 stream copy）。完全在进程内完成，不再经过 Python
// 可执行程序，也不依赖 ffmpeg.exe：
//   - MediaProbe / FrameDecoder / VideoEncoder 来自基础设施层媒体封装；
//   - 编解码是阻塞调用，因此任务在独立工作线程中执行，本对象只做线程调度
//     与状态转发；
//   - 编码参数（编码格式 / 质量档 → CRF / preset）与旧 Python 编排层一致。
//
// 若媒体层尚未交付（RESR_HAS_MEDIA 未定义），start() 会明确报错而不是静默
// 失败，界面可见「视频组件尚未就绪」。
// ============================================================================

#include "mediaprocessservice.h"
#include "processrequest.h"

#include <QString>

#include <atomic>
#include <memory>

class QThread;
class VideoJobWorker;

// 工作线程与调度方共享的控制块。
//
// 拆帧 / 编码是阻塞调用，worker 线程在整段处理期间不转事件循环，因此
// request_pause/resume/stop 不能再依赖 Qt::QueuedConnection 投递；调度方
// （VideoProcessService）直接写这些原子标志，worker 在每帧回调 / 等待循环里
// 轮询，保证阻塞阶段也能暂停、停止一定生效。
struct VideoJobControl
{
	std::atomic<bool> m_pause_requested{ false };
	std::atomic<bool> m_cancel_requested{ false };
	// worker 是否真的在跑 run_job（区别于服务层已把状态置 IDLE 的“取消中”窗口）
	std::atomic<bool> m_job_running{ false };
};

class VideoProcessService : public MediaProcessService
{
	Q_OBJECT

  public:
	explicit VideoProcessService(QObject *v_parent = nullptr);
	~VideoProcessService() override;

	bool
	start(const ProcessRequest &v_request) override;
	void
	pause() override;
	void
	resume() override;
	void
	stop() override;

	ProcessState
	state() const override;
	bool
	is_active() const override;
	int
	progress() const override;
	QString
	current_file() const override;
	QString
	output_path() const override;
	void
	set_bin_directory(const QString &v_dir) override;

  signals:
	// 取消真正落地（worker 已退出本次 run_job）：ProcessService 据此确认
	// 后台已空闲，才允许新的 start。
	void
	stopped();

  private slots:
	void
	on_worker_progress_changed(int v_percent);
	void
	on_worker_current_file_changed(const QString &v_file_name);
	void
	on_worker_file_counts_changed(int v_processed, int v_total);
	void
	on_worker_state_changed(ProcessState v_state);
	void
	on_worker_output_ready(const QString &v_output_path);
	void
	on_worker_finished(bool v_success, const QString &v_message);
	void
	on_worker_cancelled();

  private:
	void
	ensure_worker();
	// 取消并等待工作线程真正结束（析构 / 关闭流程用）；超时则 terminate 兜底。
	void
	shutdown(int v_timeout_ms);
	// 任务运行期间延迟翻译器切换（Qt translate 无锁读，worker 线程并发翻译）；
	// 任务结束 / 关闭时由主线程应用或丢弃。
	void
	release_translation_deferral(bool v_apply_pending);

	VideoJobWorker *m_worker = nullptr;
	QThread *m_thread = nullptr;
	// 共享控制块用 shared_ptr 持有：极端情况下工作线程无法停止而被“泄漏”时，
	// 线程内持有的副本仍保证控制块存活，避免 service 销毁后访问悬空指针。
	std::shared_ptr<VideoJobControl> m_control
		= std::make_shared<VideoJobControl>();
	bool m_translation_deferred = false;
	QString m_bin_directory;
	ProcessState m_state = ProcessState::IDLE;
	int m_progress = 0;
	QString m_current_file;
	QString m_output_path;
};

#endif // VIDEOPROCESSSERVICE_H
