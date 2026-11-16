#include "functionmodegroup.h"

#include "segmented_control.h"

#include <QHBoxLayout>

// 分段项索引：0=图片，1=视频
namespace
{
const int K_INDEX_IMAGE = 0;
const int K_INDEX_VIDEO = 1;
} // namespace

FunctionModeGroup::FunctionModeGroup(QWidget *v_parent)
	: QWidget(v_parent), m_segment(new SegmentedControl(this))
{
	setObjectName(QStringLiteral("functionModeGroup"));

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	m_segment->set_size_mode(SegmentedControl::SizeMode::MEDIUM);
	m_segment->add_item(FunctionModeGroup::tr("图片处理"),
						QStringLiteral("image"));
	m_segment->add_item(FunctionModeGroup::tr("视频处理"),
						QStringLiteral("video"));
	m_segment->set_current_index(K_INDEX_IMAGE, false);
	layout->addWidget(m_segment);

	connect(m_segment, &SegmentedControl::current_changed, this,
			[this](int v_index)
			{
				emit mode_changed(v_index == K_INDEX_VIDEO ? MediaKind::VIDEO
														   : MediaKind::IMAGE);
			});
}

MediaKind
FunctionModeGroup::media_kind() const
{
	return (m_segment->current_index() == K_INDEX_VIDEO) ? MediaKind::VIDEO
														 : MediaKind::IMAGE;
}

void
FunctionModeGroup::set_mode(MediaKind v_kind)
{
	// set_current_index 只在索引真的变化时才发 current_changed，
	// 与旧版 QRadioButton::setChecked 的行为一致（避免无谓的模式联动）。
	const int index
		= (v_kind == MediaKind::VIDEO) ? K_INDEX_VIDEO : K_INDEX_IMAGE;
	m_segment->set_current_index(index, true);
}

void
FunctionModeGroup::retranslate_ui()
{
	m_segment->set_item_text(K_INDEX_IMAGE, FunctionModeGroup::tr("图片处理"));
	m_segment->set_item_text(K_INDEX_VIDEO, FunctionModeGroup::tr("视频处理"));
}
