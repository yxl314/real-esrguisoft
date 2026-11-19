#include "worker.h"

#include <QThread>

worker::worker(const stacklog::trace_frame& frame, QObject* parent)
  : QObject(parent), frame_(frame)
{
}

void
worker::run()
{
  // 显式接续主线程 trace，再创建 worker 子节点。
  STACKLOG_RESTORE_TRACE(frame_, "worker-job");
  STACKLOG_INFO() << "worker thread started";

  for (int index = 0; index < 60; ++index)
    {
      STACKLOG_INFO() << "worker line " << index;
      QThread::msleep(3);
    }

  {
    STACKLOG_TRACE("worker-sub");
    STACKLOG_INFO() << "worker sub scope";
  }

  STACKLOG_INFO() << "worker thread finished";
  emit finished();
}
