#include "batchprogresswatcher.h"

#include "fileservice.h"
#include "logger.h"

#include <QDir>
#include <QFileInfo>

BatchProgressWatcher::BatchProgressWatcher(QObject *v_parent)
	: QObject(v_parent)
{
	m_timer.setInterval(K_POLL_INTERVAL_MS);
	connect(&m_timer, &QTimer::timeout, this,
			&BatchProgressWatcher::on_timeout);
}

void
BatchProgressWatcher::start(const QString &v_output_dir, int v_total_files,
							MediaKind v_kind)
{
	m_output_dir = v_output_dir;
	m_total_files = v_total_files;
	m_kind = v_kind;
	m_initial_file_state.clear();
	m_last_reported_count = 0;
	record_initial_file_state();
	m_timer.start();
}

void
BatchProgressWatcher::stop()
{
	m_timer.stop();
}

void
BatchProgressWatcher::record_initial_file_state()
{
	if (m_output_dir.isEmpty())
	{
		return;
	}

	QDir output_dir(m_output_dir);
	if (!output_dir.exists())
	{
		return;
	}

	const QStringList files = FileService::files_in_directory(
		m_output_dir, FileService::filters(m_kind));

	for (const QString &file_path : files)
	{
		const QFileInfo info(file_path);
		FileSnapshot snapshot;
		snapshot.m_size = info.size();
		snapshot.m_last_modified = info.lastModified();
		m_initial_file_state[file_path] = snapshot;
	}

	LOG_INFO(QStringLiteral("Recorded initial state: %1 files in output folder")
				 .arg(files.size()));
}

int
BatchProgressWatcher::detect_processed_file_count() const
{
	if (m_output_dir.isEmpty())
	{
		return 0;
	}

	QDir output_dir(m_output_dir);
	if (!output_dir.exists())
	{
		return 0;
	}

	const QStringList current_files = FileService::files_in_directory(
		m_output_dir, FileService::filters(m_kind));

	int processed_count = 0;
	for (const QString &file_path : current_files)
	{
		const QFileInfo info(file_path);
		const auto it = m_initial_file_state.constFind(file_path);
		if (it == m_initial_file_state.constEnd())
		{
			++processed_count; // 新增文件
		}
		else if (info.size() != it->m_size
				 || info.lastModified() != it->m_last_modified)
		{
			++processed_count; // 已变化文件
		}
	}
	return processed_count;
}

void
BatchProgressWatcher::on_timeout()
{
	if (m_output_dir.isEmpty() || m_total_files == 0)
	{
		return;
	}

	int processed_count = detect_processed_file_count();
	if (processed_count > m_total_files)
	{
		processed_count = m_total_files;
	}

	if (processed_count != m_last_reported_count)
	{
		m_last_reported_count = processed_count;
		emit processed_count_changed(processed_count);
	}
}