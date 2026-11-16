#include "progresspanel.h"

#include "action_button.h"
#include "hint_label.h"
#include "mainviewinterface.h"
#include "tooltips.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QStyle>
#include <QVBoxLayout>

namespace
{
// 卡内边距（紧凑密度）
const int K_CARD_PADDING = 12;
} // namespace

ProgressPanel::ProgressPanel(QWidget *v_parent)
	: CardWidget(ProgressPanel::tr("处理进度"), v_parent),
	  m_progress_bar(new QProgressBar(this)), m_detail_label(new QLabel(this)),
	  m_current_file_label(new QLabel(this)),
	  m_percent_label(new QLabel(QStringLiteral("0%"), this)),
	  m_state_message_label(new HintLabel(QString(), this))
{
	setProperty("panelName", QStringLiteral("progress"));
	set_padding(K_CARD_PADDING);
	body_layout()->setSpacing(8);

	m_percent_label->setObjectName(QStringLiteral("progressPercent"));
	m_percent_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_percent_label->hide();
	add_header_action(m_percent_label);

	// —— 8px 细进度条（高度由 app.qss.in 的 QProgressBar 规则锁定）——
	m_progress_bar->setObjectName(QStringLiteral("processProgress"));
	m_progress_bar->setRange(0, 100);
	m_progress_bar->setValue(0);
	m_progress_bar->setTextVisible(false);
	m_progress_bar->setFixedHeight(8);
	m_progress_bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	// —— 主操作 + 停止：放在进度条右侧，符合常见任务进度布局 ——
	// 主操作只显示图标（开始/暂停/继续），文案放 tooltip / 无障碍名称。
	m_primary_btn = new ActionButton(ActionButton::Variant::PRIMARY_ICON,
									 QString(), QStringLiteral("play"), this);
	m_primary_btn->setEnabled(false);
	m_stop_btn = new ActionButton(ActionButton::Variant::ICON_COMPACT,
								  QString(), QStringLiteral("square"), this);
	m_stop_btn->setEnabled(false);
	m_stop_btn->setToolTip(ToolTips::stop_button());
	m_stop_btn->setAccessibleName(ToolTips::stop_button());
	m_stop_btn->set_hover_tone(IconProvider::Tone::DANGER);

	QHBoxLayout *progress_row = new QHBoxLayout();
	progress_row->setContentsMargins(0, 0, 0, 0);
	progress_row->setSpacing(8);
	progress_row->addWidget(m_progress_bar, 1, Qt::AlignVCenter);
	progress_row->addWidget(m_primary_btn, 0, Qt::AlignVCenter);
	progress_row->addWidget(m_stop_btn, 0, Qt::AlignVCenter);
	body_layout()->addLayout(progress_row);

	connect(m_primary_btn, &QPushButton::clicked, this,
			[this]()
			{
				// 防止「双击开始」的第二次点击立刻变成暂停：同一语义按钮在
				// 300ms 内只接受一次动作，状态由控制器下发后再恢复。
				if (m_click_guard.isValid() && m_click_guard.elapsed() < 300)
				{
					return;
				}
				m_click_guard.start();
				if (m_last_control_state.m_primary_action
					== PrimaryAction::START)
				{
					emit start_requested();
				}
				else
				{
					emit pause_resume_requested();
				}
			});
	connect(m_stop_btn, &QPushButton::clicked, this,
			&ProgressPanel::stop_requested);

	// —— 详情行：第 X 个 / 共 Y 个 · Z% ——
	QHBoxLayout *detail_row = new QHBoxLayout();
	detail_row->setContentsMargins(0, 0, 0, 0);
	detail_row->setSpacing(8);
	m_detail_label->setObjectName(QStringLiteral("fieldLabel"));
	detail_row->addWidget(m_detail_label, 0, Qt::AlignVCenter);
	detail_row->addStretch(1);
	body_layout()->addLayout(detail_row);

	// —— 状态补充行（取消 / 停止收尾 / 失败原因，空时隐藏）——
	m_state_message_label->setObjectName(
		QStringLiteral("progressStateMessage"));
	m_state_message_label->setWordWrap(true);
	m_state_message_label->hide();
	body_layout()->addWidget(m_state_message_label);

	// —— 当前文件行（空时隐藏，避免占位）——
	m_current_file_label->setObjectName(QStringLiteral("mediaInfoLabel"));
	m_current_file_label->setWordWrap(true);
	m_current_file_label->hide();
	body_layout()->addWidget(m_current_file_label);

	apply_progress_tone(ProcessState::IDLE);
	update_detail_text(0, 0, 0);
	refresh_current_file();
}

void
ProgressPanel::apply_status(const ProcessStatus &v_status)
{
	m_last_status = v_status;
	m_has_status = true;

	m_progress_bar->setValue(v_status.m_progress_percent);
	m_percent_label->setText(
		ProgressPanel::tr("%1%").arg(v_status.m_progress_percent));
	update_detail_text(v_status.m_current_index, v_status.m_total_files,
					   v_status.m_progress_percent);
	refresh_percent_visibility();
	refresh_state_message();

	m_current_file_path = v_status.m_current_file;
	refresh_current_file();

	apply_progress_tone(v_status.m_state);
}

void
ProgressPanel::set_detail(int v_current, int v_total, int v_percent)
{
	update_detail_text(v_current, v_total, v_percent);
}

void
ProgressPanel::set_input_ready(bool v_ready)
{
	if (m_input_ready == v_ready)
	{
		return;
	}
	m_input_ready = v_ready;
	update_detail_text(m_detail_current, m_detail_total, m_detail_percent);
}

void
ProgressPanel::set_batch_mode(bool v_batch_mode)
{
	if (m_batch_mode == v_batch_mode)
	{
		return;
	}
	m_batch_mode = v_batch_mode;
	update_detail_text(m_detail_current, m_detail_total, m_detail_percent);
}

void
ProgressPanel::apply_control_state(const ControlState &v_state)
{
	m_last_control_state = v_state;

	QString text;
	QString icon;
	switch (v_state.m_primary_action)
	{
		case PrimaryAction::PAUSE:
			text = ProgressPanel::tr("暂停处理");
			icon = QStringLiteral("pause");
			break;
		case PrimaryAction::RESUME:
			text = ProgressPanel::tr("继续处理");
			icon = QStringLiteral("play");
			break;
		case PrimaryAction::START:
			text = ProgressPanel::tr("开始处理");
			icon = QStringLiteral("play");
			break;
	}
	m_primary_btn->setText(QString());
	m_primary_btn->set_icon_name(icon);
	m_primary_btn->setToolTip(text);
	m_primary_btn->setAccessibleName(text);
	m_primary_btn->setEnabled(v_state.m_primary_enabled);
	m_stop_btn->setEnabled(v_state.m_stop_enabled);
}

void
ProgressPanel::retranslate_ui()
{
	set_title(ProgressPanel::tr("处理进度"));
	update_detail_text(m_detail_current, m_detail_total, m_detail_percent);
	refresh_current_file();
	refresh_state_message();
	apply_control_state(m_last_control_state);
	m_stop_btn->setToolTip(ToolTips::stop_button());
	m_stop_btn->setAccessibleName(ToolTips::stop_button());
}

void
ProgressPanel::refresh_percent_visibility()
{
	// 空闲且没有中断进度时不显示无意义的 0%；运行/暂停/完成/失败均显示。
	const bool show = m_has_status
					  && (m_last_status.m_state != ProcessState::IDLE
						  || m_last_status.m_progress_percent > 0);
	m_percent_label->setVisible(show);
}

void
ProgressPanel::refresh_state_message()
{
	QString message;
	if (m_has_status)
	{
		const QString raw = m_last_status.m_message.trimmed();
		if (m_last_status.m_state == ProcessState::FAILED)
		{
			message = raw;
		}
		else if (m_last_status.m_state == ProcessState::IDLE && !raw.isEmpty()
				 && raw != ProgressPanel::tr("待处理")
				 && raw != ProgressPanel::tr("就绪"))
		{
			message = raw;
		}
	}
	m_state_message_label->setText(message);
	m_state_message_label->set_error(
		m_has_status && m_last_status.m_state == ProcessState::FAILED);
	m_state_message_label->setVisible(!message.isEmpty());
}

void
ProgressPanel::update_detail_text(int v_current, int v_total, int v_percent)
{
	m_detail_current = v_current;
	m_detail_total = v_total;
	m_detail_percent = v_percent;

	if (v_total <= 0)
	{
		if (m_has_status && m_last_status.m_state == ProcessState::RUNNING)
		{
			m_detail_label->setText(ProgressPanel::tr("正在启动处理…"));
			return;
		}
		if (m_has_status && m_last_status.m_state == ProcessState::FAILED)
		{
			m_detail_label->setText(ProgressPanel::tr("处理失败"));
			return;
		}
		if (m_input_ready)
		{
			m_detail_label->setText(
				ProgressPanel::tr("准备就绪，点击开始处理"));
		}
		else
		{
			m_detail_label->setText(
				m_batch_mode ? ProgressPanel::tr("请选择输入文件夹")
							 : ProgressPanel::tr("请选择输入文件或目录"));
		}
		return;
	}
	// 百分比已经显示在卡头右侧，详情行不再重复
	m_detail_label->setText(
		ProgressPanel::tr("第 %1 个 / 共 %2 个").arg(v_current).arg(v_total));
}

void
ProgressPanel::refresh_current_file()
{
	// 当前文件：只显示文件名，完整路径放在 tooltip 里
	if (m_current_file_path.isEmpty())
	{
		m_current_file_label->clear();
		m_current_file_label->setToolTip(QString());
		m_current_file_label->hide();
		return;
	}
	const QString file_name = QFileInfo(m_current_file_path).fileName();
	m_current_file_label->setText(
		ProgressPanel::tr("当前：%1")
			.arg(file_name.isEmpty() ? m_current_file_path : file_name));
	m_current_file_label->setToolTip(m_current_file_path);
	m_current_file_label->show();
}

void
ProgressPanel::apply_progress_tone(ProcessState v_state)
{
	QString tone;
	switch (v_state)
	{
		case ProcessState::COMPLETED:
			tone = QStringLiteral("success");
			break;
		case ProcessState::PAUSED:
			tone = QStringLiteral("warning");
			break;
		case ProcessState::FAILED:
			tone = QStringLiteral("danger");
			break;
		case ProcessState::IDLE:
		case ProcessState::RUNNING:
			break;
	}
	m_progress_bar->setProperty("tone", tone.isEmpty() ? QVariant() : tone);
	m_progress_bar->style()->unpolish(m_progress_bar);
	m_progress_bar->style()->polish(m_progress_bar);
	m_progress_bar->update();
}
