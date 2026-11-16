#include "card_widget.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>

#include "theme_manager.h"

CardWidget::CardWidget(QWidget *v_parent) : QFrame(v_parent)
{
	init_ui();
}

CardWidget::CardWidget(const QString &v_title, QWidget *v_parent)
	: QFrame(v_parent)
{
	init_ui();
	set_title(v_title);
}

void
CardWidget::init_ui()
{
	setObjectName(QStringLiteral("card"));
	setFrameShape(QFrame::NoFrame);
	setAttribute(Qt::WA_StyledBackground, true);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

	m_body_layout = new QVBoxLayout(this);
	m_body_layout->setContentsMargins(m_padding, m_padding, m_padding,
									  m_padding);
	m_body_layout->setSpacing(12);

	refresh_shadow();

	connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
			[this]()
			{
				refresh_shadow();
				update();
			});
}

void
CardWidget::set_title(const QString &v_title)
{
	if (m_title_label == nullptr)
	{
		m_header_row = new QWidget(this);
		m_header_row->setObjectName(QStringLiteral("cardHeaderRow"));
		m_header_layout = new QHBoxLayout(m_header_row);
		m_header_layout->setContentsMargins(0, 0, 0, 0);
		m_header_layout->setSpacing(8);

		m_title_label = new QLabel(m_header_row);
		m_title_label->setObjectName(QStringLiteral("sectionTitle"));
		m_header_layout->addWidget(m_title_label, 0, Qt::AlignVCenter);
		m_header_layout->addStretch(1);

		m_body_layout->insertWidget(0, m_header_row);
	}
	m_title_label->setText(v_title);
	m_header_row->setVisible(!v_title.isEmpty());
}

QString
CardWidget::title() const
{
	return (m_title_label != nullptr) ? m_title_label->text() : QString();
}

void
CardWidget::add_header_action(QWidget *v_widget)
{
	if (m_header_row == nullptr)
	{
		set_title(QString());
	}
	if (m_header_row == nullptr || v_widget == nullptr)
	{
		return;
	}
	m_header_layout->addWidget(v_widget, 0, Qt::AlignVCenter);
}

void
CardWidget::set_active(bool v_active)
{
	if (v_active == m_active)
	{
		return;
	}
	m_active = v_active;
	setProperty("active", v_active ? QStringLiteral("true") : QVariant());
	style()->unpolish(this);
	style()->polish(this);
	update();
}

bool
CardWidget::is_active() const
{
	return m_active;
}

void
CardWidget::set_hoverable(bool v_hoverable)
{
	m_hoverable = v_hoverable;
	setProperty("hoverable", v_hoverable ? QStringLiteral("true") : QVariant());
	style()->unpolish(this);
	style()->polish(this);
	update();
}

void
CardWidget::refresh_shadow()
{
	if (!m_shadow_enabled)
	{
		setGraphicsEffect(nullptr);
		return;
	}

	const ThemeTokens &tokens = ThemeManager::instance().tokens();
	ThemeShadow shadow = tokens.m_shadow_sm;
	// 卡片用比 shadow-sm 稍明显的一档，才能在浅色底上看出分层
	shadow = tokens.m_shadow_md;

	QGraphicsDropShadowEffect *effect
		= qobject_cast<QGraphicsDropShadowEffect *>(graphicsEffect());
	if (effect == nullptr)
	{
		effect = new QGraphicsDropShadowEffect(this);
		effect->setOffset(0, qMax(1, shadow.m_offset_y / 2));
		effect->setBlurRadius(shadow.m_blur);
		setGraphicsEffect(effect);
	}
	effect->setColor(shadow.m_color);
}

void
CardWidget::set_padding(int v_padding)
{
	m_padding = v_padding;
	m_body_layout->setContentsMargins(v_padding, v_padding, v_padding,
									  v_padding);
}
