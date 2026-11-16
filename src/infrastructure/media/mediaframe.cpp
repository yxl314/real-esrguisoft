#include "mediaframe.h"

#include <QDir>

namespace MediaFrameSequence
{

QString
frame_file_name(const QString &v_prefix, const QString &v_extension,
				int v_frame_number)
{
	const QString number
		= QString::number(v_frame_number).rightJustified(8, QLatin1Char('0'));
	return v_prefix + number + QLatin1Char('.') + v_extension;
}

QStringList
list_frame_files(const QString &v_dir, const QString &v_prefix,
				 const QString &v_extension)
{
	QDir directory(v_dir);
	if (!directory.exists())
	{
		return QStringList();
	}

	const QStringList filters{ v_prefix + QStringLiteral("*.") + v_extension };
	// 8 位补零后字典序 == 帧序，无需额外按数字排序
	return directory.entryList(filters, QDir::Files | QDir::Readable,
							   QDir::Name);
}

} // namespace MediaFrameSequence
