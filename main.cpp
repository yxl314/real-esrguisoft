#include "appcontroller.h"
#include "language_manager.h"
#include "logger.h"
#include "mainwindow.h"
#include "qtdialogservice.h"

#include <QApplication>
#include <QDir>
#include <QTimer>
#include <memory>
#include <singleproc/singleproc.hpp>

namespace
{

/// 应用唯一 key，用于单实例锁与二次启动门铃。
constexpr const char *kInstanceKey = "real-esrguisoft";

/// 门铃轮询间隔；POSIX 与 Windows 共用 QTimer，避免平台相关的 fd 接入。
constexpr int kDoorbellPollIntervalMs = 200;

/// 受限环境下的单实例锁目录：程序目录下的 singleproc/。
///
/// 库默认使用 XDG_RUNTIME_DIR / %TEMP%；沙箱或只读临时目录里可能不可写。
/// 这时退回程序目录，保证单实例判定在受限环境下依然可用。
QString
fallback_runtime_dir()
{
	const QString directory = QCoreApplication::applicationDirPath()
							  + QStringLiteral("/singleproc");
	(void)QDir().mkpath(directory);
	return directory;
}

} // namespace

int
main(int argc, char *argv[])
{
	QApplication a(argc, argv);

	// QSettings 统一使用 QCoreApplication 的组织/应用身份（QSettings 默认构造），
	// 生产与测试保持同一路径；测试通过 setDefaultFormat + setPath 隔离到临时目录。
	QCoreApplication::setOrganizationName(QStringLiteral("real-esrguisoft"));
	QCoreApplication::setApplicationName(QStringLiteral("real-esrguisoft"));

	// 版本号由 CMakeLists.txt 的 project(VERSION ...) 经 RESR_APP_VERSION 注入，
	// 关于页等界面读取 QCoreApplication::applicationVersion()，避免源码硬编码漂移。
	a.setApplicationVersion(QString::fromLatin1(RESR_APP_VERSION));

	// 界面语言必须在任何界面对象之前初始化（决定所有 tr() 的初始取词），
	// 顺序：RESR_UI_LANG 钩子 > QSettings(ui/language) > 跟随系统。
	LanguageManager::instance().initialize();

	// 单实例判定放在 QApplication 构造之后、界面构造之前。
	singleproc::Guard guard(kInstanceKey);
	std::unique_ptr<singleproc::Guard> fallback_guard;
	if (guard.state() == singleproc::Guard::State::Error)
	{
		fallback_guard.reset(new singleproc::Guard(
			kInstanceKey, fallback_runtime_dir().toStdString()));
	}
	singleproc::Guard *active_guard
		= fallback_guard ? fallback_guard.get() : &guard;

	if (active_guard->state() == singleproc::Guard::State::Error)
	{
		LOG_ERROR(QStringLiteral("Single instance check failed (error code %1)")
					  .arg(active_guard->error_code()));
		return 1;
	}
	if (!active_guard->is_primary())
	{
		// 二次实例：门铃已在 Guard 构造时发出，这里只记录并退出。
		LOG_INFO(QStringLiteral(
			"Another instance is already running, notified it and exiting"));
		return 0;
	}

	// 主题与全局样式表由 ThemeManager 在 MainWindow 构造时统一初始化
	// （QSS 模板做 {{token}} 替换），这里不再加载旧的 :/style.qss。
	QtDialogService dialogs;
	MainWindow w;
	AppController controller(&w, &dialogs);
	controller.initialize();

	w.show();

	// 二次启动：门铃回调在主线程由定时器分发，只负责把已有窗口前置。
	active_guard->set_doorbell_handler(
		[&w]()
		{
			w.showNormal();
			w.raise();
			w.activateWindow();
			LOG_INFO(QStringLiteral(
				"Second instance detected, activating main window"));
		});

	QTimer doorbell_timer;
	QObject::connect(&doorbell_timer, &QTimer::timeout, &a,
					 [active_guard]() { active_guard->process_doorbell(0); });
	doorbell_timer.start(kDoorbellPollIntervalMs);

	const int exit_code = a.exec();

	// 主实例退出前关闭日志文件，确保缓冲内容落盘。
	Logger::instance()->shutdown();
	return exit_code;
}
