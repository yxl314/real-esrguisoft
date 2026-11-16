#include "exeoutputparser.h"

#include <QRegularExpression>
#include <QtMath>

namespace
{

// 把浮点百分比收敛到 0-100 的整数
int
clamp_progress(double v_value)
{
	int progress = qRound(v_value);
	if (progress < 0)
	{
		return 0;
	}
	if (progress > 100)
	{
		return 100;
	}
	return progress;
}

} // namespace

ExeOutputParser::Result
ExeOutputParser::parse_line(const QString &v_line)
{
	Result result;

	const QString line = v_line.trimmed();
	if (line.isEmpty())
	{
		return result;
	}

	// 引擎失败标记：退出码不可信，必须显式识别这些文本。
	// task-38 P3-9：旧正则整行锚定，漏掉 "failed: reason"、前缀
	// "ERROR:"/"时间戳"、"[" 前缀行；这里改为行内子串匹配，并在忽略
	// "[" GPU 枚举行之前判定。
	static const QRegularExpression s_failure_regex(
		QStringLiteral("(?:decode|encode)\\s+image\\s+.+\\bfailed\\b"
					   "|_wfopen\\s+.+\\bfailed\\b"),
		QRegularExpression::CaseInsensitiveOption);
	if (s_failure_regex.match(line).hasMatch())
	{
		result.m_failures << line;
		return result;
	}

	// GPU 枚举信息行（"[" 开头）与其它普通日志行直接忽略
	if (line.startsWith(QLatin1Char('[')))
	{
		return result;
	}

	// 旧协议：PROGRESS:xx.yy（可出现在行内任意位置）
	static const QRegularExpression s_legacy_progress_regex(
		QStringLiteral("PROGRESS:(\\d+(?:\\.\\d+)?)"));
	const QRegularExpressionMatch legacy_match
		= s_legacy_progress_regex.match(line);
	if (legacy_match.hasMatch())
	{
		result.m_progress = clamp_progress(legacy_match.captured(1).toDouble());
		return result;
	}

	// realesrgan 进度行：整行形如 "16.67%"
	static const QRegularExpression s_percent_regex(
		QStringLiteral("^(\\d+(?:\\.\\d+)?)\\s*%$"));
	const QRegularExpressionMatch percent_match = s_percent_regex.match(line);
	if (percent_match.hasMatch())
	{
		result.m_progress
			= clamp_progress(percent_match.captured(1).toDouble());
		return result;
	}

	// 旧协议：当前处理文件 "Processing: xxx"
	static const QRegularExpression s_file_regex(
		QStringLiteral("^Processing[:\\s]+(.+)$"));
	const QRegularExpressionMatch file_match = s_file_regex.match(line);
	if (file_match.hasMatch())
	{
		result.m_file = file_match.captured(1).trimmed();
	}

	return result;
}

ExeOutputParser::Result
ExeOutputParser::parse(const QString &v_output)
{
	Result aggregate;

	const QStringList lines = v_output.split(
		QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
	for (const QString &line : lines)
	{
		const Result line_result = parse_line(line);
		if (line_result.m_progress)
		{
			aggregate.m_progress = line_result.m_progress;
		}
		if (line_result.m_file)
		{
			aggregate.m_file = line_result.m_file;
		}
		if (!line_result.m_failures.isEmpty())
		{
			aggregate.m_failures.append(line_result.m_failures);
		}
	}

	return aggregate;
}
