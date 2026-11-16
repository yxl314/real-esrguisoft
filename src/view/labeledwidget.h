#ifndef LABELEDWIDGET_H
#define LABELEDWIDGET_H

// ============================================================================
// 视图层 - 表单行（cc-switch v4 风格）
//
// 一行 = [标签 左对齐 13px 二级文字] …… [控件 右对齐 定宽]：
//   - 标签固定宽度 64（可用 set_label_width 调整），与控件间距 8；
//   - 中间加弹性空隙，让控件整体贴右，形成整齐的两列表单；
//   - 控件高度由调用方设定（常规 32 / 紧凑 28），这里不改控件尺寸。
// ============================================================================

#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

class LabeledWidget : public QWidget
{
	Q_OBJECT

  public:
	explicit LabeledWidget(const QString &v_label_text,
						   QWidget *v_control_widget,
						   QWidget *v_parent = nullptr);
	explicit LabeledWidget(QWidget *v_parent = nullptr);

	void
	set_label(const QString &v_text);
	void
	set_widget(QWidget *v_widget);
	void
	set_label_width(int v_width);
	void
	set_spacing(int v_spacing);

	// 依据文本量出所需标签宽度（含内边距，>= 默认宽度）。供面板在构造 /
	// retranslate 时统一「同一卡片内标签列宽」，避免英文文案被截断。
	int
	measure_label_width(const QString &v_text) const;

	// 默认标签列宽（中文 4~5 字标签的基准值）
	static int
	default_label_width();

	QLabel *
	label() const
	{
		return m_label;
	}

	QWidget *
	widget() const
	{
		return m_widget;
	}

  private:
	void
	init_ui();

	QHBoxLayout *m_layout;
	QLabel *m_label;
	QWidget *m_widget;
};

#endif
