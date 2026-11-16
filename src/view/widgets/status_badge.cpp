#include "status_badge.h"

#include <QStyle>

StatusBadge::StatusBadge(const QString &v_text, Tone v_tone, QWidget *v_parent)
	: QLabel(v_text, v_parent), m_tone(v_tone)
{
	setObjectName(QStringLiteral("statusBadge"));
	setAlignment(Qt::AlignCenter);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	refresh_style();
}

void
StatusBadge::set_tone(Tone v_tone)
{
	if (v_tone == m_tone)
	{
		return;
	}
	m_tone = v_tone;
	refresh_style();
}

StatusBadge::Tone
StatusBadge::tone() const
{
	return m_tone;
}

QString
StatusBadge::tone_name(Tone v_tone)
{
	switch (v_tone)
	{
		case Tone::OUTLINE:
			return QStringLiteral("outline");
		case Tone::SUCCESS:
			return QStringLiteral("success");
		case Tone::WARNING:
			return QStringLiteral("warning");
		case Tone::DANGER:
			return QStringLiteral("danger");
		case Tone::INFO:
			return QStringLiteral("info");
		case Tone::ACCENT:
			return QStringLiteral("accent");
		case Tone::ROUTE:
			return QStringLiteral("route");
		case Tone::NEUTRAL:
			break;
	}
	return QStringLiteral("neutral");
}

void
StatusBadge::refresh_style()
{
	setProperty("tone", tone_name(m_tone));
	style()->unpolish(this);
	style()->polish(this);
	updateGeometry();
	update();
}
