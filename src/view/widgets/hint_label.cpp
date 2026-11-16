#include "hint_label.h"

#include <QStyle>

HintLabel::HintLabel(const QString &v_text, QWidget *v_parent)
	: QLabel(v_text, v_parent)
{
	setObjectName(QStringLiteral("hintLabel"));
	setWordWrap(true);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
}

void
HintLabel::set_error(bool v_is_error)
{
	if (property("state").toString()
		== (v_is_error ? QStringLiteral("error") : QString()))
	{
		return;
	}
	setProperty("state", v_is_error ? QStringLiteral("error") : QVariant());
	// 动态属性变化后必须重新 polish 才会应用 QSS 里的属性选择器
	style()->unpolish(this);
	style()->polish(this);
	update();
}
