#include "videosettingsgroup.h"

#include "action_button.h"
#include "hint_label.h"
#include "labeledwidget.h"
#include "tooltips.h"

#include <QComboBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace
{
// 卡内边距（紧凑密度）
const int K_CARD_PADDING = 12;
// 控件高度（ui-spec 1.6：常规 32）
const int K_CONTROL_HEIGHT = 32;
// 英文条目（H.264 / Balanced 等）均短于中文场景，宽度保持原值
const int K_ENCODING_WIDTH = 112;
const int K_QUALITY_WIDTH = 96;

template <typename T>
T
combo_enum_value(const QComboBox *v_combo, T v_default)
{
	const QVariant data = v_combo->currentData();
	return data.isValid() ? static_cast<T>(data.toInt()) : v_default;
}

template <typename T>
void
select_combo_enum(QComboBox *v_combo, T v_value)
{
	const int index = v_combo->findData(static_cast<int>(v_value));
	if (index >= 0)
	{
		v_combo->setCurrentIndex(index);
	}
}
} // namespace

VideoSettingsGroup::VideoSettingsGroup(QWidget *v_parent)
	: CardWidget(VideoSettingsGroup::tr("视频设置"), v_parent),
	  m_refresh_btn(new ActionButton(ActionButton::Variant::ICON_COMPACT,
									 QString(), QStringLiteral("refresh-cw"),
									 this)),
	  m_encoding_row(nullptr), m_quality_row(nullptr),
	  m_encoding_combo(new QComboBox(this)),
	  m_quality_combo(new QComboBox(this)),
	  m_image_mode_hint(new HintLabel(
		  VideoSettingsGroup::tr("切换到视频处理后可设置编码与质量"), this)),
	  m_controls_visible(false)
{
	setProperty("panelName", QStringLiteral("videoSettings"));
	set_padding(K_CARD_PADDING);
	body_layout()->setSpacing(8);

	m_refresh_btn->setToolTip(VideoSettingsGroup::tr("恢复视频默认设置"));
	add_header_action(m_refresh_btn);
	// 只恢复本组的编码格式 / 编码质量，不再触发全局会话重置（F5 才是全局重置）
	connect(m_refresh_btn, &QPushButton::clicked, this,
			&VideoSettingsGroup::reset_to_defaults);

	// —— 编码格式 ——
	rebuild_encoding_items(VideoEncoding::H264);
	m_encoding_combo->setFixedWidth(K_ENCODING_WIDTH);
	m_encoding_combo->setFixedHeight(K_CONTROL_HEIGHT);
	m_encoding_row = new LabeledWidget(VideoSettingsGroup::tr("编码格式"),
									   m_encoding_combo, this);
	body_layout()->addWidget(m_encoding_row);

	// —— 编码质量 ——
	rebuild_quality_items(VideoQuality::BALANCED);
	m_quality_combo->setFixedWidth(K_QUALITY_WIDTH);
	m_quality_combo->setFixedHeight(K_CONTROL_HEIGHT);
	m_quality_row = new LabeledWidget(VideoSettingsGroup::tr("编码质量"),
									  m_quality_combo, this);
	body_layout()->addWidget(m_quality_row);

	m_image_mode_hint->hide();
	body_layout()->addWidget(m_image_mode_hint);

	update_tooltips();
	update_label_widths();
}

VideoEncoding
VideoSettingsGroup::encoding() const
{
	return combo_enum_value(m_encoding_combo, VideoEncoding::H264);
}

VideoQuality
VideoSettingsGroup::quality() const
{
	return combo_enum_value(m_quality_combo, VideoQuality::BALANCED);
}

void
VideoSettingsGroup::set_controls_visible(bool v_visible)
{
	m_controls_visible = v_visible;
	m_encoding_row->setVisible(v_visible);
	m_quality_row->setVisible(v_visible);
	m_refresh_btn->setVisible(v_visible);
	m_image_mode_hint->setVisible(!v_visible);

	m_encoding_combo->setEnabled(v_visible);
	m_quality_combo->setEnabled(v_visible);
}

void
VideoSettingsGroup::reset_to_defaults()
{
	select_combo_enum(m_encoding_combo, VideoEncoding::H264);
	select_combo_enum(m_quality_combo, VideoQuality::BALANCED);
}

void
VideoSettingsGroup::rebuild_encoding_items(VideoEncoding v_select)
{
	const QSignalBlocker blocker(m_encoding_combo);
	m_encoding_combo->clear();
	const VideoEncoding encodings[]
		= { VideoEncoding::H264, VideoEncoding::H265, VideoEncoding::VP9,
			VideoEncoding::AV1 };
	for (const VideoEncoding encoding : encodings)
	{
		// 编码名是技术标识（H.264 / H.265 / VP9 / AV1），不翻译
		m_encoding_combo->addItem(video_encoding_to_string(encoding),
								  static_cast<int>(encoding));
	}
	select_combo_enum(m_encoding_combo, v_select);
	if (m_encoding_combo->currentIndex() < 0)
	{
		m_encoding_combo->setCurrentIndex(0);
	}
}

void
VideoSettingsGroup::rebuild_quality_items(VideoQuality v_select)
{
	const QSignalBlocker blocker(m_quality_combo);
	m_quality_combo->clear();
	const VideoQuality qualities[]
		= { VideoQuality::FAST, VideoQuality::BALANCED, VideoQuality::QUALITY };
	for (const VideoQuality quality : qualities)
	{
		m_quality_combo->addItem(video_quality_to_string(quality),
								 static_cast<int>(quality));
	}
	select_combo_enum(m_quality_combo, v_select);
	if (m_quality_combo->currentIndex() < 0)
	{
		m_quality_combo->setCurrentIndex(0);
	}
}

void
VideoSettingsGroup::update_tooltips()
{
	m_encoding_combo->setToolTip(VideoSettingsGroup::tr(
		"视频编码格式：H.264 / H.265 / VP9 / AV1，影响输出体积与播放兼容性"));
	// 质量档位实际映射到 CRF + 编码 preset（见 domain VideoEncodeParams），
	// 用 CRF 说明与「快速/均衡/质量」选项一致；旧的 preset 列表
	// （ultrafast..veryslow）与 UI 选项不对应。
	m_quality_combo->setToolTip(ToolTips::crf_value());
}

void
VideoSettingsGroup::update_label_widths()
{
	int width = LabeledWidget::default_label_width();
	const LabeledWidget *rows[] = { m_encoding_row, m_quality_row };
	for (const LabeledWidget *row : rows)
	{
		if (row != nullptr && row->label() != nullptr)
		{
			width = qMax(width, row->measure_label_width(row->label()->text()));
		}
	}
	for (LabeledWidget *row : { m_encoding_row, m_quality_row })
	{
		if (row != nullptr)
		{
			row->set_label_width(width);
		}
	}
}

void
VideoSettingsGroup::retranslate_ui()
{
	set_title(VideoSettingsGroup::tr("视频设置"));
	m_encoding_row->set_label(VideoSettingsGroup::tr("编码格式"));
	m_quality_row->set_label(VideoSettingsGroup::tr("编码质量"));
	m_image_mode_hint->setText(
		VideoSettingsGroup::tr("切换到视频处理后可设置编码与质量"));
	m_refresh_btn->setToolTip(VideoSettingsGroup::tr("恢复视频默认设置"));

	// 先记住当前选择，再重建条目文本，最后按 data 恢复
	const VideoEncoding encoding
		= combo_enum_value(m_encoding_combo, VideoEncoding::H264);
	const VideoQuality quality
		= combo_enum_value(m_quality_combo, VideoQuality::BALANCED);
	rebuild_encoding_items(encoding);
	rebuild_quality_items(quality);

	// 保持外部设定的可见性 / 可用性（retranslate 只改文案）
	set_controls_visible(m_controls_visible);
	update_tooltips();
	update_label_widths();
}
