#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <stacklog/stacklog.hpp>
#include <string>

int
main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);

  const QString data_dir
      = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (data_dir.isEmpty() || !QDir().mkpath(data_dir))
    {
      return 1;
    }

  stacklog::config cfg;
  cfg.dir = std::string(data_dir.toUtf8().constData());
  cfg.base_name = "qt_demo";
  cfg.min_level = stacklog::level::info;

  if (!stacklog::init(cfg))
    {
      return 1;
    }

  STACKLOG_TRACE("qt-main");
  STACKLOG_INFO() << "qt demo started: " << QString("stacklog").toStdString();
  stacklog::shutdown();
  return 0;
}
