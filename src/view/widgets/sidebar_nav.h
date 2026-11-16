#ifndef SIDEBAR_NAV_H
#define SIDEBAR_NAV_H

// ============================================================================
// 视图层 - 左侧导航栏（自绘）
//
// 对应 cc-switch v4 的 Sidebar（ui-spec 2.3）：
//   - 展开宽 200 / 收起宽 72；
//   - 顶条 44 高：应用 Logo 22×22 + 品牌名 14/600 + 折叠按钮 28×28；
//   - 导航项 28 高、圆角 6、图标 18、文字 13；选中态 bg-selected + 字重 500；
//   - 组间 1px 分隔线；底部主题切换行（浅色 / 深色 / 跟随系统）。
//
// 整条侧栏由一个自绘控件完成（精确控制 28 行高、图标间距与选中态），
// 因此这里是少数不使用 QSS 的背景色块；颜色全部取自 ThemeManager token。
// ============================================================================

#include <QHash>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

class SidebarNav : public QWidget
{
	Q_OBJECT

  public:
	explicit SidebarNav(QWidget *v_parent = nullptr);

	// 品牌区（Logo 取自应用图标 resources/app.png）
	void
	set_brand(const QString &v_title, const QString &v_subtitle = QString());

	// 追加导航项；返回有效的 id 数量
	void
	add_item(const QString &v_id, const QString &v_text,
			 const QString &v_icon_name);

	// 追加一条组间分隔线
	void
	add_separator();

	// 更新导航项显示文本（语言切换用；按 id 定位，找不到时忽略）
	void
	set_item_text(const QString &v_id, const QString &v_text);

	// 语言切换：导航项文本由 MainWindow 逐项 set_item_text，这里刷新底部主题标签
	void
	retranslate_ui();

	void
	set_current(const QString &v_id);
	QString
	current_id() const;

	// 展开 / 收起（收起后只显示图标，行高 32、宽 48 居中）
	void
	set_collapsed(bool v_collapsed);
	bool
	is_collapsed() const;

	// 底部主题按钮显示的状态：0=跟随系统 1=浅色 2=深色
	void
	set_theme_mode(int v_mode);

	QSize
	sizeHint() const override;

  signals:
	void
	item_activated(const QString &v_id);
	void
	collapsed_changed(bool v_collapsed);
	void
	theme_button_clicked();

  protected:
	void
	paintEvent(QPaintEvent *v_event) override;
	void
	mousePressEvent(QMouseEvent *v_event) override;
	void
	mouseMoveEvent(QMouseEvent *v_event) override;
	void
	leaveEvent(QEvent *v_event) override;
	void
	keyPressEvent(QKeyEvent *v_event) override;
	void
	resizeEvent(QResizeEvent *v_event) override;

  private:
	enum class RowType
	{
		NAV,
		SEPARATOR,
		THEME
	};

	struct Row
	{
		RowType m_type = RowType::NAV;
		QString m_id;
		QString m_text;
		QString m_icon;
		QRect m_rect;
	};

	void
	relayout();
	void
	update_width();
	int
	row_at(const QPoint &v_pos) const;
	bool
	is_brand_toggle_at(const QPoint &v_pos) const;
	QString
	theme_icon_name() const;
	QString
	theme_text() const;
	void
	repaint_all();

	QVector<Row> m_rows;
	QString m_current_id;
	QString m_brand_title = QStringLiteral("RealESRGAN");
	QString m_brand_subtitle;
	bool m_collapsed = false;
	int m_theme_mode = 0;
	int m_hovered_row = -1;
	bool m_hovered_brand_toggle = false;
	bool m_hovered_theme = false;
	QPixmap m_logo;
};

#endif // SIDEBAR_NAV_H
