#ifndef EXERUNNER_H
#define EXERUNNER_H

// ============================================================================
// 基础设施层 - 外部可执行程序生命周期管理
//
// 职责单一：启动一个外部进程、按行解析合并输出、支持暂停 / 恢复 / 停止。
//   - stdout 与 stderr 合并（realesrgan 的进度与错误全部在 stderr）；
//   - 行缓冲解析交给 ExeOutputParser（进度百分比 / 失败标记）；
//   - Windows 下通过挂起进程树实现暂停，停止时递归终止子进程；
//   - **进程级全局串行**（TDR 加固，task-34）：同一进程内任意时刻最多一个
//     外部引擎进程；并发 start 进入异步待启动队列（不阻塞调用线程），上一个
//     进程结束到下一次真正启动之间至少间隔 K_ENGINE_COOLDOWN_MS；stop/析构
//     会取消队列，保证"停止后不再启动引擎"语义不回退；
//   - **孤儿进程清理**：Windows 用 Job Object（KILL_ON_JOB_CLOSE）把引擎及其
//     子进程纳入作业，退出/停止/析构/kill 超时时强制清理；POSIX 使用进程组 kill。
// 不负责输出产物校验（成功与否由模型层的 job 服务按产物判定）。
// ============================================================================

#include <QMutex>
#include <QObject>
#include <atomic>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "processstatus.h"

class ExeRunner : public QObject
{
	Q_OBJECT

  public:
	explicit ExeRunner(QObject *v_parent = nullptr);
	~ExeRunner() override;

	// 启动外部程序（每次 start 对应一次完整任务）。
	// 上一个进程尚未真正结束时拒绝启动，并通过 start_failed + finished(false)
	// 明确报告，而不是静默忽略（避免调用方一直等不到结果）。
	void
	start(const QString &v_program, const QStringList &v_arguments);
	void
	pause();
	void
	resume();
	void
	stop();

	ProcessState
	state() const;
	bool
	is_running() const;
	int
	progress() const;
	qint64
	process_id() const;
	// 最近一次运行中识别到的失败标记（引擎失败时退出码仍可能为 0）
	QStringList
	failures() const;

  signals:
	void
	progress_changed(int v_percent);
	void
	current_file_changed(const QString &v_file_name);
	void
	state_changed(ProcessState v_state);
	// 进程结束：v_process_ok = 正常退出且退出码 0（产物仍需调用方校验）
	void
	finished(bool v_process_ok, int v_exit_code, const QStringList &v_failures);
	// 用户主动停止：正常中止，并非失败
	void
	stopped();
	// 启动失败（可执行文件不存在 / 无执行权限等）
	void
	start_failed(const QString &v_error);

  private slots:
	void
	on_process_started();
	void
	on_process_finished(int v_exit_code, QProcess::ExitStatus v_exit_status);
	void
	on_process_ready_read();
	void
	on_process_error_occurred(QProcess::ProcessError v_error);

  private:
	// 真正启动 QProcess（由 start() 直接调用，或由全局串行 gate 在冷却后调度）。
	void
	start_now(const QString &v_program, const QStringList &v_arguments);

	// 全局串行 gate（进程级静态状态，跨 ExeRunner 实例生效）。
	struct PendingStart
	{
		QPointer<ExeRunner> m_runner;
		QString m_program;
		QStringList m_arguments;
	};
	static bool
	try_begin_engine_or_queue(ExeRunner *v_runner, const QString &v_program,
							  const QStringList &v_arguments);
	static void
	dispatch_pending_engines();
	static void
	schedule_dispatch(qint64 v_delay_ms);
	static void
	release_global_gate();
	static void
	remove_pending_engine(ExeRunner *v_runner);
	// 释放本实例占用的 gate（幂等；进程结束 / 启动失败 / stop / 析构路径调用）。
	void
	release_engine_gate();

	// 消费行缓冲；v_flush_partial 为 true 时把残留的半行也解析掉
	void
	consume_buffer(bool v_flush_partial);
	void
	emit_finished_once(bool v_process_ok, int v_exit_code,
					   const QStringList &v_failures);
	// 同步等待 QProcess 真正结束；超时返回 false（调用方决定如何收尾）。
	bool
	wait_for_finished(int v_timeout_ms);
	void
	reset_state();

#ifdef Q_OS_WIN
	QList<qint64>
	get_child_processes(qint64 v_parent_pid);
	bool
	suspend_process_tree(qint64 v_pid);
	bool
	resume_process_tree();
	void
	close_suspended_handles();
#endif

	// 相邻两次引擎进程结束 -> 下一次启动之间的最小冷却时间（TDR 加固）。
	static constexpr int K_ENGINE_COOLDOWN_MS = 500;

	static constexpr int K_SHUTDOWN_WAIT_MS = 5000;

	// 全局 gate 静态状态（定义在 exerunner.cpp）
	static bool s_gate_active;
	static qint64 s_last_engine_finish_ms;
	static QList<PendingStart> s_pending_starts;
	static QMutex s_gate_mutex;

	QProcess *m_process;
	ProcessState m_state = ProcessState::IDLE;
	int m_progress = 0;
	QString m_current_file;
	QStringList m_failures;
	QString m_line_buffer;
	bool m_stopping = false;
	bool m_finished_emitted = false;
	// 本次任务已进入全局待启动队列（尚未真正 start）。
	// 跨线程访问（调用线程 / dispatch 定时器 / queued lambda），必须原子化。
	std::atomic<bool> m_queued_start{ false };
	// 本实例是否持有全局 gate（真正启动后为 true，进程结束/Canceled 后释放）。
	// 用 exchange(false) 归还，天然避免 stop/析构/迟到 lambda 的重复释放。
	std::atomic<bool> m_gate_acquired{ false };

#ifdef Q_OS_WIN
	QList<void *> m_suspended_handles;
	// Job Object 句柄（KILL_ON_JOB_CLOSE）：把引擎及其子进程纳入作业。
	void *m_job_handle = nullptr;
	void
	create_job_for_current_process();
	void
	terminate_job();
	void
	close_job();
#endif
};

#endif // EXERUNNER_H
