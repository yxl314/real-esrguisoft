#include "sidebar_nav.h"

#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include "icon_provider.h"
#include "theme_manager.h"

namespace
{
const int SIDEBAR_WIDTH = 200;
const int SIDEBAR_COLLAPSED_WIDTH = 72;
const int BRAND_HEIGHT = 44;
const int ROW_HEIGHT = 28;
const int ROW_HEIGHT_COLLAPSED = 32;
const int ROW_MARGIN_X = 8;
const int ROW_SPACING = 2;
const int ICON_SIZE = 18;
const int LOGO_SIZE = 22;
const int COLLAPSE_BUTTON_SIZE = 28;
const int BOTTOM_PADDING = 8;
} // namespace

SidebarNav::SidebarNav(QWidget *v_parent) : QWidget(v_parent)
{
	setObjectName(QStringLiteral("sidebar"));
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	setCursor(Qt::ArrowCursor);

	m_logo = QPixmap(QStringLiteral(":/app.png"));
	if (!m_logo.isNull())
	{
		m_logo = m_logo.scaled(LOGO_SIZE * 2, LOGO_SIZE * 2,
							   Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}

	add_separator(); // 占位：后面 add_item 时把首条分隔线从顶部挪到正确位置

	connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
			[this]()
			{
				m_logo = QPixmap(QStringLiteral(":/app.png"));
				if (!m_logo.isNull())
				{
					m_logo = m_logo.scaled(LOGO_SIZE * 2, LOGO_SIZE * 2,
										   Qt::KeepAspectRatio,
										   Qt::SmoothTransformation);
				}
				relayout();
				update();
			});

	update_width();
	relayout();
}

void
SidebarNav::set_brand(const QString &v_title, const QString &v_subtitle)
{
	m_brand_title = v_title;
	m_brand_subtitle = v_subtitle;
	relayout();
	update();
}

void
SidebarNav::add_item(const QString &v_id, const QString &v_text,
					 const QString &v_icon_name)
{
	Row row;
	row.m_type = RowType::NAV;
	row.m_id = v_id;
	row.m_text = v_text;
	row.m_icon = v_icon_name;
	m_rows.append(row);

	// 第一条导航项之前不应出现分隔线
	if (m_rows.size() >= 2 && m_rows.first().m_type == RowType::SEPARATOR
		&& m_rows.at(1).m_type == RowType::NAV)
	{
		m_rows.removeFirst();
	}

	relayout();
	update();
}

void
SidebarNav::set_item_text(const QString &v_id, const QString &v_text)
{
	for (Row &row : m_rows)
	{
		if (row.m_type == RowType::NAV && row.m_id == v_id)
		{
			row.m_text = v_text;
			update();
			return;
		}
	}
}

void
SidebarNav::retranslate_ui()
{
	// 导航项文本由 MainWindow 通过 set_item_text 更新；底部主题标签在
	// paintEvent 内按 tr() 现取，这里触发一次重绘即可。
	update();
}

void
SidebarNav::add_separator()
{
	if (m_rows.isEmpty() || m_rows.last().m_type == RowType::SEPARATOR)
	{
		return;
	}
	Row row;
	row.m_type = RowType::SEPARATOR;
	m_rows.append(row);
	relayout();
	update();
}

void
SidebarNav::set_current(const QString &v_id)
{
	if (v_id == m_current_id)
	{
		return;
	}
	m_current_id = v_id;
	update();
}

QString
SidebarNav::current_id() const
{
	return m_current_id;
}

void
SidebarNav::set_collapsed(bool v_collapsed)
{
	if (v_collapsed == m_collapsed)
	{
		return;
	}
	m_collapsed = v_collapsed;
	update_width();
	relayout();
	update();
	emit collapsed_changed(m_collapsed);
}

bool
SidebarNav::is_collapsed() const
{
	return m_collapsed;
}

void
SidebarNav::set_theme_mode(int v_mode)
{
	m_theme_mode = v_mode;
	update();
}

QSize
SidebarNav::sizeHint() const
{
	return QSize(m_collapsed ? SIDEBAR_COLLAPSED_WIDTH : SIDEBAR_WIDTH, 600);
}

void
SidebarNav::update_width()
{
	setFixedWidth(m_collapsed ? SIDEBAR_COLLAPSED_WIDTH : SIDEBAR_WIDTH);
}

void
SidebarNav::resizeEvent(QResizeEvent *v_event)
{
	relayout();
	QWidget::resizeEvent(v_event);
}

void
SidebarNav::relayout()
{
	const int nav_row_height = m_collapsed ? ROW_HEIGHT_COLLAPSED : ROW_HEIGHT;
	const int nav_row_width = m_collapsed ? 48 : width() - ROW_MARGIN_X * 2;
	const int nav_row_x
		= m_collapsed ? (width() - nav_row_width) / 2 : ROW_MARGIN_X;

	int y = BRAND_HEIGHT;

	// 主题行固定在底部：先算出它的位置，导航项不要压到它
	const int theme_row_height = nav_row_height;
	const int theme_row_y = height() - BOTTOM_PADDING - theme_row_height;

	for (int i = 0; i < m_rows.size(); ++i)
	{
		Row &row = m_rows[i];
		if (row.m_type == RowType::SEPARATOR)
		{
			y += 7;
			row.m_rect = QRect(ROW_MARGIN_X + 8, y,
							   width() - (ROW_MARGIN_X + 8) * 2, 1);
			y += 7;
			continue;
		}
		if (row.m_type == RowType::THEME)
		{
			continue; // 主题行在 paint 里按底部定位
		}
		row.m_rect = QRect(nav_row_x, y, nav_row_width, nav_row_height);
		y += nav_row_height + ROW_SPACING;
	}

	// 主题行行尾追加（不在 m_rows 里，绘制时按底部固定位置画）
	Q_UNUSED(theme_row_y);
}

int
SidebarNav::row_at(const QPoint &v_pos) const
{
	for (int i = 0; i < m_rows.size(); ++i)
	{
		const Row &row = m_rows[i];
		if (row.m_type == RowType::NAV && row.m_rect.contains(v_pos))
		{
			return i;
		}
	}
	return -1;
}

bool
SidebarNav::is_brand_toggle_at(const QPoint &v_pos) const
{
	if (m_collapsed)
	{
		// 收起态：整个品牌区都可以点击展开
		return QRect(0, 0, width(), BRAND_HEIGHT).contains(v_pos);
	}
	const QRect toggle_rect(width() - ROW_MARGIN_X - COLLAPSE_BUTTON_SIZE,
							(BRAND_HEIGHT - COLLAPSE_BUTTON_SIZE) / 2,
							COLLAPSE_BUTTON_SIZE, COLLAPSE_BUTTON_SIZE);
	return toggle_rect.contains(v_pos);
}

QString
SidebarNav::theme_icon_name() const
{
	if (m_theme_mode == 1)
	{
		return QStringLiteral("sun");
	}
	if (m_theme_mode == 2)
	{
		return QStringLiteral("moon");
	}
	return QStringLiteral("monitor");
}

QString
SidebarNav::theme_text() const
{
	if (m_theme_mode == 1)
	{
		return SidebarNav::tr("浅色主题");
	}
	if (m_theme_mode == 2)
	{
		return SidebarNav::tr("深色主题");
	}
	return SidebarNav::tr("跟随系统");
}

void
SidebarNav::paintEvent(QPaintEvent *v_event)
{
	Q_UNUSED(v_event)

	const ThemeTokens &tokens = ThemeManager::instance().tokens();
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	// 背景 + 右侧 1px 描边
	painter.fillRect(rect(), tokens.m_bg_sidebar);
	painter.fillRect(QRect(width() - 1, 0, 1, height()),
					 tokens.m_border_default);

	// ---- 品牌区 ----
	const int brand_toggle_x = width() - ROW_MARGIN_X - COLLAPSE_BUTTON_SIZE;
	const QRect brand_toggle_rect(brand_toggle_x,
								  (BRAND_HEIGHT - COLLAPSE_BUTTON_SIZE) / 2,
								  COLLAPSE_BUTTON_SIZE, COLLAPSE_BUTTON_SIZE);

	if (m_collapsed)
	{
		// 收起态：Logo 居中，右上角给一个可见的展开箭头（点击品牌区展开）
		const int logo_x = (width() - LOGO_SIZE) / 2;
		const int logo_y = (BRAND_HEIGHT - LOGO_SIZE) / 2;
		if (!m_logo.isNull())
		{
			painter.drawPixmap(QRect(logo_x, logo_y, LOGO_SIZE, LOGO_SIZE),
							   m_logo);
		}
		const QPixmap expand_icon = IconProvider::pixmap(
			QStringLiteral("panel-left-open"), tokens.m_text_2, 14);
		painter.drawPixmap(QRect(width() - 18, 4, 14, 14), expand_icon);
	}
	else
	{
		if (!m_logo.isNull())
		{
			painter.drawPixmap(
				QRect(16, (BRAND_HEIGHT - LOGO_SIZE) / 2, LOGO_SIZE, LOGO_SIZE),
				m_logo);
		}

		QFont brand_font = font();
		brand_font.setPixelSize(tokens.m_metrics.m_fs_strong);
		brand_font.setWeight(QFont::DemiBold);
		painter.setFont(brand_font);
		painter.setPen(tokens.m_text_1);
		painter.drawText(QRect(46, 0, brand_toggle_x - 46, BRAND_HEIGHT),
						 Qt::AlignLeft | Qt::AlignVCenter, m_brand_title);

		// 折叠按钮（hover 浅底）
		if (m_hovered_brand_toggle)
		{
			QPainterPath path;
			path.addRoundedRect(brand_toggle_rect,
								tokens.m_metrics.m_radius_control,
								tokens.m_metrics.m_radius_control);
			painter.fillPath(path, tokens.m_bg_subtle);
		}
		const QPixmap collapse_icon = IconProvider::pixmap(
			QStringLiteral("panel-left-close"), tokens.m_text_2, 16);
		const QRect icon_rect = brand_toggle_rect.adjusted(6, 6, -6, -6);
		painter.drawPixmap(icon_rect, collapse_icon);
	}

	// ---- 导航行 ----
	QFont row_font = font();
	row_font.setPixelSize(tokens.m_metrics.m_fs_body);

	for (int i = 0; i < m_rows.size(); ++i)
	{
		const Row &row = m_rows[i];

		if (row.m_type == RowType::SEPARATOR)
		{
			painter.fillRect(row.m_rect, tokens.m_border_default);
			continue;
		}

		const bool selected = (row.m_id == m_current_id);
		const bool hovered = (i == m_hovered_row);

		if (selected || hovered)
		{
			QPainterPath path;
			path.addRoundedRect(QRectF(row.m_rect),
								tokens.m_metrics.m_radius_control,
								tokens.m_metrics.m_radius_control);
			painter.fillPath(path, selected ? tokens.m_bg_selected
											: tokens.m_bg_subtle);
		}

		const QPixmap icon = IconProvider::pixmap(
			row.m_icon, selected ? tokens.m_text_1 : tokens.m_text_2,
			ICON_SIZE);

		if (m_collapsed)
		{
			const QRect icon_rect(row.m_rect.center().x() - ICON_SIZE / 2,
								  row.m_rect.center().y() - ICON_SIZE / 2,
								  ICON_SIZE, ICON_SIZE);
			painter.drawPixmap(icon_rect, icon);
		}
		else
		{
			const int icon_x = row.m_rect.x() + 8;
			const QRect icon_rect(icon_x,
								  row.m_rect.center().y() - ICON_SIZE / 2,
								  ICON_SIZE, ICON_SIZE);
			painter.drawPixmap(icon_rect, icon);

			row_font.setWeight(selected ? QFont::DemiBold : QFont::Normal);
			painter.setFont(row_font);
			painter.setPen(selected ? tokens.m_text_1 : tokens.m_text_2);
			painter.drawText(QRect(icon_x + ICON_SIZE + 8, row.m_rect.y(),
								   row.m_rect.width()
									   - (icon_x - row.m_rect.x()) - ICON_SIZE
									   - 12,
								   row.m_rect.height()),
							 Qt::AlignLeft | Qt::AlignVCenter, row.m_text);
		}
	}

	// ---- 底部主题行 ----
	const int theme_row_height
		= m_collapsed ? ROW_HEIGHT_COLLAPSED : ROW_HEIGHT;
	const int theme_row_y = height() - BOTTOM_PADDING - theme_row_height;
	const int theme_row_width = m_collapsed ? 48 : width() - ROW_MARGIN_X * 2;
	const int theme_row_x
		= m_collapsed ? (width() - theme_row_width) / 2 : ROW_MARGIN_X;
	const QRect theme_rect(theme_row_x, theme_row_y, theme_row_width,
						   theme_row_height);

	// 主题行上方 1px 分隔线
	painter.fillRect(QRect(ROW_MARGIN_X + 8, theme_row_y - 7,
						   width() - (ROW_MARGIN_X + 8) * 2, 1),
					 tokens.m_border_default);

	if (m_hovered_theme)
	{
		QPainterPath path;
		path.addRoundedRect(QRectF(theme_rect),
							tokens.m_metrics.m_radius_control,
							tokens.m_metrics.m_radius_control);
		painter.fillPath(path, tokens.m_bg_subtle);
	}

	const QPixmap theme_icon
		= IconProvider::pixmap(theme_icon_name(), tokens.m_text_2, ICON_SIZE);
	if (m_collapsed)
	{
		painter.drawPixmap(QRect(theme_rect.center().x() - ICON_SIZE / 2,
								 theme_rect.center().y() - ICON_SIZE / 2,
								 ICON_SIZE, ICON_SIZE),
						   theme_icon);
	}
	else
	{
		const int icon_x = theme_rect.x() + 8;
		painter.drawPixmap(QRect(icon_x,
								 theme_rect.center().y() - ICON_SIZE / 2,
								 ICON_SIZE, ICON_SIZE),
						   theme_icon);
		row_font.setWeight(QFont::Normal);
		painter.setFont(row_font);
		painter.setPen(tokens.m_text_2);
		painter.drawText(QRect(icon_x + ICON_SIZE + 8, theme_rect.y(),
							   theme_rect.width() - ICON_SIZE - 24,
							   theme_rect.height()),
						 Qt::AlignLeft | Qt::AlignVCenter, theme_text());
	}
}

void
SidebarNav::mousePressEvent(QMouseEvent *v_event)
{
	if (v_event->button() != Qt::LeftButton)
	{
		QWidget::mousePressEvent(v_event);
		return;
	}
	const QPoint pos = v_event->pos();

	// 品牌区：收起态点击展开；展开态点折叠按钮收起
	if (is_brand_toggle_at(pos))
	{
		set_collapsed(!m_collapsed);
		v_event->accept();
		return;
	}

	const int theme_row_height
		= m_collapsed ? ROW_HEIGHT_COLLAPSED : ROW_HEIGHT;
	const int theme_row_y = height() - BOTTOM_PADDING - theme_row_height;
	const int theme_row_width = m_collapsed ? 48 : width() - ROW_MARGIN_X * 2;
	const int theme_row_x
		= m_collapsed ? (width() - theme_row_width) / 2 : ROW_MARGIN_X;
	if (QRect(theme_row_x, theme_row_y, theme_row_width, theme_row_height)
			.contains(pos))
	{
		emit theme_button_clicked();
		v_event->accept();
		return;
	}

	const int row = row_at(pos);
	if (row >= 0)
	{
		const QString id = m_rows[row].m_id;
		set_current(id);
		setFocus(Qt::MouseFocusReason);
		emit item_activated(id);
		v_event->accept();
		return;
	}

	QWidget::mousePressEvent(v_event);
}

void
SidebarNav::mouseMoveEvent(QMouseEvent *v_event)
{
	const QPoint pos = v_event->pos();

	const int row = row_at(pos);
	if (row != m_hovered_row)
	{
		m_hovered_row = row;
		update();
	}

	const bool brand_toggle = is_brand_toggle_at(pos);
	if (brand_toggle != m_hovered_brand_toggle)
	{
		m_hovered_brand_toggle = brand_toggle;
		update();
	}

	const int theme_row_height
		= m_collapsed ? ROW_HEIGHT_COLLAPSED : ROW_HEIGHT;
	const int theme_row_y = height() - BOTTOM_PADDING - theme_row_height;
	const int theme_row_width = m_collapsed ? 48 : width() - ROW_MARGIN_X * 2;
	const int theme_row_x
		= m_collapsed ? (width() - theme_row_width) / 2 : ROW_MARGIN_X;
	const bool theme_hovered
		= QRect(theme_row_x, theme_row_y, theme_row_width, theme_row_height)
			  .contains(pos);
	if (theme_hovered != m_hovered_theme)
	{
		m_hovered_theme = theme_hovered;
		update();
	}

	// 收起态：导航行 / 品牌区 / 主题区都显示文字提示（对应 cc-switch HoverTip）
	if (m_collapsed && row >= 0)
	{
		QToolTip::showText(v_event->globalPosition().toPoint(),
						   m_rows[row].m_text, this);
	}
	else if (m_collapsed && brand_toggle)
	{
		QToolTip::showText(v_event->globalPosition().toPoint(),
						   SidebarNav::tr("展开侧栏"), this);
	}
	else if (m_collapsed && theme_hovered)
	{
		QToolTip::showText(v_event->globalPosition().toPoint(), theme_text(),
						   this);
	}
	else
	{
		QToolTip::hideText();
	}

	QWidget::mouseMoveEvent(v_event);
}

void
SidebarNav::leaveEvent(QEvent *v_event)
{
	m_hovered_row = -1;
	m_hovered_brand_toggle = false;
	m_hovered_theme = false;
	update();
	QWidget::leaveEvent(v_event);
}

void
SidebarNav::keyPressEvent(QKeyEvent *v_event)
{
	const int key = v_event->key();

	if (key == Qt::Key_B && (v_event->modifiers() & Qt::ControlModifier))
	{
		set_collapsed(!m_collapsed);
		return;
	}

	// 上下方向键在导航项间移动，回车激活
	if (key == Qt::Key_Down || key == Qt::Key_Up)
	{
		QVector<int> nav_rows;
		for (int i = 0; i < m_rows.size(); ++i)
		{
			if (m_rows[i].m_type == RowType::NAV)
			{
				nav_rows.append(i);
			}
		}
		if (nav_rows.isEmpty())
		{
			return;
		}
		int current = 0;
		for (int j = 0; j < nav_rows.size(); ++j)
		{
			if (m_rows[nav_rows[j]].m_id == m_current_id)
			{
				current = j;
				break;
			}
		}
		const int step = (key == Qt::Key_Down) ? 1 : -1;
		const int next = qBound(0, current + step, nav_rows.size() - 1);
		set_current(m_rows[nav_rows[next]].m_id);
		emit item_activated(m_current_id);
		return;
	}
	if (key == Qt::Key_Return || key == Qt::Key_Enter)
	{
		if (!m_current_id.isEmpty())
		{
			emit item_activated(m_current_id);
		}
		return;
	}

	QWidget::keyPressEvent(v_event);
}

void
SidebarNav::repaint_all()
{
	relayout();
	update();
}
