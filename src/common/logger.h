#ifndef LOGGER_H
#define LOGGER_H

#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTextStream>

/// 日志级别；与旧版 Logger 保持一致，内部映射到 stacklog 的级别。
enum class LogLevel
{
	DEBUG,
	INFO,
	WARNING,
	ERROR
};

/// 日志兼容层：对外仍是原来的 Logger 接口与 LOG_* 宏，底层用 stacklog 落盘。
///
/// - 日志目录默认为 <程序目录>/logs；相对目录按程序目录解析。
/// - 文件名为 <base_name>.log，超过大小后按序号轮转，由 stacklog 负责。
/// - 每条日志都带调用点 文件:行号（由 LOG_* 宏注入 __FILE__/__LINE__）。
/// - 保留旧的控制台 tee 行为（std::cout）。
/// - 线程安全由 stacklog 与内部互斥量保证。
class Logger : public QObject
{
	Q_OBJECT

  public:
	static Logger *
	instance();

	/// 设置日志目录；相对路径基于 QCoreApplication::applicationDirPath()。
	void
	set_log_dir(const QString &v_dir);
	/// 当前日志目录的绝对路径（设置页展示 / 打开目录用）。
	QString
	log_directory() const;
	void
	set_log_level(LogLevel v_level);
	void
	set_log_to_file(bool v_enabled);
	void
	set_log_to_console(bool v_enabled);

	/// 关闭日志文件；可重复调用。主实例退出前应调用一次。
	void
	shutdown();

	void
	debug(const QString &v_message);
	void
	info(const QString &v_message);
	void
	warning(const QString &v_message);
	void
	error(const QString &v_message);

	/// 写一条日志；file/line 由 LOG_* 宏注入，直接调用时可省略（显示为 "-"）。
	void
	log(LogLevel v_level, const QString &v_message);
	void
	log(LogLevel v_level, const QString &v_message, const char *v_file,
		int v_line);

	// 供 LOG_* 宏使用的便捷入口。
	// 不用 LogLevel::ERROR 拼宏：<windows.h> 把 ERROR 定义成错误码宏，
	// 宏展开时会把枚举值替换成 0。
	void
	log_debug(const QString &v_message, const char *v_file, int v_line);
	void
	log_info(const QString &v_message, const char *v_file, int v_line);
	void
	log_warning(const QString &v_message, const char *v_file, int v_line);
	void
	log_error(const QString &v_message, const char *v_file, int v_line);

  signals:
	void
	log_message(LogLevel v_level, const QString &v_message);

  private:
	Logger();
	~Logger();
	Logger(const Logger &) = delete;
	Logger &
	operator=(const Logger &)
		= delete;

	/// 返回级别对应的可读名字。
	QString
	level_to_string(LogLevel v_level);

	/// 确保 stacklog 已按当前配置初始化；失败时返回 false。
	void
	ensure_file_sink();

	static Logger *g_m_instance;
	QString m_log_dir;
	mutable QMutex m_mutex;
	LogLevel m_log_level;
	bool m_log_to_file;
	bool m_log_to_console;
	bool m_file_sink_ready;
	// shutdown() 之后不再重新初始化文件 sink：主程序在退出前关闭日志，
	// 但窗口/控制器析构仍可能写日志，重新 init 会在退出阶段重建日志文件。
	bool m_shutdown = false;
};

#define LOG_DEBUG(msg) Logger::instance()->log_debug((msg), __FILE__, __LINE__)
#define LOG_INFO(msg) Logger::instance()->log_info((msg), __FILE__, __LINE__)
#define LOG_WARNING(msg)                                                       \
	Logger::instance()->log_warning((msg), __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::instance()->log_error((msg), __FILE__, __LINE__)

#endif
