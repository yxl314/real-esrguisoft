#ifndef IMAGEPROCESSSERVICE_H
#define IMAGEPROCESSSERVICE_H

// ============================================================================
// 模型层 - 图片原生处理服务
//
// 直接把用户设置翻译成 realesrgan-ncnn-vulkan 调用（不再经过 Python 可执行
// 程序），完整继承旧 Python 编排层行为：
//   - 单文件：{stem}_{输入扩展名}_{model}[_x].{输出扩展名}；引擎直接写目标文件；
//   - 批量：输出目录 {输入目录名}_{model}[_x]/，引擎目录模式一次处理整批
//     （与 Python image_batch 一致），BatchProgressWatcher 轮询输出目录算进度；
//   - BMP / GIF 等引擎不支持的输入：先用 QImage 解码并转成临时 PNG 再送引擎，
//     该批次退回逐文件模式；逐文件失败会聚合（跑完再报"处理不完整"）；
//   - 成功判定不信任引擎退出码：必须「进程正常 + 无失败标记 + 逐输入产物
//     存在且非空、且相对任务开始时的快照确实被更新」（避免输出目录里旧格式
//     残留产物被误计为本次成果）。
// ============================================================================

#include "mediaprocessservice.h"
#include "outputpathresolver.h"
#include "processrequest.h"

#include <QString>
#include <QStringList>

class BatchProgressWatcher;
class ExeRunner;
class QTemporaryDir;

class ImageProcessService : public MediaProcessService
{
	Q_OBJECT

  public:
	explicit ImageProcessService(QObject *v_parent = nullptr);
	~ImageProcessService() override;

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

  private slots:
	void
	on_runner_progress_changed(int v_percent);
	void
	on_runner_state_changed(ProcessState v_state);
	void
	on_runner_finished(bool v_process_ok, int v_exit_code,
					   const QStringList &v_failures);
	void
	on_watcher_count_changed(int v_processed_count);

  private:
	// 当前任务形态：单文件 / 批量目录模式 / 批量逐文件模式
	enum class JobMode
	{
		NONE,
		SINGLE,
		BATCH_DIRECTORY,
		BATCH_PER_FILE
	};

	void
	reset_job();
	void
	begin_single();
	void
	begin_batch();
	void
	begin_next_batch_file();
	void
	start_engine(const QString &v_input_path, const QString &v_output_path,
				 const QString &v_output_format);
	// 启动一次引擎调用；v_safe_retry=true 时使用崩溃降级参数（-t 128 / -j 1:1:1），
	// 仅用于"引擎异常退出后自动重试一次"，不写回用户设置。
	void
	start_engine_attempt(bool v_safe_retry);
	// 引擎异常退出（0xC0000409/crash）且本次任务尚未重试过时，返回 true。
	bool
	engine_crash_retry_available(bool v_process_ok, int v_exit_code) const;
	// 已重试仍失败时在原因前加"已降级安全参数重试"的可翻译说明。
	QString
	with_engine_retry_hint(const QString &v_reason) const;
	void
	finish_job(bool v_success, const QString &v_message);
	void
	fail_job(const QString &v_reason);
	void
	publish_progress(int v_percent);
	void
	publish_counts(int v_processed);
	// 批量逐文件模式：汇总本轮成功/失败并结束任务（失败为"处理不完整"）
	void
	finish_batch_per_file();
	// 把任意图片输入转成引擎可解码的路径（必要时用 QImage 转临时 PNG）
	QString
	engine_input_for(const QString &v_file_path);
	// 批量逐文件模式下单个输入对应的输出文件路径（与引擎目录模式命名一致）
	QString
	batch_output_file_path(const QString &v_input_file);
	// 批量图片「同 stem 不同扩展名」输入可能映射到同一产物名（例如 a.bmp 与
	// a.png 都写 a.png；显式 JPG 下 a.jpg 与 a.jpeg 都写 a.jpg）。启动前按
	// 实际产物名检测冲突，返回可展示的错误；空串表示无冲突。
	// 目录模式传 batch_expected_settings()，逐文件模式传 m_request.m_settings。
	QString
	batch_output_collision_error(const MediaSettings &v_settings) const;
	// 批量目录模式「期望产物」使用的设置：引擎目录模式未显式 -f 时统一输出
	// PNG（即使输入是 jpg/webp），成功判定必须与引擎实际格式一致。
	MediaSettings
	batch_expected_settings() const;
	void
	cleanup_temp();
	// 引擎先写同目录隐藏临时文件，成功后再改名到最终路径：失败/取消不会
	// 截断或覆盖上一次的成品（P2 数据完整性）。批量目录模式仍直接写目录。
	QString
	staged_output_path(const QString &v_final_path) const;
	bool
	commit_staged_output(const QString &v_final_path, QString *v_error);
	// 只删除暂存文件、保留 m_staged_output_path：崩溃降级重试仍写同一路径，
	// 重试成功后才能正常提交（仅 clear 路径会让成功重试被判失败并遗留孤儿）。
	void
	remove_staged_output_file();
	// 批量目录模式崩溃重试前：清理本次任务新增的期望产物（任务开始快照之外
	// 的半成品），不删除任务开始前已存在的旧成品/用户文件。
	void
	cleanup_crashed_batch_outputs();
	void
	discard_staged_output();

	ExeRunner *m_runner;
	BatchProgressWatcher *m_watcher;
	QString m_bin_directory;
	ProcessRequest m_request;
	JobMode m_job_mode = JobMode::NONE;
	ProcessState m_state = ProcessState::IDLE;
	int m_progress = 0;
	int m_total_files = 0;
	int m_processed_files = 0;
	int m_batch_index = 0;
	int m_batch_success_count = 0;
	QString m_current_file;
	QString m_output_path;		   // 单文件为输出文件；批量为输出目录
	QString m_current_output_file; // 批量逐文件模式下当前期望的产物
	QStringList m_pending_files;
	QStringList m_needs_convert; // 需要 QImage 预转 PNG 的输入（bmp/gif 等）
	// 批量逐文件模式的失败聚合（全部处理完再统一判定）
	// 引擎崩溃降级重试：每次引擎调用最多一次，参数只覆盖本次调用。
	bool m_engine_retry_used = false;
	QString m_engine_program;
	QString m_engine_input;
	QString m_engine_output;
	QString m_engine_format;
	QStringList m_batch_failed_files;
	QStringList m_batch_failed_details;
	// 产物校验快照：批量目录模式=整批；逐文件模式=当前文件
	OutputPathResolver::OutputSnapshotMap m_output_snapshot;
	// 当前单文件 / 逐文件任务中引擎实际写入的临时产物路径
	QString m_staged_output_path;
	bool m_cancelled = false;
	QTemporaryDir *m_temp_dir = nullptr;
};

#endif // IMAGEPROCESSSERVICE_H
