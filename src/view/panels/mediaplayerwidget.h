#ifndef MEDIAPLAYERWIDGET_H
#define MEDIAPLAYERWIDGET_H

// ============================================================================
// 视图层 - 内嵌媒体播放器
//
// 封装 QMediaPlayer + QVideoWidget + 细滑块 + 图标播放按钮 + 时间码：
//   - 视频画面自适应容器大小（保持 16:9 由视频控件自身绘制）；
//   - 控制条 = 28 图标按钮（播放 / 暂停）+ 4px 细滑块 + 12px 时间码；
//   - 输入预览与输出预览复用同一组件（仅视频控件对象名不同以适配样式表）。
// ============================================================================

#include <QString>
#include <QWidget>

class ActionButton;
class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QSlider;
class QVideoWidget;

class MediaPlayerWidget : public QWidget
{
	Q_OBJECT

  public:
	// videoWidgetObjectName 用于区分输入 / 输出（inputVideoWidget /
	// outputVideoWidget）
	explicit MediaPlayerWidget(const QString &v_video_widget_object_name,
							   QWidget *v_parent = nullptr);

	// 打开并循环播放（同时展示视频控件 + 控制条）
	void
	open_and_play(const QString &v_file_path);

	// 停止播放并隐藏视频控件
	void
	stop_and_hide();

	// 语言切换：重设播放/暂停按钮 tooltip（播放状态不变）
	void
	retranslate_ui();

	QMediaPlayer *
	player() const
	{
		return m_player;
	}

  private slots:
	void
	on_position_changed(qint64 v_position);
	void
	on_duration_changed(qint64 v_duration);
	void
	on_slider_moved(int v_position);
	void
	on_play_pause_clicked();
	void
	update_play_pause_button();

  private:
	// 毫秒 -> mm:ss（超过 1 小时用 h:mm:ss）
	static QString
	format_time(qint64 v_milliseconds);
	void
	refresh_time_label();

	QMediaPlayer *m_player;
	QAudioOutput *m_audio_output;
	QVideoWidget *m_video_widget;
	QWidget *m_controls;
	QSlider *m_slider;
	ActionButton *m_play_pause_btn;
	QLabel *m_time_label;
};

#endif // MEDIAPLAYERWIDGET_H
