#ifndef BASICSETTINGSPANEL_H
#define BASICSETTINGSPANEL_H

// ============================================================================
// 视图层 - 基本设置卡（cc-switch v4 卡片体系）
//
// 表单行：左标签 13px 二级文字、右控件定宽 32 高（LabeledWidget）。
// 负责模型与放大倍数选项的联动（不同图片类型可用模型不同，
// 不同模型支持不同放大倍数），此联动逻辑归属视图层。
//
// 模式差异：图片模式显示「图片类型」；视频模式隐藏该行，模型列表直接提供
// 全部可用模型（默认推荐 realesr-animevideov3），避免「视频页配图片模型」
// 的歧义；两种模式的选择由 MainWindow 分别保存/恢复。
//
// i18n：所有用户可见文案走 tr()；组合框条目用 userData 承载枚举值
// （不再按显示文本反解），retranslate_ui() 重建条目文本并保留当前选择。
// ============================================================================

#include "card_widget.h"

#include "mediasettings.h"

class HintLabel;
class LabeledWidget;
class QComboBox;

class BasicSettingsPanel : public CardWidget
{
	Q_OBJECT

  public:
	explicit BasicSettingsPanel(QWidget *v_parent = nullptr);

	BasicSettings
	collect() const;

	// 图片 / 视频模式：视频模式隐藏图片类型行并显示视频推荐提示
	void
	set_media_kind(MediaKind v_kind);

	// 语言切换：重设卡片标题 / 标签 / 组合框条目与 tooltip，保留当前选择
	void
	retranslate_ui();

  public slots:
	void
	apply(const BasicSettings &v_settings);
	void
	reset_to_defaults();

  private slots:
	void
	on_image_type_changed();
	void
	on_model_changed();

  private:
	// 重建组合框条目（itemData 承载枚举；QSignalBlocker 包裹，不触发联动）
	void
	rebuild_image_type_items();
	void
	rebuild_model_items(ImageType v_image_type, ModelType v_select);
	void
	rebuild_scale_items(ModelType v_model_type, ScaleFactor v_select);
	void
	refresh_video_hint();

	void
	update_tooltips();
	void
	update_label_widths();

	LabeledWidget *m_image_type_row;
	LabeledWidget *m_model_row;
	LabeledWidget *m_scale_row;
	HintLabel *m_video_hint;
	MediaKind m_media_kind = MediaKind::IMAGE;
	QComboBox *m_image_type_combo;
	QComboBox *m_model_combo;
	QComboBox *m_scale_combo;
};

#endif // BASICSETTINGSPANEL_H
