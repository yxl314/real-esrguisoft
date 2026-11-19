#ifndef STACKLOG_EXAMPLE_QT_WORKER_HPP
#define STACKLOG_EXAMPLE_QT_WORKER_HPP

#include <QObject>
#include <stacklog/stacklog.hpp>

///
/// \brief 在工作线程中执行日志写入的示例对象。
///
class worker : public QObject
{
  Q_OBJECT

public:
  ///
  /// \brief 接收主线程捕获的追踪帧。
  ///
  explicit worker(const stacklog::trace_frame& frame,
                  QObject* parent = nullptr);

signals:
  ///
  /// \brief 工作线程任务结束。
  ///
  void finished();

public slots:
  ///
  /// \brief 由 QThread::started 触发。
  ///
  void run();

private:
  stacklog::trace_frame frame_;
};

#endif // STACKLOG_EXAMPLE_QT_WORKER_HPP
