#ifndef MEDIAPREVIEWPANEL_H
#define MEDIAPREVIEWPANEL_H

// ============================================================================
// 视图层 - 媒体预览面板（cc-switch v4 卡片体系）
//
// 布局：左侧「输入预览」卡 + 右侧「输出预览」卡（各一张 CardWidget）：
//   卡头 = SectionTitle + 状态徽章；
//   卡身 = 预览区（空态 / 图片 / 视频三页切换）+ 文件信息 + 操作按钮行。
//
// 交互：
//   - 空态：图标 + 提示 + 「选择文件」主操作；
//   - 图片等比适配预览区（resize 时重算缩放），视频复用 MediaPlayerWidget；
//   - 从窗口拖入文件时两张预览卡高亮（panels.qss.in 的 dropActive 规则）。
//
// i18n：文案走 tr()；面板保存输入/输出的「显示状态快照」，retranslate_ui()
// 只重设文案（refresh_input_texts / refresh_output_texts），不重载图片、不
// 重启视频播放器。
// ============================================================================

#include <QPixmap>
#include <QSize>
#include <QString>
#include <QWidget>

#include "mediasettings.h"

class ActionButton;
class CardWidget;
class HintLabel;
class MediaPlayerWidget;
class QLabel;
class QStackedWidget;
class StatusBadge;

class MediaPreviewPanel : public QWidget
{
	Q_OBJECT

  public:
	explicit MediaPreviewPanel(QWidget *v_parent = nullptr);

	// 输入选择变化
	void
	set_input(const QString &v_path, int v_valid_file_count,
			  bool v_is_directory);
	void
	clear_input();

	// 输出展示
	void
	set_output_path(const QString &v_path);
	void
	show_output(const QString &v_path);
	void
	clear_output();

	// 单文件 / 批量模式：批量模式下隐藏「另存为」（另存为只对单文件有意义）
	void
	set_batch_mode(bool v_batch_mode);

	// 图片 / 视频模式：空态标题、提示与选择按钮文案随模式变化
	void
	set_media_kind(MediaKind v_kind);

	// 拖放高亮：由主窗口根据「是否接受本次拖放」统一驱动（busy / 非工作台页
	// 不接受时不高亮），面板自身不再监听全窗口事件。
	void
	set_drop_highlight(bool v_active);

	// 语言切换：重设卡标题 / 空态 / 徽章 / 文件信息 / 按钮与 tooltip，
	// 保留当前输入输出与预览内容
	void
	retranslate_ui();

  signals:
	void
	select_input_requested();
	void
	delete_input_requested();
	void
	set_output_path_requested();
	void
	open_output_folder_requested();
	void
	save_as_requested();

  protected:
	void
	resizeEvent(QResizeEvent *v_event) override;

  private:
	// 输入显示状态（决定 refresh_input_texts 的渲染分支）
	enum class InputDisplay
	{
		EMPTY,
		DIRECTORY,
		FILE
	};
	// 单文件输入时的预览类型
	enum class InputPreviewKind
	{
		NONE,
		IMAGE,
		VIDEO,
		UNSUPPORTED,
		FAILED
	};
	// 输出预览卡状态徽章的语义（PATH_SET 等没有重新渲染文案的状态下，
	// 语言切换时按最近的徽章语义重新翻译，避免残留旧语言）
	enum class OutputBadgeKind
	{
		PENDING,
		DONE,
		FAILED
	};
	// 输出预览面（图片/文字）当前内容的语义：PATH_SET 等状态下 surface
	// 不会重新渲染，语言切换时按此快照重设文字（否则残留旧语言）。
	enum class OutputSurfaceText
	{
		NONE,
		DIRECTORY_DONE,
		UNSUPPORTED_DONE,
		MISSING,
		IMAGE_FAILED
	};
	// 输出显示状态（决定 refresh_output_texts 的渲染分支）
	enum class OutputDisplay
	{
		NONE,
		PATH_SET,
		DIRECTORY_DONE,
		IMAGE_DONE,
		VIDEO_DONE,
		UNSUPPORTED_DONE,
		MISSING,
		IMAGE_FAILED
	};

	QWidget *
	build_input_card();
	QWidget *
	build_output_card();

	// 输入侧
	void
	update_file_info(const QString &v_file_path);
	void
	show_video_preview(const QString &v_file_path);
	void
	hide_video_preview();
	void
	show_input_text(const QString &v_text);
	// 按状态快照重设输入侧文案（不改预览面 / 播放器）
	void
	refresh_input_texts();

	// 输出侧
	void
	show_output_text(const QString &v_text);
	// 按当前语言重设输出预览面文字（PATH_SET 等不改变 surface 的状态用）
	void
	refresh_output_surface_text();
	void
	clear_output_preview();
	// 按状态快照重设输出侧文案（不改预览面 / 播放器）
	void
	refresh_output_texts();
	// 按「是否有输出 + 是否批量模式」刷新输出操作按钮可见性
	void
	update_output_actions();

	// 语言切换：空态标题/说明
	void
	refresh_empty_state_texts();

	// 通用
	void
	refresh_preview_pixmaps();

	CardWidget *m_input_card;
	StatusBadge *m_input_badge;
	QStackedWidget *m_input_stack;
	QWidget *m_input_empty_page;
	QLabel *m_input_empty_title;
	HintLabel *m_input_empty_hint;
	MediaKind m_media_kind = MediaKind::IMAGE;
	QLabel *m_input_image_label;
	MediaPlayerWidget *m_input_player;
	QLabel *m_input_file_info_label;
	ActionButton *m_select_file_btn;
	ActionButton *m_delete_file_btn;
	ActionButton *m_empty_select_btn;
	ActionButton *m_output_empty_select_btn;
	QPixmap m_input_pixmap;

	CardWidget *m_output_card;
	StatusBadge *m_output_badge;
	QStackedWidget *m_output_stack;
	QWidget *m_output_empty_page;
	QLabel *m_output_empty_title;
	HintLabel *m_output_empty_hint;
	QLabel *m_output_image_label;
	MediaPlayerWidget *m_output_player;
	QLabel *m_output_file_info_label;
	ActionButton *m_set_output_path_btn;
	ActionButton *m_open_folder_btn;
	ActionButton *m_save_as_btn;
	QPixmap m_output_pixmap;

	// —— 输入状态快照（语言切换时重设文案用）——
	InputDisplay m_input_display = InputDisplay::EMPTY;
	InputPreviewKind m_input_preview_kind = InputPreviewKind::NONE;
	QString m_input_path;
	int m_input_valid_file_count = 0;
	QString m_input_suffix_upper;
	QString m_input_dir_size_text;

	// —— 输出状态快照 ——
	OutputDisplay m_output_display = OutputDisplay::NONE;
	OutputBadgeKind m_output_badge_kind = OutputBadgeKind::PENDING;
	// 输出预览面文字快照（PATH_SET 下语言切换时重渲染用，避免旧语言残留）
	OutputSurfaceText m_output_surface_text = OutputSurfaceText::NONE;
	int m_output_surface_dir_file_count = 0;
	QString m_output_surface_suffix_upper;
	QString m_output_path;
	QString m_output_file_name;
	QString m_output_size_text;
	int m_output_dir_file_count = 0;
	QString m_output_dir_size_text;
	QSize m_output_image_size;
	QString m_output_suffix_upper;

	bool m_output_set = false; // 是否已有输出路径 / 已完成输出
	bool m_batch_mode = false; // 当前处理模式（批量模式下隐藏「另存为」）

	bool m_drop_active = false;
};

#endif // MEDIAPREVIEWPANEL_H
