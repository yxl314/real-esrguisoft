#ifndef HINT_LABEL_H
#define HINT_LABEL_H

// ============================================================================
// 视图层 - 辅助说明文字（12/18，三级文字色，默认自动换行）
// ============================================================================

#include <QLabel>

class HintLabel : public QLabel
{
	Q_OBJECT

  public:
	explicit HintLabel(const QString &v_text = QString(),
					   QWidget *v_parent = nullptr);

	// 切换成危险说明（表单校验失败时用，如「路径不存在」）
	void
	set_error(bool v_is_error);
};

#endif // HINT_LABEL_H
