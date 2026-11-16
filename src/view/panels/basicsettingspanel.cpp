#include "basicsettingspanel.h"

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
// 下拉框宽度：右栏约 300 宽，标签列 + 间距后仍有富余。
// 模型名最长的是 "realesrgan-x4plus-anime"（13px 下约 150px），加上左侧
// padding 8、边框 2 与 Qt 为箭头预留的 24px，190 可完整显示；最小窗口
// （1000 宽）下右栏控件可用宽度约 194，190 不会撑坏布局。
// 英文条目（Real photo / Anime image / 4×）均短于中文场景，宽度无需调整。
const int K_IMAGE_TYPE_WIDTH = 128;
const int K_MODEL_WIDTH = 190;
const int K_SCALE_WIDTH = 72;

// 组合框枚举值读取 / 选中（userData 承载枚举，不依赖显示文本）
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

ModelType
default_model_for(ImageType v_image_type)
{
	return v_image_type == ImageType::ANIME_IMAGE
			   ? ModelType::REAL_ESRGAN_X4PLUS_ANIME
			   : ModelType::REAL_ESRGAN_X4PLUS;
}

QString
model_tooltip(ModelType v_model_type)
{
	switch (v_model_type)
	{
		case ModelType::REAL_ESRGAN_X4PLUS:
			return ToolTips::model_real_esrga_nx4plus();
		case ModelType::REAL_ESRGAN_X4PLUS_ANIME:
			return ToolTips::model_real_esrga_nx4plus_anime();
		case ModelType::REAL_ESR_ANIMEVIDEOV3:
			return ToolTips::model_real_esr_anime_video_v3();
	}
	return ToolTips::model_real_esrga_nx4plus();
}
} // namespace

BasicSettingsPanel::BasicSettingsPanel(QWidget *v_parent)
	: CardWidget(BasicSettingsPanel::tr("基本设置"), v_parent),
	  m_image_type_row(nullptr), m_model_row(nullptr), m_scale_row(nullptr),
	  m_video_hint(nullptr), m_image_type_combo(new QComboBox(this)),
	  m_model_combo(new QComboBox(this)), m_scale_combo(new QComboBox(this))
{
	setProperty("panelName", QStringLiteral("basicSettings"));
	set_padding(K_CARD_PADDING);
	body_layout()->setSpacing(8);

	// —— 图片类型 ——
	rebuild_image_type_items();
	m_image_type_combo->setFixedWidth(K_IMAGE_TYPE_WIDTH);
	m_image_type_combo->setFixedHeight(K_CONTROL_HEIGHT);
	m_image_type_row = new LabeledWidget(BasicSettingsPanel::tr("图片类型"),
										 m_image_type_combo, this);
	body_layout()->addWidget(m_image_type_row);

	// —— 模型 ——
	m_model_combo->setFixedWidth(K_MODEL_WIDTH);
	m_model_combo->setFixedHeight(K_CONTROL_HEIGHT);
	m_model_row = new LabeledWidget(BasicSettingsPanel::tr("模型"),
									m_model_combo, this);
	body_layout()->addWidget(m_model_row);

	// —— 放大倍数 ——
	m_scale_combo->setFixedWidth(K_SCALE_WIDTH);
	m_scale_combo->setFixedHeight(K_CONTROL_HEIGHT);
	m_scale_row = new LabeledWidget(BasicSettingsPanel::tr("放大倍数"),
									m_scale_combo, this);
	body_layout()->addWidget(m_scale_row);

	// —— 视频模式推荐提示（仅视频模式显示）——
	m_video_hint = new HintLabel(
		BasicSettingsPanel::tr(
			"视频推荐模型：realesr-animevideov3（2x/3x 更快）。"),
		this);
	m_video_hint->setWordWrap(true);
	m_video_hint->hide();
	body_layout()->addWidget(m_video_hint);

	connect(m_image_type_combo,
			QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&BasicSettingsPanel::on_image_type_changed);
	connect(m_model_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
			this, &BasicSettingsPanel::on_model_changed);

	update_label_widths();
	refresh_video_hint();

	// 初始填充：与旧版 setupUI 相同的初始化顺序
	on_image_type_changed();
}

BasicSettings
BasicSettingsPanel::collect() const
{
	BasicSettings settings;
	settings.m_image_type
		= combo_enum_value(m_image_type_combo, ImageType::REAL_IMAGE);
	settings.m_model_type = combo_enum_value(
		m_model_combo, default_model_for(settings.m_image_type));
	settings.m_scale_factor
		= combo_enum_value(m_scale_combo, ScaleFactor::SCALE4X);
	return settings;
}

void
BasicSettingsPanel::set_media_kind(MediaKind v_kind)
{
	if (m_media_kind == v_kind)
	{
		return;
	}
	m_media_kind = v_kind;
	const bool video_mode = (v_kind == MediaKind::VIDEO);
	m_image_type_row->setVisible(!video_mode);
	m_video_hint->setVisible(video_mode);

	// 当前选择尽量保留；模型不在新模式列表中时由 rebuild 回退到第一项
	const ImageType image_type
		= combo_enum_value(m_image_type_combo, ImageType::REAL_IMAGE);
	const ModelType model_type
		= combo_enum_value(m_model_combo, default_model_for(image_type));
	const ScaleFactor scale_factor
		= combo_enum_value(m_scale_combo, ScaleFactor::SCALE4X);
	rebuild_model_items(image_type, model_type);
	rebuild_scale_items(model_type, scale_factor);
	update_tooltips();
	update_label_widths();
	refresh_video_hint();
}

void
BasicSettingsPanel::apply(const BasicSettings &v_settings)
{
	// 一次性重建三列并恢复目标选择；期间屏蔽联动信号，避免中间态重建
	const QSignalBlocker image_type_blocker(m_image_type_combo);
	const QSignalBlocker model_blocker(m_model_combo);
	const QSignalBlocker scale_blocker(m_scale_combo);

	select_combo_enum(m_image_type_combo, v_settings.m_image_type);
	rebuild_model_items(v_settings.m_image_type, v_settings.m_model_type);
	rebuild_scale_items(v_settings.m_model_type, v_settings.m_scale_factor);
	update_tooltips();
	refresh_video_hint();
}

void
BasicSettingsPanel::reset_to_defaults()
{
	{
		const QSignalBlocker blocker(m_image_type_combo);
		select_combo_enum(m_image_type_combo, ImageType::REAL_IMAGE);
	}
	on_image_type_changed();
}

void
BasicSettingsPanel::on_image_type_changed()
{
	const ImageType image_type
		= combo_enum_value(m_image_type_combo, ImageType::REAL_IMAGE);
	const ModelType model_type = default_model_for(image_type);
	rebuild_model_items(image_type, model_type);
	rebuild_scale_items(model_type, ScaleFactor::SCALE4X);
	update_tooltips();
}

void
BasicSettingsPanel::on_model_changed()
{
	const ModelType model_type
		= combo_enum_value(m_model_combo, ModelType::REAL_ESRGAN_X4PLUS);
	rebuild_scale_items(model_type, ScaleFactor::SCALE4X);
	update_tooltips();
	refresh_video_hint();
}

void
BasicSettingsPanel::refresh_video_hint()
{
	if (m_video_hint == nullptr)
	{
		return;
	}
	const ModelType model_type
		= combo_enum_value(m_model_combo, ModelType::REAL_ESRGAN_X4PLUS);
	const bool image_model
		= (m_media_kind == MediaKind::VIDEO
		   && model_type != ModelType::REAL_ESR_ANIMEVIDEOV3);
	if (image_model)
	{
		m_video_hint->setText(BasicSettingsPanel::tr(
			"当前是图片模型，视频会逐帧处理，速度明显更慢；"
			"推荐 realesr-animevideov3。"));
	}
	else
	{
		m_video_hint->setText(BasicSettingsPanel::tr(
			"视频推荐模型：realesr-animevideov3（2x/3x 更快）。"));
	}
	m_video_hint->set_error(image_model);
}

void
BasicSettingsPanel::rebuild_image_type_items()
{
	const QSignalBlocker blocker(m_image_type_combo);
	m_image_type_combo->clear();
	const auto add_item = [this](ImageType v_type, const QString &v_tooltip)
	{
		m_image_type_combo->addItem(image_type_to_string(v_type),
									static_cast<int>(v_type));
		m_image_type_combo->setItemData(m_image_type_combo->count() - 1,
										v_tooltip, Qt::ToolTipRole);
	};
	add_item(ImageType::REAL_IMAGE, ToolTips::image_type_real());
	add_item(ImageType::ANIME_IMAGE, ToolTips::image_type_anime());
}

void
BasicSettingsPanel::rebuild_model_items(ImageType v_image_type,
										ModelType v_select)
{
	const QSignalBlocker blocker(m_model_combo);
	m_model_combo->clear();
	const auto add_item = [this](ModelType v_type)
	{
		// 模型显示名为技术模型名（不翻译）
		m_model_combo->addItem(model_type_to_string(v_type),
							   static_cast<int>(v_type));
		m_model_combo->setItemData(m_model_combo->count() - 1,
								   model_tooltip(v_type), Qt::ToolTipRole);
	};
	if (m_media_kind == MediaKind::VIDEO)
	{
		// 视频模式直接提供全部模型；推荐项排第一，MainWindow 会默认选中
		add_item(ModelType::REAL_ESR_ANIMEVIDEOV3);
		add_item(ModelType::REAL_ESRGAN_X4PLUS);
		add_item(ModelType::REAL_ESRGAN_X4PLUS_ANIME);
	}
	else if (v_image_type == ImageType::ANIME_IMAGE)
	{
		add_item(ModelType::REAL_ESRGAN_X4PLUS_ANIME);
		add_item(ModelType::REAL_ESR_ANIMEVIDEOV3);
	}
	else
	{
		add_item(ModelType::REAL_ESRGAN_X4PLUS);
	}
	select_combo_enum(m_model_combo, v_select);
	if (m_model_combo->currentIndex() < 0)
	{
		m_model_combo->setCurrentIndex(0);
	}
}

void
BasicSettingsPanel::rebuild_scale_items(ModelType v_model_type,
										ScaleFactor v_select)
{
	const QSignalBlocker blocker(m_scale_combo);
	m_scale_combo->clear();
	const auto add_item = [this](ScaleFactor v_factor)
	{
		m_scale_combo->addItem(scale_factor_to_string(v_factor),
							   static_cast<int>(v_factor));
	};
	if (v_model_type == ModelType::REAL_ESR_ANIMEVIDEOV3)
	{
		add_item(ScaleFactor::SCALE2X);
		add_item(ScaleFactor::SCALE3X);
		add_item(ScaleFactor::SCALE4X);
	}
	else
	{
		add_item(ScaleFactor::SCALE4X);
	}
	select_combo_enum(m_scale_combo, v_select);
	if (m_scale_combo->currentIndex() < 0)
	{
		m_scale_combo->setCurrentIndex(0);
	}
}

void
BasicSettingsPanel::update_tooltips()
{
	const ImageType image_type
		= combo_enum_value(m_image_type_combo, ImageType::REAL_IMAGE);
	m_image_type_combo->setToolTip(image_type == ImageType::ANIME_IMAGE
									   ? ToolTips::image_type_anime()
									   : ToolTips::image_type_real());

	const ModelType model_type
		= combo_enum_value(m_model_combo, ModelType::REAL_ESRGAN_X4PLUS);
	m_model_combo->setToolTip(model_tooltip(model_type));
	// 放大倍数候选随模型变化：默认模型只有 4x，2x/3x 仅 anime-video 提供；
	// 用对应提示避免列出当前不可选的 2x/3x。
	m_scale_combo->setToolTip(
		m_scale_combo->count() > 1
			? ToolTips::scale_option()
			: BasicSettingsPanel::tr("放大倍数：当前模型固定为 4x。"));
}

void
BasicSettingsPanel::update_label_widths()
{
	int width = LabeledWidget::default_label_width();
	const LabeledWidget *rows[]
		= { m_image_type_row, m_model_row, m_scale_row };
	for (const LabeledWidget *row : rows)
	{
		if (row != nullptr && row->label() != nullptr)
		{
			width = qMax(width, row->measure_label_width(row->label()->text()));
		}
	}
	for (LabeledWidget *row : { m_image_type_row, m_model_row, m_scale_row })
	{
		if (row != nullptr)
		{
			row->set_label_width(width);
		}
	}
}

void
BasicSettingsPanel::retranslate_ui()
{
	set_title(BasicSettingsPanel::tr("基本设置"));
	m_image_type_row->set_label(BasicSettingsPanel::tr("图片类型"));
	m_model_row->set_label(BasicSettingsPanel::tr("模型"));
	m_scale_row->set_label(BasicSettingsPanel::tr("放大倍数"));
	// 先记住当前枚举值，再重建条目文本，最后按 data 恢复选择
	const ImageType image_type
		= combo_enum_value(m_image_type_combo, ImageType::REAL_IMAGE);
	const ModelType model_type
		= combo_enum_value(m_model_combo, default_model_for(image_type));
	const ScaleFactor scale_factor
		= combo_enum_value(m_scale_combo, ScaleFactor::SCALE4X);

	rebuild_image_type_items();
	select_combo_enum(m_image_type_combo, image_type);
	rebuild_model_items(image_type, model_type);
	rebuild_scale_items(model_type, scale_factor);

	update_tooltips();
	update_label_widths();
	refresh_video_hint();
}
