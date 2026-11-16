#include "processmodegroup.h"

#include "segmented_control.h"

#include <QHBoxLayout>

// 分段项索引：0=单文件，1=批量
namespace
{
const int K_INDEX_SINGLE = 0;
const int K_INDEX_BATCH = 1;
} // namespace

ProcessModeGroup::ProcessModeGroup(QWidget *v_parent)
	: QWidget(v_parent), m_segment(new SegmentedControl(this))
{
	setObjectName(QStringLiteral("processModeGroup"));

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	// 处理方式属于页面内的次级切换，视觉权重低于页面级「图片/视频」模式
	m_segment->set_size_mode(SegmentedControl::SizeMode::SMALL);
	m_segment->add_item(ProcessModeGroup::tr("单文件处理"));
	m_segment->add_item(ProcessModeGroup::tr("批量处理"),
						QStringLiteral("images"));
	m_segment->set_current_index(K_INDEX_SINGLE, false);
	layout->addWidget(m_segment);

	connect(m_segment, &SegmentedControl::current_changed, this,
			[this](int v_index)
			{ emit batch_mode_changed(v_index == K_INDEX_BATCH); });
}

bool
ProcessModeGroup::is_batch_mode() const
{
	return m_segment->current_index() == K_INDEX_BATCH;
}

void
ProcessModeGroup::set_batch_mode(bool v_batch)
{
	m_segment->set_current_index(v_batch ? K_INDEX_BATCH : K_INDEX_SINGLE,
								 true);
}

void
ProcessModeGroup::retranslate_ui()
{
	m_segment->set_item_text(K_INDEX_SINGLE,
							 ProcessModeGroup::tr("单文件处理"));
	m_segment->set_item_text(K_INDEX_BATCH, ProcessModeGroup::tr("批量处理"));
}
