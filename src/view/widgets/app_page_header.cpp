#include "app_page_header.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QWindow>

#include "icon_provider.h"
#include "theme_manager.h"

namespace
{
const int HEADER_HEIGHT = 52;
} // namespace

AppPageHeader::AppPageHeader(QWidget *v_parent) : QWidget(v_parent)
{
	setObjectName(QStringLiteral("pageHeader"));
	setFixedHeight(HEADER_HEIGHT);
	setAttribute(Qt::WA_StyledBackground, false);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(24, 0, 16, 0);
	layout->setSpacing(8);

	m_leading_icon = new QLabel(this);
	m_leading_icon->setObjectName(QStringLiteral("pageHeaderLeading"));
	m_leading_icon->setFixedSize(22, 22);
	m_leading_icon->setVisible(false);
	layout->addWidget(m_leading_icon, 0, Qt::AlignVCenter);

	m_title = new QLabel(this);
	m_title->setObjectName(QStringLiteral("pageHeaderTitle"));
	layout->addWidget(m_title, 0, Qt::AlignVCenter);

	m_subtitle = new QLabel(this);
	m_subtitle->setObjectName(QStringLiteral("pageHeaderSubtitle"));
	m_subtitle->setVisible(false);
	layout->addWidget(m_subtitle, 0, Qt::AlignVCenter);

	layout->addStretch(1);

	m_actions_layout = new QHBoxLayout();
	m_actions_layout->setContentsMargins(0, 0, 0, 0);
	m_actions_layout->setSpacing(8);
	layout->addLayout(m_actions_layout);

	m_window_layout = new QHBoxLayout();
	m_window_layout->setContentsMargins(0, 0, 0, 0);
	m_window_layout->setSpacing(4);
	layout->addLayout(m_window_layout);

	connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
			[this]() { update(); });
}

void
AppPageHeader::set_leading_icon(const QString &v_icon_name)
{
	if (v_icon_name.isEmpty())
	{
		m_leading_icon->setVisible(false);
		return;
	}
	m_leading_icon->setPixmap(IconProvider::pixmap(
		v_icon_name, ThemeManager::instance().color(QStringLiteral("text.1")),
		22));
	m_leading_icon->setVisible(true);
}

void
AppPageHeader::set_title(const QString &v_title)
{
	m_title->setText(v_title);
}

QString
AppPageHeader::title() const
{
	return m_title->text();
}

void
AppPageHeader::set_subtitle(const QString &v_subtitle)
{
	m_subtitle->setText(v_subtitle);
	m_subtitle->setVisible(!v_subtitle.isEmpty());
}

void
AppPageHeader::set_title_level(TitleLevel v_level)
{
	m_title->setProperty("level", (v_level == TitleLevel::APP)
									  ? QStringLiteral("app")
									  : QVariant());
	m_title->style()->unpolish(m_title);
	m_title->style()->polish(m_title);
}

void
AppPageHeader::add_action(QWidget *v_widget)
{
	if (v_widget != nullptr)
	{
		m_actions_layout->addWidget(v_widget, 0, Qt::AlignVCenter);
	}
}

void
AppPageHeader::add_window_action(QWidget *v_widget)
{
	if (v_widget != nullptr)
	{
		m_window_layout->addWidget(v_widget, 0, Qt::AlignVCenter);
	}
}

void
AppPageHeader::paintEvent(QPaintEvent *v_event)
{
	Q_UNUSED(v_event)

	const ThemeTokens &tokens = ThemeManager::instance().tokens();
	QPainter painter(this);
	painter.fillRect(rect(), tokens.m_bg_app);
	// 1px 底边框（页头总高 52 含边框）
	painter.fillRect(QRect(0, height() - 1, width(), 1),
					 tokens.m_border_default);
}

void
AppPageHeader::mousePressEvent(QMouseEvent *v_event)
{
	// 空白处按住左键：交给系统做窗口拖动（Win11 上还支持贴靠布局）
	if (v_event->button() == Qt::LeftButton
		&& window()->windowHandle() != nullptr)
	{
		window()->windowHandle()->startSystemMove();
		v_event->accept();
		return;
	}
	QWidget::mousePressEvent(v_event);
}

void
AppPageHeader::mouseDoubleClickEvent(QMouseEvent *v_event)
{
	if (v_event->button() == Qt::LeftButton)
	{
		emit title_double_clicked();
		v_event->accept();
		return;
	}
	QWidget::mouseDoubleClickEvent(v_event);
}
