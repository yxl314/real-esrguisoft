#include "action_button.h"

#include <QStyle>

#include "theme_manager.h"

namespace
{
QString
variant_name(ActionButton::Variant v_variant)
{
	switch (v_variant)
	{
		case ActionButton::Variant::PRIMARY:
			return QStringLiteral("primary");
		case ActionButton::Variant::PRIMARY_ICON:
			return QStringLiteral("primaryIcon");
		case ActionButton::Variant::QUIET:
			return QStringLiteral("quiet");
		case ActionButton::Variant::DANGER:
			return QStringLiteral("danger");
		case ActionButton::Variant::ICON:
			return QStringLiteral("icon");
		case ActionButton::Variant::ICON_COMPACT:
			return QStringLiteral("iconCompact");
		case ActionButton::Variant::SECONDARY:
			break;
	}
	return QStringLiteral("secondary");
}
} // namespace

ActionButton::ActionButton(Variant v_variant, const QString &v_text,
						   const QString &v_icon_name, QWidget *v_parent)
	: QPushButton(v_text, v_parent), m_variant(v_variant),
	  m_icon_name(v_icon_name), m_text(v_text)
{
	setObjectName(QStringLiteral("actionButton"));
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::StrongFocus);

	refresh_style();
	refresh_icon();

	connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
			[this]() { refresh_icon(); });
}

void
ActionButton::set_variant(Variant v_variant)
{
	if (v_variant == m_variant)
	{
		return;
	}
	m_variant = v_variant;
	refresh_style();
	refresh_icon();
}

ActionButton::Variant
ActionButton::variant() const
{
	return m_variant;
}

void
ActionButton::set_icon_name(const QString &v_icon_name)
{
	m_icon_name = v_icon_name;
	refresh_icon();
}

QString
ActionButton::icon_name() const
{
	return m_icon_name;
}

void
ActionButton::set_hover_tone(IconProvider::Tone v_tone)
{
	m_has_hover_tone = true;
	m_hover_tone = v_tone;
	refresh_icon();
}

void
ActionButton::set_compact(bool v_compact)
{
	if (v_compact == m_compact)
	{
		return;
	}
	m_compact = v_compact;
	refresh_style();
}

bool
ActionButton::is_compact() const
{
	return m_compact;
}

void
ActionButton::enterEvent(QEnterEvent *v_event)
{
	m_hovered = true;
	QPushButton::enterEvent(v_event);
	refresh_icon();
}

void
ActionButton::leaveEvent(QEvent *v_event)
{
	m_hovered = false;
	QPushButton::leaveEvent(v_event);
	refresh_icon();
}

IconProvider::Tone
ActionButton::icon_tone() const
{
	if (m_hovered && m_has_hover_tone)
	{
		return m_hover_tone;
	}
	switch (m_variant)
	{
		case Variant::PRIMARY:
		case Variant::PRIMARY_ICON:
			return IconProvider::Tone::ON_ACCENT;
		case Variant::SECONDARY:
		case Variant::QUIET:
			return IconProvider::Tone::PRIMARY;
		case Variant::DANGER:
			return IconProvider::Tone::DANGER;
		case Variant::ICON:
		case Variant::ICON_COMPACT:
			return m_hovered ? IconProvider::Tone::PRIMARY
							 : IconProvider::Tone::SECONDARY;
	}
	return IconProvider::Tone::SECONDARY;
}

void
ActionButton::refresh_style()
{
	setProperty("variant", variant_name(m_variant));

	const bool is_icon
		= (m_variant == Variant::ICON || m_variant == Variant::ICON_COMPACT
		   || m_variant == Variant::PRIMARY_ICON);
	if (is_icon)
	{
		const int side
			= (m_variant == Variant::ICON || m_variant == Variant::PRIMARY_ICON)
				  ? 32
				  : 28;
		setFixedSize(side, side);
		setIconSize(QSize(16, 16));
		setText(QString());
	}
	else
	{
		setMinimumHeight(0);
		setMaximumHeight(QWIDGETSIZE_MAX);
		setFixedHeight(m_compact ? 28 : 32);
		setIconSize(QSize(16, 16));
		setText(m_text);
	}

	style()->unpolish(this);
	style()->polish(this);
	updateGeometry();
}

void
ActionButton::refresh_icon()
{
	if (m_icon_name.isEmpty())
	{
		setIcon(QIcon());
		return;
	}
	const int logical = 16;
	setIcon(IconProvider::icon(m_icon_name, icon_tone(), logical));
	update();
}
