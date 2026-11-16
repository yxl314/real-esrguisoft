#ifndef CARD_WIDGET_H
#define CARD_WIDGET_H

// ============================================================================
// 视图层 - 卡片容器（圆角 10、1px 描边、可选阴影 / 可选选中态）
//
// 对应 cc-switch v4 的 .pcard / 设置分组卡：白底、10 圆角、1px 描边，
// 内容内边距由 body 布局控制。可选：
//   - 标题区（SectionTitle + 右侧操作）；
//   - active 选中态（蓝底蓝边，像 cc-switch 里「当前使用」的供应商卡片）；
//   - hover 描边加深（列表项式卡片）。
// 背景/描边/圆角交给 app.qss.in（objectName=card + 动态属性），
// 阴影用 QGraphicsDropShadowEffect（QSS 表达不了 shadow）。
// ============================================================================

#include <QFrame>
#include <QString>

class QHBoxLayout;
class QLabel;
class QVBoxLayout;

class CardWidget : public QFrame
{
	Q_OBJECT

  public:
	explicit CardWidget(QWidget *v_parent = nullptr);
	explicit CardWidget(const QString &v_title, QWidget *v_parent = nullptr);

	// 卡片内容布局（调用方往这里 addWidget）
	QVBoxLayout *
	body_layout() const
	{
		return m_body_layout;
	}

	// 卡片标题（会自动创建/显示标题行）
	void
	set_title(const QString &v_title);
	QString
	title() const;

	// 标题右侧追加控件（如「添加」按钮、状态徽章）
	void
	add_header_action(QWidget *v_widget);

	// 选中态（蓝底蓝边）
	void
	set_active(bool v_active);
	bool
	is_active() const;

	// 列表项式卡片：hover 时描边加深
	void
	set_hoverable(bool v_hoverable);

	// 阴影（默认开启；嵌套在滚动区里时如出现裁切可关掉）
	void
	set_shadow_enabled(bool v_enabled);
	void
	refresh_shadow(); // 主题切换后重新取阴影颜色

	// 内容内边距（默认 16）
	void
	set_padding(int v_padding);

  private:
	void
	init_ui();

	QVBoxLayout *m_body_layout = nullptr;
	QWidget *m_header_row = nullptr;
	QHBoxLayout *m_header_layout = nullptr;
	QLabel *m_title_label = nullptr;
	bool m_active = false;
	bool m_hoverable = false;
	bool m_shadow_enabled = true;
	int m_padding = 16;
};

#endif // CARD_WIDGET_H
