#include "appsettings.h"

#include <QSettings>

namespace
{

// QSettings 键：超分引擎路径（空 = 自动发现）
const char *const K_ENGINE_PATH_KEY = "engine/path";

QSettings
make_settings()
{
	// 使用 main.cpp 统一设置的 QCoreApplication 组织/应用身份。
	return QSettings();
}

} // namespace

namespace AppSettings
{

QString
engine_path()
{
	QSettings settings = make_settings();
	return settings.value(QString::fromLatin1(K_ENGINE_PATH_KEY))
		.toString()
		.trimmed();
}

void
set_engine_path(const QString &v_path)
{
	QSettings settings = make_settings();
	const QString path = v_path.trimmed();
	if (path.isEmpty())
	{
		settings.remove(QString::fromLatin1(K_ENGINE_PATH_KEY));
		return;
	}
	settings.setValue(QString::fromLatin1(K_ENGINE_PATH_KEY), path);
}

} // namespace AppSettings
