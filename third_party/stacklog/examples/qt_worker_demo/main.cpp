#include <stacklog/stacklog.hpp>
#include "worker.h"

#include <string>

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QThread>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("stacklog-qt-worker-demo");
    QCoreApplication::setOrganizationName("stacklog");

    const QString data_dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (data_dir.isEmpty() || !QDir().mkpath(data_dir))
    {
        return 1;
    }

    stacklog::config cfg;
    cfg.dir = std::string(data_dir.toUtf8().constData());
    cfg.base_name = "qt_worker_demo";
    cfg.max_file_size = 4096;
    cfg.max_files = 3;
    cfg.min_level = stacklog::level::debug;

    if (!stacklog::init(cfg))
    {
        return 1;
    }

    stacklog::trace_frame captured;
    {
        STACKLOG_TRACE("qt-main");
        STACKLOG_INFO() << "main thread started";
        captured = stacklog::capture_trace();
    }

    QThread thread;
    worker worker_object(captured);
    worker_object.moveToThread(&thread);

    QObject::connect(&thread, &QThread::started, &worker_object, &worker::run);
    QObject::connect(&worker_object, &worker::finished, &thread, &QThread::quit);
    QObject::connect(&thread, &QThread::finished, &app, &QCoreApplication::quit);

    thread.start();
    const int result = app.exec();
    thread.wait();

    {
        STACKLOG_TRACE("qt-main-end");
        STACKLOG_INFO() << "main thread finished";
    }
    stacklog::shutdown();
    return result;
}
