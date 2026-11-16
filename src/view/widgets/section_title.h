#ifndef SECTION_TITLE_H
#define SECTION_TITLE_H

// ============================================================================
// 视图层 - 区块标题（15/22 semibold，用于卡片内部小标题）
// ============================================================================

#include <QLabel>

class SectionTitle : public QLabel
{
	Q_OBJECT

  public:
	explicit SectionTitle(const QString &v_text = QString(),
						  QWidget *v_parent = nullptr);
};

#endif // SECTION_TITLE_H
