#ifndef ADVANCEDSETTINGSPANEL_H
#define ADVANCEDSETTINGSPANEL_H

// ============================================================================
// 视图层 - 高级参数卡（cc-switch v4 卡片体系，可折叠）
//
// 卡头 = SectionTitle「高级参数」+ 右侧 chevron 图标按钮；点击展开 / 收起
// 表单区（输出格式 / GPU / 图块 / TTA / 线程数）。TTA 用 ToggleSwitch，
// 不再使用裸 QCheckBox（指示器样式由壳层统一维护）。
//
// i18n：文案走 tr()；组合框条目用 userData 承载枚举/数值（图块 -1 = 自动），
// retranslate_ui() 重建条目文本并按 data 恢复选择。
// ============================================================================

#include "card_widget.h"

#include "mediasettings.h"

class ActionButton;
class LabeledWidget;
class QComboBox;
class QLineEdit;
class QWidget;
class ToggleSwitch;

class AdvancedSettingsPanel : public CardWidget
{
	Q_OBJECT

  public:
	explicit AdvancedSettingsPanel(QWidget *v_parent = nullptr);

	AdvancedSettings
	collect() const;

	// 媒体模式切换时重排「输出格式」候选（图片格式 / 视频格式）
	void
	apply_media_kind(MediaKind v_kind);

	// 展开 / 收起表单区（图片模式默认展开，补齐右栏并露出输出格式；
	// 视频模式默认收起，三卡高度平衡）
	void
	set_expanded(bool v_expanded);

	// 语言切换：重设卡片标题 / 标签 / 组合框条目与 tooltip，保留当前选择
	void
	retranslate_ui();

  public slots:
	void
	apply(const AdvancedSettings &v_settings);
	void
	reset_to_defaults();

  private slots:
	void
	toggle_expanded();

  private:
	void
	refresh_expand_icon();

	// 重建组合框条目（itemData 承载枚举/数值；QSignalBlocker 包裹）
	void
	rebuild_output_format_items(OutputFormat v_select);
	void
	rebuild_gpu_items(GPUDevice v_select);
	void
	rebuild_tile_size_items(int v_select); // -1 = 自动
	void
	update_tooltips();
	void
	update_label_widths();

	ActionButton *m_expand_btn;
	QWidget *m_content;
	LabeledWidget *m_output_format_row;
	LabeledWidget *m_gpu_row;
	LabeledWidget *m_tile_row;
	LabeledWidget *m_tta_row;
	LabeledWidget *m_thread_row;
	QComboBox *m_output_format_combo;
	QComboBox *m_gpu_device_combo;
	QComboBox *m_tile_size_combo;
	ToggleSwitch *m_tta_mode_switch;
	QLineEdit *m_thread_count_edit;
	MediaKind m_media_kind;
	bool m_expanded;
};

#endif // ADVANCEDSETTINGSPANEL_H
