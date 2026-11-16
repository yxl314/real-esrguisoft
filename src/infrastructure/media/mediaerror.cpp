#include "mediaerror.h"

bool
MediaError::is_ok() const
{
	return m_code == MediaErrorCode::NONE;
}

void
MediaError::clear()
{
	m_code = MediaErrorCode::NONE;
	m_message.clear();
}

MediaError
MediaError::make(MediaErrorCode v_code, const QString &v_message)
{
	MediaError error;
	error.m_code = v_code;
	error.m_message = v_message;
	return error;
}

QString
media_error_code_to_string(MediaErrorCode v_code)
{
	switch (v_code)
	{
		case MediaErrorCode::NONE:
			return QStringLiteral("NONE");
		case MediaErrorCode::INVALID_ARGUMENT:
			return QStringLiteral("INVALID_ARGUMENT");
		case MediaErrorCode::FILE_NOT_FOUND:
			return QStringLiteral("FILE_NOT_FOUND");
		case MediaErrorCode::OPEN_FAILED:
			return QStringLiteral("OPEN_FAILED");
		case MediaErrorCode::STREAM_NOT_FOUND:
			return QStringLiteral("STREAM_NOT_FOUND");
		case MediaErrorCode::UNSUPPORTED:
			return QStringLiteral("UNSUPPORTED");
		case MediaErrorCode::PROBE_FAILED:
			return QStringLiteral("PROBE_FAILED");
		case MediaErrorCode::DECODE_FAILED:
			return QStringLiteral("DECODE_FAILED");
		case MediaErrorCode::ENCODE_FAILED:
			return QStringLiteral("ENCODE_FAILED");
		case MediaErrorCode::MUX_FAILED:
			return QStringLiteral("MUX_FAILED");
		case MediaErrorCode::IO_FAILED:
			return QStringLiteral("IO_FAILED");
		case MediaErrorCode::CANCELLED:
			return QStringLiteral("CANCELLED");
		case MediaErrorCode::INTERNAL:
			return QStringLiteral("INTERNAL");
	}
	return QStringLiteral("UNKNOWN");
}
