#include "segmented_control.h"

#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QVariantAnimation>

#include "icon_provider.h"
#include "theme_manager.h"

namespace
{
const int ANIMATION_DURATION_MS = 200;

qreal
lerp(qreal v_from, qreal v_to, qreal v_t)
{
	return v_from + (v_to - v_from) * v_t;
}

QRectF
lerp_rect(const QRectF &v_from, const QRectF &v_to, qreal v_t)
{
	return QRectF(lerp(v_from.x(), v_to.x(), v_t),
				  lerp(v_from.y(), v_to.y(), v_t),
				  lerp(v_from.width(), v_to.width(), v_t),
				  lerp(v_from.height(), v_to.height(), v_t));
}
} // namespace

SegmentedControl::SegmentedControl(QWidget *v_parent) : QWidget(v_parent)
{
	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_Hover, true);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	setCursor(Qt::PointingHandCursor);

	refresh_metrics();

	m_animation = new QVariantAnimation(this);
	m_animation->setDuration(ANIMATION_DURATION_MS);
	m_animation->setEasingCurve(QEasingCurve::OutCubic);
	connect(m_animation, &QVariantAnimation::valueChanged, this,
			[this](const QVariant &v_value)
			{
				m_indicator = lerp_rect(m_indicator_from, m_indicator_to,
										v_value.toReal());
				update();
			});

	connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
			[this]()
			{
				rebuild_icons();
				update();
			});
}

void
SegmentedControl::set_size_mode(SizeMode v_mode)
{
	if (v_mode == m_size_mode)
	{
		return;
	}
	m_size_mode = v_mode;
	refresh_metrics();
	rebuild_icons();
	updateGeometry();
	update();
}

SegmentedControl::SizeMode
SegmentedControl::size_mode() const
{
	return m_size_mode;
}

void
SegmentedControl::refresh_metrics()
{
	if (m_size_mode == SizeMode::MEDIUM)
	{
		m_height = 36;
		m_radius = 10;
		m_nested_radius = 7;
		m_font_size = 13;
		m_icon_size = 16;
		m_item_min_width = 88;
	}
	else
	{
		m_height = 28;
		m_radius = 8;
		m_nested_radius = 5;
		m_font_size = 12;
		m_icon_size = 14;
		m_item_min_width = 56;
	}
	setFixedHeight(m_height);
}

int
SegmentedControl::add_item(const QString &v_text, const QString &v_icon_name,
						   const QString &v_dot_token)
{
	Item item;
	item.m_text = v_text;
	item.m_icon_name = v_icon_name;
	item.m_dot_token = v_dot_token;
	if (!item.m_icon_name.isEmpty())
	{
		item.m_icon = IconProvider::pixmap(
			item.m_icon_name,
			ThemeManager::instance().color(QStringLiteral("text.2")),
			m_icon_size);
	}
	m_items.append(item);
	updateGeometry();
	update();
	return m_items.size() - 1;
}

int
SegmentedControl::count() const
{
	return m_items.size();
}

void
SegmentedControl::set_current_index(int v_index, bool v_animated)
{
	if (m_items.isEmpty())
	{
		return;
	}
	v_index = qBound(0, v_index, m_items.size() - 1);
	const bool changed = (v_index != m_current);
	m_current = v_index;

	const QRectF target = item_rect(v_index);
	if (!m_indicator.isValid())
	{
		m_indicator = target;
		m_indicator_from = target;
		m_indicator_to = target;
	}
	else if (v_animated && changed)
	{
		m_indicator_from = m_indicator;
		m_indicator_to = target;
		m_animation->stop();
		m_animation->setStartValue(0.0);
		m_animation->setEndValue(1.0);
		m_animation->start();
	}
	else
	{
		m_animation->stop();
		m_indicator = target;
	}
	update();
	if (changed)
	{
		emit current_changed(v_index);
	}
}

int
SegmentedControl::current_index() const
{
	return m_current;
}

void
SegmentedControl::set_item_text(int v_index, const QString &v_text)
{
	if (v_index < 0 || v_index >= m_items.size())
	{
		return;
	}
	m_items[v_index].m_text = v_text;
	updateGeometry();
	update();
}

void
SegmentedControl::set_item_enabled(int v_index, bool v_enabled)
{
	if (v_index < 0 || v_index >= m_items.size())
	{
		return;
	}
	m_items[v_index].m_enabled = v_enabled;
	update();
}

QRectF
SegmentedControl::track_rect() const
{
	return QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
}

QRectF
SegmentedControl::item_rect(int v_index) const
{
	const QRectF track = track_rect();
	const int inset = 3;
	const qreal item_width
		= (track.width() - inset * 2.0) / qMax(1, m_items.size());
	return QRectF(track.x() + inset + v_index * item_width, track.y() + inset,
				  item_width, track.height() - inset * 2.0);
}

int
SegmentedControl::item_at(const QPoint &v_pos) const
{
	const QRectF track = track_rect();
	if (!track.contains(v_pos))
	{
		return -1;
	}
	for (int i = 0; i < m_items.size(); ++i)
	{
		if (item_rect(i).contains(v_pos))
		{
			return i;
		}
	}
	return -1;
}

void
SegmentedControl::rebuild_icons()
{
	const QColor idle
		= ThemeManager::instance().color(QStringLiteral("text.2"));
	const QColor active
		= ThemeManager::instance().color(QStringLiteral("text.1"));
	for (int i = 0; i < m_items.size(); ++i)
	{
		Item &item = m_items[i];
		if (item.m_icon_name.isEmpty())
		{
			continue;
		}
		item.m_icon = IconProvider::pixmap(
			item.m_icon_name, (i == m_current) ? active : idle, m_icon_size);
	}
}

int
SegmentedControl::item_width() const
{
	QFont font = this->font();
	font.setPixelSize(m_font_size);
	font.setWeight(QFont::DemiBold);
	const QFontMetrics metrics(font);

	int width = 0;
	for (const Item &item : m_items)
	{
		int candidate
			= metrics.horizontalAdvance(item.m_text) + 28; // 左右 padding
		if (!item.m_icon_name.isEmpty())
		{
			candidate += m_icon_size + 6;
		}
		if (!item.m_dot_token.isEmpty())
		{
			candidate += 6 + 6;
		}
		width = qMax(width, candidate);
	}
	return qMax(width, m_item_min_width);
}

QSize
SegmentedControl::sizeHint() const
{
	const int width = qMax(1, m_items.size()) * item_width() + 6;
	return QSize(width, m_height);
}

QSize
SegmentedControl::minimumSizeHint() const
{
	return sizeHint();
}

void
SegmentedControl::resizeEvent(QResizeEvent *v_event)
{
	QWidget::resizeEvent(v_event);
	// 控件首次 set_current_index() 时布局宽度可能尚未确定（构造期调用），
	// 指示块用的 rect 会过期；尺寸确定后无动画时重算一次。
	if (m_animation == nullptr
		|| m_animation->state() != QAbstractAnimation::Running)
	{
		const QRectF target = item_rect(m_current);
		if (m_indicator != target)
		{
			m_indicator = target;
			update();
		}
	}
}

void
SegmentedControl::paintEvent(QPaintEvent *v_event)
{
	Q_UNUSED(v_event)

	const ThemeTokens &tokens = ThemeManager::instance().tokens();
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	if (!isEnabled())
	{
		// 自绘控件必须自己表达禁用态（busy 时模式切换会整体禁用）
		painter.setOpacity(0.45);
	}

	// 轨道
	const QRectF track = track_rect();
	QPainterPath track_path;
	track_path.addRoundedRect(track, m_radius, m_radius);
	painter.fillPath(track_path, tokens.m_bg_subtle);

	// 悬停项浅底（不抢选中块的视觉）
	if (m_hovered >= 0 && m_hovered != m_current
		&& m_items[m_hovered].m_enabled)
	{
		QColor hover = tokens.m_bg_selected;
		hover.setAlpha(140);
		QPainterPath hover_path;
		hover_path.addRoundedRect(item_rect(m_hovered), m_nested_radius,
								  m_nested_radius);
		painter.fillPath(hover_path, hover);
	}

	// 选中块（白底 + 细描边模拟 shadow-sm）
	const QRectF indicator
		= m_indicator.isValid() ? m_indicator : item_rect(m_current);
	QPainterPath indicator_path;
	indicator_path.addRoundedRect(indicator, m_nested_radius, m_nested_radius);
	painter.fillPath(indicator_path, tokens.m_bg_card);
	painter.setPen(QPen(tokens.m_border_default, 1.0));
	painter.drawPath(indicator_path);

	// 文字 / 图标 / 圆点
	for (int i = 0; i < m_items.size(); ++i)
	{
		const Item &item = m_items[i];
		const QRectF area = item_rect(i);
		const bool selected = (i == m_current);

		QFont font = painter.font();
		font.setPixelSize(m_font_size);
		font.setWeight(selected ? QFont::DemiBold : QFont::Medium);
		painter.setFont(font);

		// 先算整体内容宽，再整体居中
		const QFontMetrics metrics(font);
		int content_width = metrics.horizontalAdvance(item.m_text);
		if (!item.m_icon_name.isEmpty())
		{
			content_width += m_icon_size + 6;
		}
		if (!item.m_dot_token.isEmpty())
		{
			content_width += 6 + 6;
		}

		qreal x = area.center().x() - content_width / 2.0;

		if (!item.m_icon_name.isEmpty() && !item.m_icon.isNull())
		{
			const QPixmap icon = IconProvider::pixmap(
				item.m_icon_name, selected ? tokens.m_text_1 : tokens.m_text_2,
				m_icon_size);
			const qreal icon_y = area.center().y() - m_icon_size / 2.0;
			painter.drawPixmap(QPointF(x, icon_y), icon);
			x += m_icon_size + 6;
		}

		painter.setPen(selected ? tokens.m_text_1 : tokens.m_text_2);
		const QRectF text_rect(x, area.y(), content_width + 2, area.height());
		painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter,
						 item.m_text);
		x += metrics.horizontalAdvance(item.m_text) + 6;

		if (!item.m_dot_token.isEmpty())
		{
			const QColor dot_color
				= ThemeManager::instance().color(item.m_dot_token);
			painter.setPen(Qt::NoPen);
			painter.setBrush(dot_color.isValid() ? dot_color : tokens.m_text_2);
			painter.drawEllipse(QPointF(x + 3, area.center().y() + 1), 3, 3);
		}
	}

	// 键盘焦点环（仅键盘模态下可见）
	if (hasFocus())
	{
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(tokens.m_ring, 2.0));
		painter.drawPath(indicator_path);
	}
}

void
SegmentedControl::mousePressEvent(QMouseEvent *v_event)
{
	const int index = item_at(v_event->pos());
	if (index >= 0 && m_items[index].m_enabled)
	{
		set_current_index(index, true);
	}
	QWidget::mousePressEvent(v_event);
}

void
SegmentedControl::mouseMoveEvent(QMouseEvent *v_event)
{
	const int index = item_at(v_event->pos());
	const int hovered = (index >= 0 && m_items[index].m_enabled) ? index : -1;
	if (hovered != m_hovered)
	{
		m_hovered = hovered;
		update();
	}
	QWidget::mouseMoveEvent(v_event);
}

void
SegmentedControl::leaveEvent(QEvent *v_event)
{
	m_hovered = -1;
	update();
	QWidget::leaveEvent(v_event);
}

void
SegmentedControl::keyPressEvent(QKeyEvent *v_event)
{
	switch (v_event->key())
	{
		case Qt::Key_Left:
		case Qt::Key_Up:
			set_current_index(qMax(0, m_current - 1), true);
			return;
		case Qt::Key_Right:
		case Qt::Key_Down:
			set_current_index(qMin(m_items.size() - 1, m_current + 1), true);
			return;
		case Qt::Key_Home:
			set_current_index(0, true);
			return;
		case Qt::Key_End:
			set_current_index(m_items.size() - 1, true);
			return;
		case Qt::Key_Space:
		case Qt::Key_Return:
		case Qt::Key_Enter:
			emit current_changed(m_current);
			return;
		default:
			break;
	}
	QWidget::keyPressEvent(v_event);
}
