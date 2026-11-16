#ifndef STACKLOG_EXAMPLE_QT_WORKER_HPP
#define STACKLOG_EXAMPLE_QT_WORKER_HPP

#include <stacklog/stacklog.hpp>

#include <QObject>

/// 在工作线程中执行日志写入的示例对象。
class worker : public QObject
{
    Q_OBJECT

public:
    /// 接收主线程捕获的追踪帧。
    explicit worker(const stacklog::trace_frame& frame, QObject* parent = nullptr);

signals:
    /// 工作线程任务结束。
    void finished();

public slots:
    /// 由 QThread::started 触发。
    void run();

private:
    stacklog::trace_frame frame_;
};

#endif // STACKLOG_EXAMPLE_QT_WORKER_HPP
