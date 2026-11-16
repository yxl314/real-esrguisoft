#ifndef VIDEOSETTINGSGROUP_H
#define VIDEOSETTINGSGROUP_H

// ============================================================================
// 视图层 - 视频设置卡（cc-switch v4 卡片体系）
//
// 编码格式 / 编码质量两行表单 + 卡头「恢复视频默认设置」图标按钮；图片模式下隐藏
// 编码行、显示「仅视频模式可用」提示（卡片外壳保留，避免布局跳动）。
//
// i18n：文案走 tr()；组合框条目用 userData 承载枚举（编码名不翻译，改为 data
// 后不再按显示文本反解），retranslate_ui() 重建条目文本并保留当前选择。
// ============================================================================

#include "card_widget.h"

#include "mediasettings.h"

class ActionButton;
class HintLabel;
class LabeledWidget;
class QComboBox;

class VideoSettingsGroup : public CardWidget
{
	Q_OBJECT

  public:
	explicit VideoSettingsGroup(QWidget *v_parent = nullptr);

	VideoEncoding
	encoding() const;
	VideoQuality
	quality() const;

	// 图片模式下隐藏编码相关控件、显示提示（卡片外壳保留）
	void
	set_controls_visible(bool v_visible);

	// 语言切换：重设卡片标题 / 标签 / 提示 / 组合框条目与 tooltip
	void
	retranslate_ui();

  public slots:
	void
	reset_to_defaults();

  private:
	// 重建组合框条目（itemData 承载枚举；QSignalBlocker 包裹）
	void
	rebuild_encoding_items(VideoEncoding v_select);
	void
	rebuild_quality_items(VideoQuality v_select);
	void
	update_tooltips();
	void
	update_label_widths();

	ActionButton *m_refresh_btn;
	LabeledWidget *m_encoding_row;
	LabeledWidget *m_quality_row;
	QComboBox *m_encoding_combo;
	QComboBox *m_quality_combo;
	HintLabel *m_image_mode_hint;
	bool m_controls_visible;
};

#endif // VIDEOSETTINGSGROUP_H
