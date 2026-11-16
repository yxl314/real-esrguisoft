#include "section_title.h"

SectionTitle::SectionTitle(const QString &v_text, QWidget *v_parent)
	: QLabel(v_text, v_parent)
{
	setObjectName(QStringLiteral("sectionTitle"));
	setWordWrap(false);
	// 行高按设计稿 15/22：用固定最小高保证与说明文字的对齐
	setMinimumHeight(22);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}
