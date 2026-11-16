#include "advancedsettingspanel.h"

#include "action_button.h"
#include "labeledwidget.h"
#include "toggle_switch.h"
#include "tooltips.h"

#include <QComboBox>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace
{
// 卡内边距（紧凑密度）
const int K_CARD_PADDING = 12;
// 控件高度（ui-spec 1.6：常规 32）
const int K_CONTROL_HEIGHT = 32;
// 英文条目（Original / Auto / H.264 等）均短于中文场景，宽度保持原值
const int K_OUTPUT_FORMAT_WIDTH = 120;
const int K_GPU_WIDTH = 96;
const int K_TILE_WIDTH = 96;
const int K_THREAD_WIDTH = 96;
// TTA 行是开关，比普通表单行矮，给一个与 32 高控件对齐的最小行高
const int K_SWITCH_ROW_HEIGHT = 32;

// 图块大小候选值（自动由条目 userData=-1 表示）
const QList<int> &
tile_size_values()
{
	static const QList<int> values{ 32, 64, 128, 256, 512, 1024 };
	return values;
}

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

AdvancedSettingsPanel::AdvancedSettingsPanel(QWidget *v_parent)
	: CardWidget(AdvancedSettingsPanel::tr("高级参数"), v_parent),
	  m_expand_btn(new ActionButton(ActionButton::Variant::ICON_COMPACT,
									QString(), QStringLiteral("chevron-right"),
									this)),
	  m_content(new QWidget(this)), m_output_format_row(nullptr),
	  m_gpu_row(nullptr), m_tile_row(nullptr), m_tta_row(nullptr),
	  m_thread_row(nullptr), m_output_format_combo(new QComboBox(m_content)),
	  m_gpu_device_combo(new QComboBox(m_content)),
	  m_tile_size_combo(new QComboBox(m_content)),
	  m_tta_mode_switch(new ToggleSwitch(m_content)),
	  m_thread_count_edit(
		  new QLineEdit(safe_default_thread_count(), m_content)),
	  m_media_kind(MediaKind::IMAGE), m_expanded(false)
{
	setProperty("panelName", QStringLiteral("advancedSettings"));
	set_padding(K_CARD_PADDING);
	body_layout()->setSpacing(8);

	m_expand_btn->setToolTip(AdvancedSettingsPanel::tr("展开 / 收起高级参数"));
	add_header_action(m_expand_btn);
	connect(m_expand_btn, &QPushButton::clicked, this,
			&AdvancedSettingsPanel::toggle_expanded);

	// 表单区：折叠时整体隐藏，不占布局高度
	QVBoxLayout *content_layout = new QVBoxLayout(m_content);
	content_layout->setContentsMargins(0, 0, 0, 0);
	content_layout->setSpacing(8);

	// —— 输出格式：初始为图片格式选项 ——
	rebuild_output_format_items(OutputFormat::ORIGINAL);
	m_output_format_combo->setFixedWidth(K_OUTPUT_FORMAT_WIDTH);
	m_output_format_combo->setFixedHeight(K_CONTROL_HEIGHT);
	m_output_format_row
		= new LabeledWidget(AdvancedSettingsPanel::tr("输出格式"),
							m_output_format_combo, m_content);
	content_layout->addWidget(m_output_format_row);

	// —— GPU 设备 ——
	rebuild_gpu_items(GPUDevice::AUTO);
	m_gpu_device_combo->setFixedWidth(K_GPU_WIDTH);
	m_gpu_device_combo->setFixedHeight(K_CONTROL_HEIGHT);
	m_gpu_row = new LabeledWidget(AdvancedSettingsPanel::tr("GPU 设备"),
								  m_gpu_device_combo, m_content);
	content_layout->addWidget(m_gpu_row);

	// —— 图块大小 ——
	rebuild_tile_size_items(-1);
	m_tile_size_combo->setFixedWidth(K_TILE_WIDTH);
	m_tile_size_combo->setFixedHeight(K_CONTROL_HEIGHT);
	m_tile_row = new LabeledWidget(AdvancedSettingsPanel::tr("图块大小"),
								   m_tile_size_combo, m_content);
	content_layout->addWidget(m_tile_row);

	// —— TTA 模式（开关，替代裸 QCheckBox）——
	m_tta_row = new LabeledWidget(AdvancedSettingsPanel::tr("TTA 模式"),
								  m_tta_mode_switch, m_content);
	m_tta_row->setMinimumHeight(K_SWITCH_ROW_HEIGHT);
	content_layout->addWidget(m_tta_row);

	// —— 线程数 ——
	m_thread_count_edit->setFixedWidth(K_THREAD_WIDTH);
	m_thread_count_edit->setFixedHeight(K_CONTROL_HEIGHT);
	m_thread_row = new LabeledWidget(AdvancedSettingsPanel::tr("线程数"),
									 m_thread_count_edit, m_content);
	content_layout->addWidget(m_thread_row);

	body_layout()->addWidget(m_content);
	update_tooltips();
	update_label_widths();

	// 折叠态：隐藏整个表单区（卡头保留）
	m_content->hide();
}

AdvancedSettings
AdvancedSettingsPanel::collect() const
{
	AdvancedSettings settings;
	settings.m_output_format
		= combo_enum_value(m_output_format_combo, OutputFormat::ORIGINAL);
	settings.m_gpu_device
		= combo_enum_value(m_gpu_device_combo, GPUDevice::AUTO);
	const int tile_size = m_tile_size_combo->currentData().toInt();
	settings.m_tile_size
		= tile_size < 0 ? tile_size_auto_token() : QString::number(tile_size);
	settings.m_tta_mode = m_tta_mode_switch->isChecked();
	settings.m_thread_count = m_thread_count_edit->text();
	return settings;
}

void
AdvancedSettingsPanel::set_expanded(bool v_expanded)
{
	if (m_expanded == v_expanded)
	{
		return;
	}
	m_expanded = v_expanded;
	m_content->setVisible(m_expanded);
	refresh_expand_icon();
}

void
AdvancedSettingsPanel::apply_media_kind(MediaKind v_kind)
{
	if (m_media_kind == v_kind)
	{
		return;
	}
	m_media_kind = v_kind;
	const OutputFormat current_format
		= combo_enum_value(m_output_format_combo, OutputFormat::ORIGINAL);
	rebuild_output_format_items(current_format);
	update_tooltips();
}

void
AdvancedSettingsPanel::apply(const AdvancedSettings &v_settings)
{
	const QSignalBlocker output_blocker(m_output_format_combo);
	const QSignalBlocker gpu_blocker(m_gpu_device_combo);
	const QSignalBlocker tile_blocker(m_tile_size_combo);

	select_combo_enum(m_output_format_combo, v_settings.m_output_format);
	select_combo_enum(m_gpu_device_combo, v_settings.m_gpu_device);

	int tile_size = -1;
	const QString auto_token = tile_size_auto_token();
	if (!v_settings.m_tile_size.isEmpty()
		&& v_settings.m_tile_size != auto_token)
	{
		bool ok = false;
		const int value = v_settings.m_tile_size.toInt(&ok);
		if (ok)
		{
			tile_size = value;
		}
	}
	select_combo_enum(m_tile_size_combo, tile_size);

	m_tta_mode_switch->setChecked(v_settings.m_tta_mode);
	m_thread_count_edit->setText(v_settings.m_thread_count);
}

void
AdvancedSettingsPanel::reset_to_defaults()
{
	select_combo_enum(m_output_format_combo, OutputFormat::ORIGINAL);
	select_combo_enum(m_gpu_device_combo, GPUDevice::AUTO);
	select_combo_enum(m_tile_size_combo, -1);
	m_tta_mode_switch->setChecked(false);
	m_thread_count_edit->setText(safe_default_thread_count());
}

void
AdvancedSettingsPanel::toggle_expanded()
{
	m_expanded = !m_expanded;
	m_content->setVisible(m_expanded);
	refresh_expand_icon();
}

void
AdvancedSettingsPanel::refresh_expand_icon()
{
	// QSS 不能旋转图标，展开 / 收起各用一张 chevron
	m_expand_btn->set_icon_name(m_expanded ? QStringLiteral("chevron-down")
										   : QStringLiteral("chevron-right"));
	m_expand_btn->setToolTip(m_expanded
								 ? AdvancedSettingsPanel::tr("收起高级参数")
								 : AdvancedSettingsPanel::tr("展开高级参数"));
}

void
AdvancedSettingsPanel::rebuild_output_format_items(OutputFormat v_select)
{
	const QSignalBlocker blocker(m_output_format_combo);
	m_output_format_combo->clear();
	const QList<OutputFormat> options = m_media_kind == MediaKind::VIDEO
											? video_output_format_options()
											: image_output_format_options();
	for (const OutputFormat format : options)
	{
		m_output_format_combo->addItem(output_format_to_string(format),
									   static_cast<int>(format));
	}
	select_combo_enum(m_output_format_combo, v_select);
	if (m_output_format_combo->currentIndex() < 0)
	{
		m_output_format_combo->setCurrentIndex(0);
	}
}

void
AdvancedSettingsPanel::rebuild_gpu_items(GPUDevice v_select)
{
	const QSignalBlocker blocker(m_gpu_device_combo);
	m_gpu_device_combo->clear();
	const GPUDevice devices[] = { GPUDevice::AUTO, GPUDevice::GPU0,
								  GPUDevice::GPU1, GPUDevice::GPU2 };
	for (const GPUDevice device : devices)
	{
		m_gpu_device_combo->addItem(gpu_device_to_string(device),
									static_cast<int>(device));
	}
	select_combo_enum(m_gpu_device_combo, v_select);
	if (m_gpu_device_combo->currentIndex() < 0)
	{
		m_gpu_device_combo->setCurrentIndex(0);
	}
}

void
AdvancedSettingsPanel::rebuild_tile_size_items(int v_select)
{
	const QSignalBlocker blocker(m_tile_size_combo);
	m_tile_size_combo->clear();
	m_tile_size_combo->addItem(AdvancedSettingsPanel::tr("自动"), -1);
	for (const int value : tile_size_values())
	{
		m_tile_size_combo->addItem(QString::number(value), value);
	}
	select_combo_enum(m_tile_size_combo, v_select);
	if (m_tile_size_combo->currentIndex() < 0)
	{
		m_tile_size_combo->setCurrentIndex(0);
	}
}

void
AdvancedSettingsPanel::update_tooltips()
{
	// 输出格式候选随媒体类型切换（图片格式 / 视频容器）；tooltip 必须与当前
	// 列表一致：图片用原有图片格式说明，视频用容器/编码组合说明。
	m_output_format_combo->setToolTip(
		m_media_kind == MediaKind::VIDEO
			? AdvancedSettingsPanel::tr(
				  "视频输出格式：原格式 / mp4 / mkv / avi / mov / webm。\n"
				  "原格式保持输入容器（GIF 输入按 mp4 输出）；不兼容的容器 / "
				  "编码组合会在开始前拒绝。")
			: ToolTips::output_format());
	m_gpu_device_combo->setToolTip(ToolTips::gpu_id());
	m_tile_size_combo->setToolTip(ToolTips::tile_size());
	m_tta_mode_switch->setToolTip(ToolTips::tta_mode());
	m_thread_count_edit->setToolTip(
		AdvancedSettingsPanel::tr("Real-ESRGAN 的 -j 线程配置，格式如 1:1:2"));
}

void
AdvancedSettingsPanel::update_label_widths()
{
	int width = LabeledWidget::default_label_width();
	const LabeledWidget *rows[] = { m_output_format_row, m_gpu_row, m_tile_row,
									m_tta_row, m_thread_row };
	for (const LabeledWidget *row : rows)
	{
		if (row != nullptr && row->label() != nullptr)
		{
			width = qMax(width, row->measure_label_width(row->label()->text()));
		}
	}
	for (LabeledWidget *row : { m_output_format_row, m_gpu_row, m_tile_row,
								m_tta_row, m_thread_row })
	{
		if (row != nullptr)
		{
			row->set_label_width(width);
		}
	}
}

void
AdvancedSettingsPanel::retranslate_ui()
{
	set_title(AdvancedSettingsPanel::tr("高级参数"));
	m_output_format_row->set_label(AdvancedSettingsPanel::tr("输出格式"));
	m_gpu_row->set_label(AdvancedSettingsPanel::tr("GPU 设备"));
	m_tile_row->set_label(AdvancedSettingsPanel::tr("图块大小"));
	m_tta_row->set_label(AdvancedSettingsPanel::tr("TTA 模式"));
	m_thread_row->set_label(AdvancedSettingsPanel::tr("线程数"));

	// 先记住当前选择，再重建条目文本，最后按 data 恢复
	const OutputFormat output_format
		= combo_enum_value(m_output_format_combo, OutputFormat::ORIGINAL);
	const GPUDevice gpu_device
		= combo_enum_value(m_gpu_device_combo, GPUDevice::AUTO);
	const int tile_size = m_tile_size_combo->currentData().toInt();

	rebuild_output_format_items(output_format);
	rebuild_gpu_items(gpu_device);
	rebuild_tile_size_items(tile_size);

	refresh_expand_icon();
	update_tooltips();
	update_label_widths();
}
