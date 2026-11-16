#include "exerunner.h"

#include "exeoutputparser.h"
#include "logger.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#include <chrono>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

namespace
{

// 单行输出缓冲上限：引擎异常输出超长且无换行时避免内存无界增长。
constexpr qsizetype K_MAX_LINE_BUFFER_SIZE = 1 << 20;

// 单调时钟毫秒数：用于全局串行 gate 的 500ms 冷却计算（不受系统时间调整影响）。
qint64
monotonic_milliseconds()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}

} // namespace

// 全局串行 gate 静态状态（进程级，跨 ExeRunner 实例）：TDR 加固要求同一时刻
// 最多一个引擎进程，且相邻引擎调用至少间隔 K_ENGINE_COOLDOWN_MS。
bool ExeRunner::s_gate_active = false;
qint64 ExeRunner::s_last_engine_finish_ms = 0;
QList<ExeRunner::PendingStart> ExeRunner::s_pending_starts;
QMutex ExeRunner::s_gate_mutex;

ExeRunner::ExeRunner(QObject *v_parent)
	: QObject(v_parent), m_process(new QProcess(this))
{
	// 合并输出通道：realesrgan 的进度与错误全部走 stderr
	m_process->setProcessChannelMode(QProcess::MergedChannels);

	connect(m_process, &QProcess::started, this,
			&ExeRunner::on_process_started);
	connect(m_process,
			QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
			&ExeRunner::on_process_finished);
	connect(m_process, &QProcess::readyRead, this,
			&ExeRunner::on_process_ready_read);
	connect(m_process, &QProcess::errorOccurred, this,
			&ExeRunner::on_process_error_occurred);
}

ExeRunner::~ExeRunner()
{
	stop();
	if (m_process->state() != QProcess::NotRunning)
	{
		LOG_ERROR("External process still running at destruction; waiting "
				  "before releasing engine gate");
		m_process->waitForFinished(K_SHUTDOWN_WAIT_MS);
	}
	if (m_process->state() == QProcess::NotRunning)
	{
		// 正常情况下 stop() 已归还 gate；这里兜底处理"排队被授予 gate 后
		// 对象析构"等窗口，避免全局 gate 永久泄漏（原子 exchange 可重入）。
		release_engine_gate();
	}
	else
	{
		// 旧引擎确实还活着：宁可保留 gate（后续任务排队等待）也不能破坏
		// "同一时刻最多一个引擎"的 TDR 串行不变量。
		LOG_ERROR("External process is still alive at destruction; retaining "
				  "global engine gate to preserve the serial-engine invariant");
	}
	remove_pending_engine(this);
#ifdef Q_OS_WIN
	close_job();
#endif
}

void
ExeRunner::start(const QString &v_program, const QStringList &v_arguments)
{
	if (m_process->state() != QProcess::NotRunning || m_queued_start.load())
	{
		// 明确拒绝而不是静默忽略：上一个进程尚未真正结束（或仍在全局队列中
		// 等待启动）时启动新任务，迟到的 finished/错误信号会污染新任务
		// （一次启动对应一个代次）。
		const QString error_msg
			= tr("外部程序尚未结束，无法启动新任务（请稍后重试）");
		LOG_WARNING(error_msg);
		emit start_failed(error_msg);
		m_failures.append(error_msg);
		m_state = ProcessState::FAILED;
		emit state_changed(m_state);
		emit_finished_once(false, -1, m_failures);
		return;
	}

	reset_state();

	LOG_INFO(QStringLiteral("External process requested: %1 %2")
				 .arg(v_program, v_arguments.join(QLatin1Char(' '))));

	// 全局串行：同一时刻最多一个引擎进程；busy / 冷却未结束时请求进入
	// 异步待启动队列（本函数立即返回，不阻塞调用线程 / UI），由 gate 在
	// 冷却结束后通过事件循环调度真正启动。
	if (!try_begin_engine_or_queue(this, v_program, v_arguments))
	{
		// 对外仍报告 RUNNING（有任务在排队/等待执行），由服务层状态机继续
		// 显示"处理中"；真正 start 时 start_now() 会再次发出 RUNNING 状态。
		m_state = ProcessState::RUNNING;
		emit state_changed(m_state);
		return;
	}

	start_now(v_program, v_arguments);
}

void
ExeRunner::start_now(const QString &v_program, const QStringList &v_arguments)
{
	if (m_process->state() != QProcess::NotRunning)
	{
		LOG_ERROR(
			"Engine gate dispatched start, but QProcess is still running");
		release_engine_gate();
		// 不能静默 return：调用方在等待 finished/start_failed，否则会一直挂起。
		const QString error_msg
			= tr("外部程序尚未结束，无法启动新任务（请稍后重试）");
		m_state = ProcessState::FAILED;
		emit start_failed(error_msg);
		emit state_changed(m_state);
		m_failures.append(error_msg);
		emit_finished_once(false, -1, m_failures);
		return;
	}

	reset_state();
	LOG_INFO(QStringLiteral("Starting external process: %1 %2")
				 .arg(v_program, v_arguments.join(QLatin1Char(' '))));

	m_state = ProcessState::RUNNING;
	emit state_changed(m_state);

	m_process->setProcessChannelMode(QProcess::MergedChannels);
#ifndef Q_OS_WIN
	// POSIX：子进程独立进程组，stop 时按进程组 kill（父退出不留子进程）。
	m_process->setChildProcessModifier([]() { ::setpgid(0, 0); });
#endif
	m_process->start(v_program, v_arguments, QProcess::ReadOnly);
}

bool
ExeRunner::try_begin_engine_or_queue(ExeRunner *v_runner,
									 const QString &v_program,
									 const QStringList &v_arguments)
{
	qint64 delay_ms = K_ENGINE_COOLDOWN_MS;
	{
		const QMutexLocker locker(&s_gate_mutex);
		if (!s_gate_active)
		{
			const qint64 now = monotonic_milliseconds();
			const qint64 elapsed = (s_last_engine_finish_ms == 0)
									   ? K_ENGINE_COOLDOWN_MS
									   : now - s_last_engine_finish_ms;
			if (elapsed >= K_ENGINE_COOLDOWN_MS)
			{
				s_gate_active = true;
				v_runner->m_gate_acquired.store(true);
				return true;
			}
			delay_ms = K_ENGINE_COOLDOWN_MS - elapsed;
		}

		PendingStart pending;
		pending.m_runner = v_runner;
		pending.m_program = v_program;
		pending.m_arguments = v_arguments;
		v_runner->m_queued_start.store(true);
		s_pending_starts.append(pending);
	}

	LOG_INFO(QStringLiteral("Engine is busy or cooling down, request queued "
							"(will start in >=%1 ms)")
				 .arg(delay_ms));
	schedule_dispatch(delay_ms);
	return false;
}

void
ExeRunner::dispatch_pending_engines()
{
	ExeRunner *target = nullptr;
	QString program;
	QStringList arguments;
	qint64 delay_ms = 0;
	bool invoke_failed = false;

	{
		// 整个"取队列项 -> 置 gate -> 投递 queued 启动"都在锁内完成：
		// ExeRunner 析构会先经 stop()/release_engine_gate()/remove_pending_engine()
		// 获取同一把锁，因此锁内 target 不会被并发析构，避免锁外解引用裸
		// QObject 指针（task-38 P2-1 UAF 风险的最小安全改进）。queued invoke
		// 本身不执行目标代码，不会与之形成锁顺序死锁。
		const QMutexLocker locker(&s_gate_mutex);
		if (s_gate_active || s_pending_starts.isEmpty())
		{
			return;
		}
		const qint64 now = monotonic_milliseconds();
		const qint64 elapsed = (s_last_engine_finish_ms == 0)
								   ? K_ENGINE_COOLDOWN_MS
								   : now - s_last_engine_finish_ms;
		if (elapsed < K_ENGINE_COOLDOWN_MS)
		{
			delay_ms = K_ENGINE_COOLDOWN_MS - elapsed;
		}
		else
		{
			while (!s_pending_starts.isEmpty())
			{
				const PendingStart pending = s_pending_starts.takeFirst();
				if (pending.m_runner.isNull())
				{
					continue;
				}
				target = pending.m_runner.data();
				program = pending.m_program;
				arguments = pending.m_arguments;
				s_gate_active = true;
				target->m_gate_acquired.store(true);
				break;
			}
		}

		if (delay_ms == 0 && target != nullptr)
		{
			const bool invoked = QMetaObject::invokeMethod(
				target,
				[target, program, arguments]()
				{
					if (!target->m_queued_start.load())
					{
						// 入队后已被 stop()/析构取消：归还 gate 并继续调度
						// 下一条（原子 exchange 保证重复归还安全）。
						target->release_engine_gate();
						return;
					}
					target->m_queued_start.store(false);
					target->start_now(program, arguments);
				},
				Qt::QueuedConnection);
			if (!invoked)
			{
				target->m_queued_start.store(false);
				target->m_gate_acquired.store(false);
				target = nullptr;
				invoke_failed = true;
			}
		}
	}

	if (delay_ms > 0)
	{
		schedule_dispatch(delay_ms);
		return;
	}
	if (invoke_failed)
	{
		LOG_WARNING("Failed to dispatch queued engine start");
		release_global_gate();
	}
}

void
ExeRunner::schedule_dispatch(qint64 v_delay_ms)
{
	if (v_delay_ms < 0)
	{
		v_delay_ms = 0;
	}
	QCoreApplication *app = QCoreApplication::instance();
	if (app == nullptr)
	{
		dispatch_pending_engines();
		return;
	}
	QTimer::singleShot(static_cast<int>(v_delay_ms), app,
					   []() { ExeRunner::dispatch_pending_engines(); });
}

void
ExeRunner::release_global_gate()
{
	{
		const QMutexLocker locker(&s_gate_mutex);
		s_gate_active = false;
		s_last_engine_finish_ms = monotonic_milliseconds();
	}
	schedule_dispatch(K_ENGINE_COOLDOWN_MS);
}

void
ExeRunner::release_engine_gate()
{
	// 原子 exchange：stop()/析构/正常 finished/迟到 queued lambda 可能并发
	// 归还同一个 gate，exchange 保证只有第一个调用真正释放全局 gate。
	if (!m_gate_acquired.exchange(false))
	{
		return;
	}
	release_global_gate();
}

void
ExeRunner::remove_pending_engine(ExeRunner *v_runner)
{
	const QMutexLocker locker(&s_gate_mutex);
	for (qsizetype index = s_pending_starts.size() - 1; index >= 0; --index)
	{
		const PendingStart &pending = s_pending_starts.at(index);
		if (pending.m_runner.isNull() || pending.m_runner.data() == v_runner)
		{
			s_pending_starts.removeAt(index);
		}
	}
}

void
ExeRunner::pause()
{
	if (m_state != ProcessState::RUNNING
		|| m_process->state() == QProcess::NotRunning)
	{
		return;
	}

#ifdef Q_OS_WIN
	const qint64 pid = m_process->processId();
	if (suspend_process_tree(pid))
	{
		m_state = ProcessState::PAUSED;
		emit state_changed(m_state);
		LOG_INFO(
			QStringLiteral("External process tree paused (PID %1)").arg(pid));
	}
	else
	{
		LOG_ERROR("Failed to suspend external process tree");
	}
#else
	// 非 Windows 平台没有通用的进程挂起接口：保持运行，仅记录日志
	LOG_WARNING("Pause is not supported on this platform");
#endif
}

void
ExeRunner::resume()
{
	if (m_state != ProcessState::PAUSED)
	{
		return;
	}

#ifdef Q_OS_WIN
	if (resume_process_tree())
	{
		m_state = ProcessState::RUNNING;
		emit state_changed(m_state);
		LOG_INFO("External process tree resumed");
	}
	else
	{
		LOG_ERROR("Failed to resume external process tree");
	}
#else
	m_state = ProcessState::RUNNING;
	emit state_changed(m_state);
#endif
}

void
ExeRunner::stop()
{
	if (m_queued_start.load())
	{
		// 还在全局队列中等待（尚未真正启动）：取消排队，按"用户停止"收尾，
		// 保证停止后不会再启动引擎（既有语义不回退）。
		LOG_INFO("Cancelling queued external process start");
		remove_pending_engine(this);
		m_queued_start.store(false);
		// 关键：dispatch_pending_engines() 可能已经取出本对象、置
		// m_gate_acquired=true 并把 queued 启动投递到事件循环；此时若本对象
		// 随后被析构，Qt 会丢弃该 metacall，gate 将永久为 true（task-38 P1）。
		// 取消方必须在这里归还 gate；原子 exchange 保证与迟到的 queued
		// lambda 不会重复释放。
		release_engine_gate();
		reset_state();
		m_state = ProcessState::IDLE;
		emit state_changed(m_state);
		emit progress_changed(0);
		emit stopped();
		return;
	}

	if (m_state == ProcessState::IDLE
		&& m_process->state() == QProcess::NotRunning)
	{
		return;
	}

	LOG_INFO("Stopping external process...");
	m_stopping = true;

#ifdef Q_OS_WIN
	if (m_state == ProcessState::PAUSED && !m_suspended_handles.isEmpty())
	{
		LOG_INFO("Resuming paused processes before termination");
		resume_process_tree();
	}
#endif

	if (m_process->state() != QProcess::NotRunning)
	{
#ifdef Q_OS_WIN
		// Job Object（KILL_ON_JOB_CLOSE）：先终止整个作业，覆盖引擎可能派生的
		// 子进程；失败只记录 WARN，仍走下面的进程树/单进程 kill。
		terminate_job();
		// pid 只在 Windows 的进程树终止逻辑里用到，避免非 Windows 平台
		// 编译出 unused variable 警告。
		const qint64 pid = m_process->processId();
		const QList<qint64> child_pids = get_child_processes(pid);
		for (qint64 child_pid : child_pids)
		{
			HANDLE child_handle = OpenProcess(PROCESS_TERMINATE, FALSE,
											  static_cast<DWORD>(child_pid));
			if (child_handle != NULL)
			{
				TerminateProcess(child_handle, 1);
				CloseHandle(child_handle);
			}
		}
		close_suspended_handles();
#else
		// POSIX：子进程在独立进程组中，kill 整个进程组（引擎的子进程不残留）。
		const qint64 pid = m_process->processId();
		if (pid > 0)
		{
			(void)::kill(-static_cast<pid_t>(pid), SIGKILL);
		}
#endif
		m_process->kill();
	}

	// 同步等待进程真正结束再清状态：否则 stop 后立刻 start 新任务时，
	// 旧进程迟到的 finished 会把新任务误判为失败（P2「停止→立即开始」竞态）。
	bool terminated = wait_for_finished(K_SHUTDOWN_WAIT_MS);
	if (!terminated)
	{
		// 极端情况下 kill 后仍未退出：断开旧信号，避免污染后续任务；
		// 进程句柄随 QProcess 析构回收（调用方此时不应再 start）。
		LOG_ERROR("External process did not terminate after kill; signals "
				  "detached");
		m_process->disconnect(this);
	}

#ifdef Q_OS_WIN
	// 无论是否等到退出都关闭作业：KILL_ON_JOB_CLOSE 会再次强制清理残留引擎。
	close_job();
	if (!terminated && m_process->state() != QProcess::NotRunning)
	{
		// Job Object 关闭是强制终止：给进程最后一次短暂退出机会。
		terminated = m_process->waitForFinished(1000);
	}
#endif
	if (terminated)
	{
		release_engine_gate();
	}
	else
	{
		// 旧引擎仍存活：绝不释放 gate（否则会破坏"同一时刻最多一个引擎"的
		// 串行/TDR 不变量）。本 ExeRunner 已无法再启动（QProcess 仍 Running），
		// 后续任务会排队直到该进程退出/应用重启；这里保留 gate 并在日志中
		// 明确记录，作为安全检查优先于可用性的兜底策略。
		LOG_ERROR("Engine still alive after stop timeout; global engine gate "
				  "is retained to keep the serial-engine invariant");
		// 调用方（worker engine_loop / ProcessService）在等 stopped；进程虽未
		// 退出但已断开信号，必须给出终止通知，否则 UI/worker 会一直等待。
		m_stopping = false;
		emit stopped();
	}
	reset_state();
	m_state = ProcessState::IDLE;
	emit state_changed(m_state);
	emit progress_changed(0);
}

bool
ExeRunner::wait_for_finished(int v_timeout_ms)
{
	if (m_process->state() == QProcess::NotRunning)
	{
		return true;
	}

	const int slice_ms = 100;
	int waited_ms = 0;
	while (m_process->state() != QProcess::NotRunning
		   && waited_ms < v_timeout_ms)
	{
		m_process->waitForFinished(slice_ms);
		waited_ms += slice_ms;
	}
	return m_process->state() == QProcess::NotRunning;
}

ProcessState
ExeRunner::state() const
{
	return m_state;
}

bool
ExeRunner::is_running() const
{
	return m_queued_start.load() || m_process->state() != QProcess::NotRunning;
}

int
ExeRunner::progress() const
{
	return m_progress;
}

qint64
ExeRunner::process_id() const
{
	return m_process->processId();
}

QStringList
ExeRunner::failures() const
{
	return m_failures;
}

void
ExeRunner::reset_state()
{
	m_progress = 0;
	m_current_file.clear();
	m_failures.clear();
	m_line_buffer.clear();
	m_stopping = false;
	m_finished_emitted = false;
}

void
ExeRunner::emit_finished_once(bool v_process_ok, int v_exit_code,
							  const QStringList &v_failures)
{
	if (m_finished_emitted)
	{
		return;
	}
	m_finished_emitted = true;
	m_failures = v_failures;
	emit finished(v_process_ok, v_exit_code, m_failures);
}

void
ExeRunner::consume_buffer(bool v_flush_partial)
{
	qsizetype newline_index = m_line_buffer.indexOf(QLatin1Char('\n'));
	while (newline_index >= 0)
	{
		const QString line = m_line_buffer.left(newline_index);
		m_line_buffer.remove(0, newline_index + 1);

		const ExeOutputParser::Result parsed
			= ExeOutputParser::parse_line(line);
		if (parsed.m_progress && *parsed.m_progress != m_progress)
		{
			m_progress = *parsed.m_progress;
			emit progress_changed(m_progress);
		}
		if (parsed.m_file && *parsed.m_file != m_current_file)
		{
			m_current_file = *parsed.m_file;
			emit current_file_changed(m_current_file);
		}
		if (!parsed.m_failures.isEmpty())
		{
			for (const QString &failure : parsed.m_failures)
			{
				if (!m_failures.contains(failure))
				{
					m_failures.append(failure);
					LOG_ERROR("External process failure marker: " + failure);
				}
			}
		}

		newline_index = m_line_buffer.indexOf(QLatin1Char('\n'));
	}

	if (v_flush_partial && !m_line_buffer.isEmpty())
	{
		const ExeOutputParser::Result parsed
			= ExeOutputParser::parse_line(m_line_buffer);
		m_line_buffer.clear();
		if (parsed.m_progress && *parsed.m_progress != m_progress)
		{
			m_progress = *parsed.m_progress;
			emit progress_changed(m_progress);
		}
		if (!parsed.m_failures.isEmpty())
		{
			for (const QString &failure : parsed.m_failures)
			{
				if (!m_failures.contains(failure))
				{
					m_failures.append(failure);
					LOG_ERROR("External process failure marker: " + failure);
				}
			}
		}
	}
}

void
ExeRunner::on_process_started()
{
	LOG_INFO(QStringLiteral("External process started (PID: %1)")
				 .arg(m_process->processId()));
#ifdef Q_OS_WIN
	// 把引擎进程纳入 Job Object（KILL_ON_JOB_CLOSE）：应用退出/停止/析构/
	// kill 超时时强制清理，避免孤儿/挂死引擎进程在 TDR 后持续占用 GPU。
	// 分配失败（如应用自身已在不容许 breakaway 的作业中）只 WARN，保留原有
	// 进程树 kill 路径，不能因此阻止引擎启动。
	create_job_for_current_process();
#endif
}

void
ExeRunner::on_process_ready_read()
{
	const QByteArray chunk = m_process->readAllStandardOutput();
	if (chunk.isEmpty())
	{
		return;
	}
	m_line_buffer += QString::fromUtf8(chunk);
	if (m_line_buffer.size() > K_MAX_LINE_BUFFER_SIZE)
	{
		LOG_WARNING(QStringLiteral("External process output line exceeds %1 "
								   "characters; truncating line buffer")
						.arg(K_MAX_LINE_BUFFER_SIZE));
		m_line_buffer = m_line_buffer.right(4096);
	}
	consume_buffer(false);
}

void
ExeRunner::on_process_finished(int v_exit_code,
							   QProcess::ExitStatus v_exit_status)
{
	// 读取剩余输出并解析掉最后半行
	const QByteArray remaining = m_process->readAllStandardOutput();
	if (!remaining.isEmpty())
	{
		m_line_buffer += QString::fromUtf8(remaining);
	}
	consume_buffer(true);

#ifdef Q_OS_WIN
	close_suspended_handles();
	close_job();
#endif
	// 进程已结束：释放全局 gate，并允许排队请求在冷却后启动。
	release_engine_gate();

	if (m_stopping)
	{
		m_stopping = false;
		emit stopped();
		LOG_INFO("External process stopped by user");
		return;
	}

	const bool process_ok
		= (v_exit_status == QProcess::NormalExit && v_exit_code == 0);
	if (process_ok)
	{
		m_state = ProcessState::COMPLETED;
		LOG_INFO("External process completed (exit code 0)");
	}
	else
	{
		m_state = ProcessState::FAILED;
		LOG_ERROR(QStringLiteral("External process failed (exit code %1, "
								 "crash=%2)")
					  .arg(v_exit_code)
					  .arg(v_exit_status == QProcess::CrashExit ? 1 : 0));
	}

	emit state_changed(m_state);
	emit_finished_once(process_ok, v_exit_code, m_failures);
}

void
ExeRunner::on_process_error_occurred(QProcess::ProcessError v_error)
{
	if (m_stopping)
	{
		return;
	}

	switch (v_error)
	{
		case QProcess::FailedToStart:
		{
#ifdef Q_OS_WIN
			close_job();
#endif
			release_engine_gate();
			const QString error_msg
				= tr("无法启动外部程序（可执行文件缺失或没有执行权限）");
			m_state = ProcessState::FAILED;
			emit start_failed(error_msg);
			emit state_changed(m_state);
			m_failures.append(error_msg);
			emit_finished_once(false, -1, m_failures);
			LOG_ERROR(error_msg);
			break;
		}
		case QProcess::Crashed:
			// 崩溃码会随 finished() 一起到达，这里只记录（模型缺失典型表现）
			LOG_ERROR("External process crashed (可能原因：模型文件缺失 / "
					  "显存不足)");
			m_failures.append(
				tr("外部程序崩溃（可能原因：模型文件缺失 / 显存不足）"));
			break;
		default:
			LOG_WARNING(QStringLiteral("External process error: %1")
							.arg(static_cast<int>(v_error)));
			break;
	}
}

#ifdef Q_OS_WIN
QList<qint64>
ExeRunner::get_child_processes(qint64 v_parent_pid)
{
	QList<qint64> child_pids;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		LOG_ERROR(QStringLiteral("CreateToolhelp32Snapshot failed: %1")
					  .arg(GetLastError()));
		return child_pids;
	}

	PROCESSENTRY32 process_entry;
	process_entry.dwSize = sizeof(PROCESSENTRY32);
	if (Process32First(snapshot, &process_entry))
	{
		do
		{
			if (process_entry.th32ParentProcessID
				== static_cast<DWORD>(v_parent_pid))
			{
				child_pids.append(process_entry.th32ProcessID);
				child_pids.append(
					get_child_processes(process_entry.th32ProcessID));
			}
		} while (Process32Next(snapshot, &process_entry));
	}

	CloseHandle(snapshot);
	return child_pids;
}

bool
ExeRunner::suspend_process_tree(qint64 v_pid)
{
	if (v_pid <= 0)
	{
		return false;
	}

	QList<qint64> all_pids;
	all_pids.append(v_pid);
	all_pids.append(get_child_processes(v_pid));

	typedef NTSTATUS(NTAPI * NtSuspendProcessFn)(HANDLE);
	HMODULE ntdll_handle = GetModuleHandleW(L"ntdll.dll");
	if (ntdll_handle == NULL)
	{
		LOG_ERROR("Failed to get ntdll.dll handle");
		return false;
	}
	NtSuspendProcessFn nt_suspend = reinterpret_cast<NtSuspendProcessFn>(
		GetProcAddress(ntdll_handle, "NtSuspendProcess"));
	if (nt_suspend == nullptr)
	{
		LOG_ERROR("Failed to resolve NtSuspendProcess");
		return false;
	}

	close_suspended_handles();

	bool any_suspended = false;
	for (qint64 pid : all_pids)
	{
		HANDLE process_handle
			= OpenProcess(PROCESS_SUSPEND_RESUME | PROCESS_TERMINATE, FALSE,
						  static_cast<DWORD>(pid));
		if (process_handle == NULL)
		{
			LOG_WARNING(
				QStringLiteral("Failed to open PID %1 for suspend").arg(pid));
			continue;
		}
		if (nt_suspend(process_handle) == 0)
		{
			m_suspended_handles.append(process_handle);
			any_suspended = true;
		}
		else
		{
			CloseHandle(process_handle);
		}
	}

	return any_suspended;
}

bool
ExeRunner::resume_process_tree()
{
	typedef NTSTATUS(NTAPI * NtResumeProcessFn)(HANDLE);
	HMODULE ntdll_handle = GetModuleHandleW(L"ntdll.dll");
	if (ntdll_handle == NULL)
	{
		LOG_ERROR("Failed to get ntdll.dll handle");
		return false;
	}
	NtResumeProcessFn nt_resume = reinterpret_cast<NtResumeProcessFn>(
		GetProcAddress(ntdll_handle, "NtResumeProcess"));
	if (nt_resume == nullptr)
	{
		LOG_ERROR("Failed to resolve NtResumeProcess");
		return false;
	}

	bool all_resumed = true;
	for (void *raw_handle : m_suspended_handles)
	{
		HANDLE process_handle = static_cast<HANDLE>(raw_handle);
		if (nt_resume(process_handle) != 0)
		{
			all_resumed = false;
		}
		CloseHandle(process_handle);
	}
	m_suspended_handles.clear();

	return all_resumed;
}

void
ExeRunner::close_suspended_handles()
{
	for (void *raw_handle : m_suspended_handles)
	{
		CloseHandle(static_cast<HANDLE>(raw_handle));
	}
	m_suspended_handles.clear();
}
#endif // Q_OS_WIN

#ifdef Q_OS_WIN
void
ExeRunner::create_job_for_current_process()
{
	close_job();

	HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
	if (job == nullptr)
	{
		LOG_WARNING(QStringLiteral("CreateJobObject failed (%1), fallback to "
								   "process-tree kill")
						.arg(::GetLastError()));
		return;
	}

	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
	limits.BasicLimitInformation.LimitFlags
		= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!::SetInformationJobObject(job, JobObjectExtendedLimitInformation,
								   &limits, sizeof(limits)))
	{
		LOG_WARNING(QStringLiteral("SetInformationJobObject failed (%1), "
								   "fallback to process-tree kill")
						.arg(::GetLastError()));
		::CloseHandle(job);
		return;
	}

	const DWORD pid = static_cast<DWORD>(m_process->processId());
	HANDLE process = ::OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE
									   | PROCESS_QUERY_LIMITED_INFORMATION,
								   FALSE, pid);
	if (process == nullptr)
	{
		LOG_WARNING(QStringLiteral("OpenProcess for Job Object failed (%1), "
								   "fallback to process-tree kill")
						.arg(::GetLastError()));
		::CloseHandle(job);
		return;
	}

	if (!::AssignProcessToJobObject(job, process))
	{
		LOG_WARNING(QStringLiteral("AssignProcessToJobObject failed (%1), "
								   "fallback to process-tree kill")
						.arg(::GetLastError()));
		::CloseHandle(process);
		::CloseHandle(job);
		return;
	}

	::CloseHandle(process);
	m_job_handle = job;
}

void
ExeRunner::terminate_job()
{
	if (m_job_handle == nullptr)
	{
		return;
	}
	if (!::TerminateJobObject(m_job_handle, 1))
	{
		LOG_WARNING(QStringLiteral("TerminateJobObject failed (%1)")
						.arg(::GetLastError()));
	}
}

void
ExeRunner::close_job()
{
	if (m_job_handle == nullptr)
	{
		return;
	}
	// KILL_ON_JOB_CLOSE：关闭句柄会强制终止作业内仍存活的进程（孤儿清理）。
	::CloseHandle(m_job_handle);
	m_job_handle = nullptr;
}
#endif
