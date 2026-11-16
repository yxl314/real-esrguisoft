#ifndef PROCESSMODEGROUP_H
#define PROCESSMODEGROUP_H

// ============================================================================
// 视图层 - 处理模式（单文件处理 / 批量处理）
//
// 模式行第二个 SegmentedControl，二选一；切换驱动批量模式联动。
// ============================================================================

#include <QWidget>

class SegmentedControl;

class ProcessModeGroup : public QWidget
{
	Q_OBJECT

  public:
	explicit ProcessModeGroup(QWidget *v_parent = nullptr);

	bool
	is_batch_mode() const;

	// 语言切换：重设两个分段项的显示文本（保留当前选择）
	void
	retranslate_ui();

  public slots:
	void
	set_batch_mode(bool v_batch);

  signals:
	void
	batch_mode_changed(bool v_batch_mode);

  private:
	SegmentedControl *m_segment;
};

#endif // PROCESSMODEGROUP_H
