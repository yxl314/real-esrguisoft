#ifndef FUNCTIONMODEGROUP_H
#define FUNCTIONMODEGROUP_H

// ============================================================================
// 视图层 - 功能选择（图片处理 / 视频处理）
//
// 对应 cc-switch v4 模式行里的 SegmentedControl（轨道 36、圆角 10）：
// 图片 / 视频二选一，切换直接驱动主窗口联动；不再使用裸 QRadioButton。
// ============================================================================

#include <QWidget>

#include "mediasettings.h"

class SegmentedControl;

class FunctionModeGroup : public QWidget
{
	Q_OBJECT

  public:
	explicit FunctionModeGroup(QWidget *v_parent = nullptr);

	MediaKind
	media_kind() const;

	// 语言切换：重设两个分段项的显示文本（保留当前选择）
	void
	retranslate_ui();

  public slots:
	void
	set_mode(MediaKind v_kind);

  signals:
	void
	mode_changed(MediaKind v_kind);

  private:
	SegmentedControl *m_segment;
};

#endif // FUNCTIONMODEGROUP_H
