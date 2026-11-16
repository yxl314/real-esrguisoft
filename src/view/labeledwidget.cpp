#include "labeledwidget.h"

namespace
{
// 表单标签默认宽度（「GPU 设备」「放大倍数」等 4~5 字标签都放得下）
const int K_DEFAULT_LABEL_WIDTH = 64;
const int K_DEFAULT_SPACING = 8;
} // namespace

LabeledWidget::LabeledWidget(const QString &v_label_text,
							 QWidget *v_control_widget, QWidget *v_parent)
	: QWidget(v_parent), m_layout(new QHBoxLayout(this)),
	  m_label(new QLabel(v_label_text, this)), m_widget(v_control_widget)
{
	init_ui();
}

LabeledWidget::LabeledWidget(QWidget *v_parent)
	: QWidget(v_parent), m_layout(new QHBoxLayout(this)),
	  m_label(new QLabel(this)), m_widget(nullptr)
{
	init_ui();
}

void
LabeledWidget::set_label(const QString &v_text)
{
	m_label->setText(v_text);
}

void
LabeledWidget::set_widget(QWidget *v_widget)
{
	if (m_widget == v_widget)
	{
		return;
	}
	if (m_widget != nullptr)
	{
		m_layout->removeWidget(m_widget);
		m_widget->deleteLater();
	}
	m_widget = v_widget;
	if (m_widget != nullptr)
	{
		m_widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		m_layout->addWidget(m_widget, 0, Qt::AlignRight | Qt::AlignVCenter);
	}
}

void
LabeledWidget::set_label_width(int v_width)
{
	m_label->setFixedWidth(v_width);
}

void
LabeledWidget::set_spacing(int v_spacing)
{
	m_layout->setSpacing(v_spacing);
}

int
LabeledWidget::measure_label_width(const QString &v_text) const
{
	// QSS 的 font-size 在 polish 后才生效，这里先确保样式已应用再量宽
	m_label->ensurePolished();
	return m_label->fontMetrics().horizontalAdvance(v_text) + 8;
}

int
LabeledWidget::default_label_width()
{
	return K_DEFAULT_LABEL_WIDTH;
}

void
LabeledWidget::init_ui()
{
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->setSpacing(K_DEFAULT_SPACING);

	// 标签：左对齐、垂直居中、13px 二级文字（objectName 交给 app.qss.in 的
	// QLabel#fieldLabel 规则）
	m_label->setObjectName(QStringLiteral("fieldLabel"));
	m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	m_label->setFixedWidth(K_DEFAULT_LABEL_WIDTH);
	m_label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	m_layout->addWidget(m_label, 0, Qt::AlignVCenter);

	// 中间弹性空隙：控件始终贴右，多行之间控件左缘对齐
	m_layout->addStretch(1);

	if (m_widget != nullptr)
	{
		m_widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		m_layout->addWidget(m_widget, 0, Qt::AlignRight | Qt::AlignVCenter);
	}

	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}
