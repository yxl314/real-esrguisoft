#include "mediaplayerwidget.h"

#include "action_button.h"

#include <QAudioOutput>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace
{
// 视频画面最小尺寸：左右两卡布局下预览区约 210 宽，这里给一个可压缩下限
const int K_VIDEO_MIN_WIDTH = 160;
const int K_VIDEO_MIN_HEIGHT = 90;
// 时间码固定宽度（「00:00 / 00:00」在 12px 下约 74）
const int K_TIME_LABEL_WIDTH = 82;
} // namespace

MediaPlayerWidget::MediaPlayerWidget(const QString &v_video_widget_object_name,
									 QWidget *v_parent)
	: QWidget(v_parent), m_player(new QMediaPlayer(this)),
	  m_audio_output(new QAudioOutput(this)),
	  m_video_widget(new QVideoWidget(this)), m_controls(new QWidget(this)),
	  m_slider(new QSlider(Qt::Horizontal, this)),
	  m_play_pause_btn(new ActionButton(ActionButton::Variant::ICON_COMPACT,
										QString(), QStringLiteral("play"),
										this)),
	  m_time_label(new QLabel(this))
{
	m_video_widget->setObjectName(v_video_widget_object_name);
	m_video_widget->setMinimumSize(K_VIDEO_MIN_WIDTH, K_VIDEO_MIN_HEIGHT);
	m_video_widget->setSizePolicy(QSizePolicy::Expanding,
								  QSizePolicy::Expanding);
	m_video_widget->hide();

	m_player->setVideoOutput(m_video_widget);
	m_player->setAudioOutput(m_audio_output);

	// —— 播放按钮：图标按钮（播放 / 暂停随状态切换图标；
	//    objectName 必须保留 actionButton，否则 app.qss.in 的样式规则失配）——
	m_play_pause_btn->setToolTip(MediaPlayerWidget::tr("播放 / 暂停"));

	// —— 细滑块：轨道 4px（app.qss.in 的 QSlider 规则）——
	m_slider->setObjectName(QStringLiteral("playerSlider"));
	m_slider->setRange(0, 0);
	m_slider->setFixedHeight(20);
	m_slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	// —— 时间码 ——
	m_time_label->setObjectName(QStringLiteral("mediaPlayerTime"));
	m_time_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_time_label->setText(QStringLiteral("00:00 / 00:00"));
	// QSS 的字体在 polish 后生效，先确保样式应用再按字体度量算最坏宽度；
	// 超过 1 小时时 format_time 输出 h:mm:ss / h:mm:ss，固定 82px 会截断。
	m_time_label->ensurePolished();
	const QFontMetrics time_metrics(m_time_label->font());
	m_time_label->setFixedWidth(qMax(
		K_TIME_LABEL_WIDTH,
		time_metrics.horizontalAdvance(QStringLiteral("00:00:00 / 00:00:00"))
			+ 8));

	m_controls->setObjectName(QStringLiteral("mediaPlayerControls"));
	m_controls->setAttribute(Qt::WA_StyledBackground, true);
	QHBoxLayout *controls_layout = new QHBoxLayout(m_controls);
	controls_layout->setContentsMargins(0, 8, 0, 0);
	controls_layout->setSpacing(8);
	controls_layout->addWidget(m_play_pause_btn, 0, Qt::AlignVCenter);
	controls_layout->addWidget(m_slider, 1, Qt::AlignVCenter);
	controls_layout->addWidget(m_time_label, 0, Qt::AlignVCenter);
	m_controls->hide();

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);
	layout->addWidget(m_video_widget, 1);
	layout->addWidget(m_controls);

	connect(m_player, &QMediaPlayer::positionChanged, this,
			&MediaPlayerWidget::on_position_changed);
	connect(m_player, &QMediaPlayer::durationChanged, this,
			&MediaPlayerWidget::on_duration_changed);
	connect(m_player, &QMediaPlayer::playbackStateChanged, this,
			[this]() { update_play_pause_button(); });
	connect(m_slider, &QSlider::sliderMoved, this,
			&MediaPlayerWidget::on_slider_moved);
	connect(m_play_pause_btn, &QPushButton::clicked, this,
			&MediaPlayerWidget::on_play_pause_clicked);
}

void
MediaPlayerWidget::open_and_play(const QString &v_file_path)
{
	m_video_widget->show();
	m_controls->show();
	m_player->setSource(QUrl::fromLocalFile(v_file_path));
	m_player->setLoops(QMediaPlayer::Infinite);
	m_player->play();
	update_play_pause_button();
}

void
MediaPlayerWidget::retranslate_ui()
{
	update_play_pause_button();
}

void
MediaPlayerWidget::stop_and_hide()
{
	m_player->stop();
	m_video_widget->hide();
	m_controls->hide();
	m_slider->setRange(0, 0);
	m_slider->setValue(0);
	m_time_label->setText(QStringLiteral("00:00 / 00:00"));
}

void
MediaPlayerWidget::on_position_changed(qint64 v_position)
{
	if (!m_slider->isSliderDown())
	{
		m_slider->blockSignals(true);
		m_slider->setValue(static_cast<int>(v_position));
		m_slider->blockSignals(false);
	}
	refresh_time_label();
}

void
MediaPlayerWidget::on_duration_changed(qint64 v_duration)
{
	m_slider->setRange(0, static_cast<int>(v_duration));
	refresh_time_label();
}

void
MediaPlayerWidget::on_slider_moved(int v_position)
{
	m_player->setPosition(v_position);
	if (m_player->playbackState() == QMediaPlayer::StoppedState)
	{
		m_player->play();
	}
	refresh_time_label();
}

void
MediaPlayerWidget::on_play_pause_clicked()
{
	if (m_player->playbackState() == QMediaPlayer::PlayingState)
	{
		m_player->pause();
	}
	else
	{
		m_player->play();
	}
	update_play_pause_button();
}

void
MediaPlayerWidget::update_play_pause_button()
{
	const bool playing
		= (m_player->playbackState() == QMediaPlayer::PlayingState);
	m_play_pause_btn->set_icon_name(playing ? QStringLiteral("pause")
											: QStringLiteral("play"));
	m_play_pause_btn->setToolTip(playing ? MediaPlayerWidget::tr("暂停")
										 : MediaPlayerWidget::tr("播放"));
}

QString
MediaPlayerWidget::format_time(qint64 v_milliseconds)
{
	const qint64 total_seconds = qMax<qint64>(0, v_milliseconds) / 1000;
	const qint64 hours = total_seconds / 3600;
	const qint64 minutes = (total_seconds % 3600) / 60;
	const qint64 seconds = total_seconds % 60;

	if (hours > 0)
	{
		return QStringLiteral("%1:%2:%3")
			.arg(hours)
			.arg(minutes, 2, 10, QLatin1Char('0'))
			.arg(seconds, 2, 10, QLatin1Char('0'));
	}
	return QStringLiteral("%1:%2")
		.arg(minutes, 2, 10, QLatin1Char('0'))
		.arg(seconds, 2, 10, QLatin1Char('0'));
}

void
MediaPlayerWidget::refresh_time_label()
{
	m_time_label->setText(QStringLiteral("%1 / %2").arg(
		format_time(m_player->position()), format_time(m_player->duration())));
}
