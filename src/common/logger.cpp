#include "logger.h"

#include <stacklog/stacklog.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QMutexLocker>
#include <cstdint>
#include <iostream>
#include <string>

namespace
{

/// 单个日志文件的大小上限：5 MB。
constexpr std::uint64_t kMaxFileSize = 5ULL * 1024 * 1024;

/// 保留的归档日志份数。
constexpr unsigned kMaxFiles = 5;

/// 日志基础名；文件为 logs/real-esrguisoft.log 及其 .1、.2 ... 归档。
constexpr const char *kBaseName = "real-esrguisoft";

/// 把旧 LogLevel 映射为 stacklog 级别。
stacklog::level
to_stacklog_level(LogLevel v_level)
{
	switch (v_level)
	{
		case LogLevel::DEBUG:
			return stacklog::level::debug;
		case LogLevel::INFO:
			return stacklog::level::info;
		case LogLevel::WARNING:
			return stacklog::level::warn;
		case LogLevel::ERROR:
			return stacklog::level::error;
		default:
			return stacklog::level::info;
	}
}

/// 把相对日志目录解析为基于程序目录的绝对路径。
QString
resolve_log_dir(const QString &v_dir)
{
	QString base = QCoreApplication::applicationDirPath();
	if (base.isEmpty())
	{
		base = QDir::currentPath();
	}
	if (QDir::isRelativePath(v_dir))
	{
		return QDir(base).filePath(v_dir);
	}
	return v_dir;
}

} // namespace

Logger *Logger::g_m_instance = nullptr;

Logger::Logger()
	: m_log_level(LogLevel::INFO), m_log_to_file(true), m_log_to_console(true),
	  m_file_sink_ready(false), m_shutdown(false)
{
	m_log_dir = resolve_log_dir(QStringLiteral("logs"));
}

Logger::~Logger()
{
	shutdown();
}

Logger *
Logger::instance()
{
	static QMutex s_init_mutex;
	QMutexLocker locker(&s_init_mutex);
	if (g_m_instance == nullptr)
	{
		g_m_instance = new Logger();
	}
	return g_m_instance;
}

void
Logger::ensure_file_sink()
{
	if (m_file_sink_ready || !m_log_to_file)
	{
		return;
	}

	stacklog::config cfg;
	cfg.dir = m_log_dir.toStdString();
	cfg.base_name = kBaseName;
	cfg.max_file_size = kMaxFileSize;
	cfg.max_files = kMaxFiles;
	cfg.min_level = to_stacklog_level(m_log_level);
	// 失败时 stacklog 内部会降级到 stderr，这里不额外处理。
	(void)stacklog::init(cfg);
	m_file_sink_ready = true;
}

void
Logger::set_log_dir(const QString &v_dir)
{
	QMutexLocker locker(&m_mutex);
	m_log_dir = resolve_log_dir(v_dir);
	// 下次写日志时用新目录重新初始化。
	m_file_sink_ready = false;
}

QString
Logger::log_directory() const
{
	QMutexLocker locker(&m_mutex);
	return m_log_dir;
}

void
Logger::set_log_level(LogLevel v_level)
{
	QMutexLocker locker(&m_mutex);
	m_log_level = v_level;
	stacklog::set_level(to_stacklog_level(v_level));
}

void
Logger::set_log_to_file(bool v_enabled)
{
	QMutexLocker locker(&m_mutex);
	if (m_log_to_file == v_enabled)
	{
		return;
	}
	m_log_to_file = v_enabled;
	m_file_sink_ready = false;
	if (!v_enabled)
	{
		stacklog::shutdown();
	}
}

void
Logger::set_log_to_console(bool v_enabled)
{
	QMutexLocker locker(&m_mutex);
	m_log_to_console = v_enabled;
}

void
Logger::shutdown()
{
	QMutexLocker locker(&m_mutex);
	stacklog::shutdown();
	m_file_sink_ready = false;
	m_shutdown = true;
}

void
Logger::debug(const QString &v_message)
{
	log(LogLevel::DEBUG, v_message);
}

void
Logger::info(const QString &v_message)
{
	log(LogLevel::INFO, v_message);
}

void
Logger::warning(const QString &v_message)
{
	log(LogLevel::WARNING, v_message);
}

void
Logger::error(const QString &v_message)
{
	log(LogLevel::ERROR, v_message);
}

void
Logger::log(LogLevel v_level, const QString &v_message)
{
	log(v_level, v_message, nullptr, 0);
}

void
Logger::log_debug(const QString &v_message, const char *v_file, int v_line)
{
	log(LogLevel::DEBUG, v_message, v_file, v_line);
}

void
Logger::log_info(const QString &v_message, const char *v_file, int v_line)
{
	log(LogLevel::INFO, v_message, v_file, v_line);
}

void
Logger::log_warning(const QString &v_message, const char *v_file, int v_line)
{
	log(LogLevel::WARNING, v_message, v_file, v_line);
}

void
Logger::log_error(const QString &v_message, const char *v_file, int v_line)
{
	log(LogLevel::ERROR, v_message, v_file, v_line);
}

void
Logger::log(LogLevel v_level, const QString &v_message, const char *v_file,
			int v_line)
{
	bool signal_ready = false;

	// 互斥量只保护日志状态/落盘；信号在锁外发射，避免槽函数再次打日志时
	// 与非递归互斥量互相等待（当前 log_message 无连接，保留正确性）。
	{
		QMutexLocker locker(&m_mutex);

		if (v_level < m_log_level)
		{
			return;
		}

		const std::string message = v_message.toStdString();

		if (m_log_to_file && !m_shutdown)
		{
			ensure_file_sink();
			// stacklog 的行对象在析构时落盘，file/line 由宏注入以保证调用点
			// 准确。
			stacklog::detail::log_line line(to_stacklog_level(v_level), v_file,
											v_line);
			line << message;
		}

		if (m_log_to_console)
		{
			const QString timestamp
				= QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
			const QString formatted
				= QString("[%1] [%2] %3")
					  .arg(timestamp, level_to_string(v_level), v_message);
			std::cout << formatted.toStdString() << std::endl;
		}

		signal_ready = true;
	}

	if (signal_ready)
	{
		emit log_message(v_level, v_message);
	}
}

QString
Logger::level_to_string(LogLevel v_level)
{
	switch (v_level)
	{
		case LogLevel::DEBUG:
			return "DEBUG";
		case LogLevel::INFO:
			return "INFO";
		case LogLevel::WARNING:
			return "WARNING";
		case LogLevel::ERROR:
			return "ERROR";
		default:
			return "UNKNOWN";
	}
}
