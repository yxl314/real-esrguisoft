#ifndef BATCHPROGRESSWATCHER_H
#define BATCHPROGRESSWATCHER_H

// ============================================================================
// 模型层 - 批量进度观察器
//
// 批量处理由外部 exe 一次性完成（无法获知内部逐文件进度），因此通过
// 轮询输出目录中「新增 / 变化」的文件数量来估算已处理文件数。
// 与 ProcessService 解耦：只负责探测与上报，不参与流程编排。
// ============================================================================

#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTimer>

#include "mediasettings.h"

class BatchProgressWatcher : public QObject
{
	Q_OBJECT

  public:
	struct FileSnapshot
	{
		qint64 m_size = 0;
		QDateTime m_last_modified;
	};

	explicit BatchProgressWatcher(QObject *v_parent = nullptr);

	// 开始观察指定输出目录（totalFiles 为期望总文件数）
	void
	start(const QString &v_output_dir, int v_total_files, MediaKind v_kind);
	void
	stop();

  signals:
	// 探测到的已处理文件数发生变化时发射
	void
	processed_count_changed(int v_processed_count);

  private slots:
	void
	on_timeout();

  private:
	void
	record_initial_file_state();
	int
	detect_processed_file_count() const;

	static constexpr int K_POLL_INTERVAL_MS = 200;

	QTimer m_timer;
	QString m_output_dir;
	int m_total_files = 0;
	MediaKind m_kind = MediaKind::IMAGE;
	QMap<QString, FileSnapshot> m_initial_file_state;
	int m_last_reported_count = 0;
};

#endif // BATCHPROGRESSWATCHER_H