#ifndef SEGMENTED_CONTROL_H
#define SEGMENTED_CONTROL_H

// ============================================================================
// 视图层 - 分段控件（自绘：轨道 + 滑动选中块）
//
// 对应 cc-switch v4 的 SegmentedControl（ui-spec 3.2）：
//   md 档：轨道高 36、圆角 10、内衬 3、选中块圆角 7、文字 13/500；
//   sm 档：轨道高 28、圆角 8、内衬 3、选中块圆角 5、文字 12/500；
//   切换时选中块 200ms ease-out 滑动（首帧不播）。
//
// 用途：图片/视频、单文件/批量、浅色/深色/跟随系统 等二~四选一。
// QSS 无法表达滑动白块，因此整体自绘；颜色全部来自 ThemeManager token。
// ============================================================================

#include <QColor>
#include <QPixmap>
#include <QString>
#include <QVector>

class QResizeEvent;
#include <QWidget>

class QVariantAnimation;

class SegmentedControl : public QWidget
{
	Q_OBJECT

  public:
	enum class SizeMode
	{
		MEDIUM, // 36 高，页面级切换
		SMALL	// 28 高，面板内小切换
	};

	explicit SegmentedControl(QWidget *v_parent = nullptr);

	void
	set_size_mode(SizeMode v_mode);
	SizeMode
	size_mode() const;

	// 追加一项；icon_name 为 Lucide 图标名（可空）；
	// dot_token 为 6px 圆点颜色 token（如 "direct.fill"，可空）
	int
	add_item(const QString &v_text, const QString &v_icon_name = QString(),
			 const QString &v_dot_token = QString());

	int
	count() const;

	void
	set_current_index(int v_index, bool v_animated = true);
	int
	current_index() const;

	void
	set_item_text(int v_index, const QString &v_text);
	void
	set_item_enabled(int v_index, bool v_enabled);

	QSize
	sizeHint() const override;
	QSize
	minimumSizeHint() const override;

  signals:
	void
	current_changed(int v_index);

  protected:
	void
	paintEvent(QPaintEvent *v_event) override;
	void
	resizeEvent(QResizeEvent *v_event) override;
	void
	mousePressEvent(QMouseEvent *v_event) override;
	void
	mouseMoveEvent(QMouseEvent *v_event) override;
	void
	leaveEvent(QEvent *v_event) override;
	void
	keyPressEvent(QKeyEvent *v_event) override;

  private:
	struct Item
	{
		QString m_text;
		QString m_icon_name;
		QString m_dot_token;
		QPixmap m_icon; // 缓存（主题切换时重建）
		bool m_enabled = true;
	};

	QRectF
	track_rect() const;
	QRectF
	item_rect(int v_index) const;
	int
	item_at(const QPoint &v_pos) const;
	void
	rebuild_icons();
	void
	refresh_metrics();
	int
	item_width() const;

	QVector<Item> m_items;
	int m_current = 0;
	int m_hovered = -1;
	SizeMode m_size_mode = SizeMode::MEDIUM;
	int m_height = 36;
	int m_radius = 10;
	int m_nested_radius = 7;
	int m_font_size = 13;
	int m_icon_size = 16;
	int m_item_min_width = 88;

	// 滑动动画
	QVariantAnimation *m_animation = nullptr;
	QRectF m_indicator_from;
	QRectF m_indicator_to;
	QRectF m_indicator;
};

#endif // SEGMENTED_CONTROL_H
