#ifndef APP_PAGE_HEADER_H
#define APP_PAGE_HEADER_H

// ============================================================================
// 视图层 - 页头（52 高，含 1px 底边框）
//
// 对应 cc-switch v4 的 AppPageHeader（ui-spec 2.4）：
//   [前置图标 22] 标题(18/26/600) 副标题(13/20 三级色)  ……  右侧操作区
// 页头同时是无边框窗口的拖拽区：
//   - 空白处按住左键拖动窗口（QWindow::startSystemMove，支持 Win11 贴靠）；
//   - 双击最大化 / 还原（发 title_double_clicked，由主窗口处理）。
// ============================================================================

#include <QString>
#include <QWidget>

class QHBoxLayout;
class QLabel;

class AppPageHeader : public QWidget
{
	Q_OBJECT

  public:
	// 标题层级：全局页/设置页用 PAGE(18)，应用页用 APP(16)
	enum class TitleLevel
	{
		PAGE,
		APP
	};

	explicit AppPageHeader(QWidget *v_parent = nullptr);

	void
	set_leading_icon(const QString &v_icon_name);
	void
	set_title(const QString &v_title);
	void
	set_subtitle(const QString &v_subtitle);
	void
	set_title_level(TitleLevel v_level);

	// 右侧操作区（间距 8）：主按钮放最后
	void
	add_action(QWidget *v_widget);

	// 窗口控制区（间距 4）：最小化 / 最大化 / 关闭
	void
	add_window_action(QWidget *v_widget);

	QString
	title() const;

  signals:
	void
	title_double_clicked();

  protected:
	void
	paintEvent(QPaintEvent *v_event) override;
	void
	mousePressEvent(QMouseEvent *v_event) override;
	void
	mouseDoubleClickEvent(QMouseEvent *v_event) override;

  private:
	QHBoxLayout *m_actions_layout = nullptr;
	QHBoxLayout *m_window_layout = nullptr;
	QLabel *m_leading_icon = nullptr;
	QLabel *m_title = nullptr;
	QLabel *m_subtitle = nullptr;
};

#endif // APP_PAGE_HEADER_H
