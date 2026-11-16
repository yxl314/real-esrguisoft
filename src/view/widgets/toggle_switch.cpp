#include "toggle_switch.h"

#include <QPainter>
#include <QVariantAnimation>

#include "theme_manager.h"

namespace
{
const int SWITCH_WIDTH = 30;
const int SWITCH_HEIGHT = 18;
const int KNOB_SIZE = 14;
const int ANIMATION_DURATION_MS = 150;
} // namespace

ToggleSwitch::ToggleSwitch(QWidget *v_parent) : QAbstractButton(v_parent)
{
	setCheckable(true);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::StrongFocus);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

	m_animation = new QVariantAnimation(this);
	m_animation->setDuration(ANIMATION_DURATION_MS);
	m_animation->setEasingCurve(QEasingCurve::OutCubic);
	connect(m_animation, &QVariantAnimation::valueChanged, this,
			[this](const QVariant &v_value)
			{
				m_position = v_value.toReal();
				update();
			});

	connect(this, &QAbstractButton::toggled, this,
			[this](bool v_checked) { animate_to(v_checked); });

	connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
			[this]() { update(); });

	m_position = isChecked() ? 1.0 : 0.0;
}

void
ToggleSwitch::set_neutral_tone(bool v_neutral)
{
	m_neutral = v_neutral;
	update();
}

bool
ToggleSwitch::neutral_tone() const
{
	return m_neutral;
}

QSize
ToggleSwitch::sizeHint() const
{
	return QSize(SWITCH_WIDTH, SWITCH_HEIGHT);
}

QSize
ToggleSwitch::minimumSizeHint() const
{
	return sizeHint();
}

void
ToggleSwitch::animate_to(bool v_checked)
{
	m_animation->stop();
	m_animation->setStartValue(m_position);
	m_animation->setEndValue(v_checked ? 1.0 : 0.0);
	m_animation->start();
}

void
ToggleSwitch::paintEvent(QPaintEvent *v_event)
{
	Q_UNUSED(v_event)

	const ThemeTokens &tokens = ThemeManager::instance().tokens();
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const QRectF track(0, 0, SWITCH_WIDTH, SWITCH_HEIGHT);

	// 开 / 关轨道颜色插值（150ms 过渡）
	QColor off_color
		= m_neutral ? tokens.m_border_strong : tokens.m_control_off;
	QColor on_color = m_neutral ? tokens.m_text_2 : tokens.m_action_bg;
	QColor track_color(
		qRound(off_color.red()
			   + (on_color.red() - off_color.red()) * m_position),
		qRound(off_color.green()
			   + (on_color.green() - off_color.green()) * m_position),
		qRound(off_color.blue()
			   + (on_color.blue() - off_color.blue()) * m_position));
	if (!isEnabled())
	{
		track_color.setAlpha(110);
	}

	painter.setPen(Qt::NoPen);
	painter.setBrush(track_color);
	painter.drawRoundedRect(track, SWITCH_HEIGHT / 2.0, SWITCH_HEIGHT / 2.0);

	// 圆点位置：关时 2px 起，开时右移 12px
	const qreal travel = SWITCH_WIDTH - KNOB_SIZE - 4.0; // 12
	const QRectF knob(2.0 + travel * m_position,
					  (SWITCH_HEIGHT - KNOB_SIZE) / 2.0, KNOB_SIZE, KNOB_SIZE);

	QColor knob_color = m_neutral ? tokens.m_bg_card : tokens.m_action_fg;
	if (!isEnabled())
	{
		knob_color.setAlpha(160);
	}
	painter.setBrush(knob_color);
	painter.setPen(Qt::NoPen);
	painter.drawEllipse(knob);

	// 键盘焦点环
	if (hasFocus())
	{
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(tokens.m_ring, 2.0));
		painter.drawRoundedRect(track.adjusted(-2, -2, 2, 2),
								SWITCH_HEIGHT / 2.0 + 2,
								SWITCH_HEIGHT / 2.0 + 2);
	}
}
