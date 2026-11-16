#include "mediapreviewpanel.h"

#include "action_button.h"
#include "card_widget.h"
#include "hint_label.h"
#include "icon_provider.h"
#include "mediaplayerwidget.h"
#include "status_badge.h"
#include "theme_manager.h"

#include "fileservice.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace
{

const QStringList K_IMAGE_PREVIEW_EXTENSIONS
	= { QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
		QStringLiteral("webp"), QStringLiteral("bmp") };

const QStringList K_VIDEO_PREVIEW_EXTENSIONS
	= { QStringLiteral("mp4"), QStringLiteral("mkv"),  QStringLiteral("avi"),
		QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("wmv"),
		QStringLiteral("flv"), QStringLiteral("gif") };

// 预览区三页的堆叠索引：空态 / 图片 / 视频
enum PreviewPage
{
	PAGE_EMPTY = 0,
	PAGE_IMAGE = 1,
	PAGE_VIDEO = 2
};

// 预览区最小高度：左右两卡在 1100×650 窗口下约 210 宽，给 6:5 左右的画面空间
const int K_PREVIEW_MIN_HEIGHT = 168;
// 空态图标块尺寸（ui-spec 3.11：56×56、图标 24）
const int K_EMPTY_ICON_TILE = 56;
const int K_EMPTY_ICON_SIZE = 24;
// 卡内边距（紧凑密度）
const int K_CARD_PADDING = 12;

// 图片预览面：objectName 交给 panels.qss.in（浅底 + 1px 描边 + 圆角）
QLabel *
make_surface_label(QWidget *v_parent)
{
	QLabel *label = new QLabel(v_parent);
	label->setObjectName(QStringLiteral("mediaPreviewSurface"));
	label->setAlignment(Qt::AlignCenter);
	label->setWordWrap(true);
	label->setMinimumSize(0, 0);
	label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	return label;
}

// 刷新图片预览面状态（empty / error / folder 交给 QSS 属性选择器）
void
set_surface_state(QLabel *v_label, const QString &v_state)
{
	v_label->setProperty("state", v_state.isEmpty() ? QVariant() : v_state);
	v_label->style()->unpolish(v_label);
	v_label->style()->polish(v_label);
}

// 空态页：56 图标块 + 标题 + 说明 + 可选主操作按钮；标题/说明指针回填给
// 面板，用于语言切换时重设文案。
QWidget *
make_empty_state(QWidget *v_parent, const QString &v_icon_name,
				 const QString &v_title, const QString &v_hint,
				 const QString &v_button_text, const QString &v_button_icon,
				 ActionButton::Variant v_button_variant,
				 ActionButton **v_out_button, QLabel **v_out_title,
				 HintLabel **v_out_hint)
{
	QWidget *page = new QWidget(v_parent);
	QVBoxLayout *layout = new QVBoxLayout(page);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	layout->addStretch(1);

	QLabel *icon_tile = new QLabel(page);
	icon_tile->setObjectName(QStringLiteral("emptyStateIconTile"));
	icon_tile->setFixedSize(K_EMPTY_ICON_TILE, K_EMPTY_ICON_TILE);
	icon_tile->setAlignment(Qt::AlignCenter);
	layout->addWidget(icon_tile, 0, Qt::AlignHCenter);

	QLabel *title = new QLabel(v_title, page);
	title->setObjectName(QStringLiteral("emptyStateTitle"));
	title->setAlignment(Qt::AlignCenter);
	title->setWordWrap(true);
	layout->addWidget(title);

	HintLabel *hint = new HintLabel(v_hint, page);
	hint->setAlignment(Qt::AlignCenter);
	layout->addWidget(hint);

	ActionButton *button = nullptr;
	if (!v_button_text.isEmpty())
	{
		button = new ActionButton(v_button_variant, v_button_text,
								  v_button_icon, page);
		layout->addWidget(button, 0, Qt::AlignHCenter);
	}
	layout->addStretch(1);

	// 图标随主题重新染色（空态图标用二级文字色）
	const auto refresh_icon = [icon_tile, v_icon_name]()
	{
		icon_tile->setPixmap(IconProvider::pixmap(
			v_icon_name,
			ThemeManager::instance().color(QStringLiteral("text.2")),
			K_EMPTY_ICON_SIZE));
	};
	refresh_icon();
	QObject::connect(&ThemeManager::instance(), &ThemeManager::theme_changed,
					 page, refresh_icon);

	if (v_out_button != nullptr)
	{
		*v_out_button = button;
	}
	if (v_out_title != nullptr)
	{
		*v_out_title = title;
	}
	if (v_out_hint != nullptr)
	{
		*v_out_hint = hint;
	}
	return page;
}

// 让缩略图铺满预览面：保持宽高比，超出的部分留白
QPixmap
fit_pixmap(const QPixmap &v_pixmap, const QSize &v_target)
{
	if (v_pixmap.isNull() || v_target.isEmpty())
	{
		return QPixmap();
	}
	return v_pixmap.scaled(v_target, Qt::KeepAspectRatio,
						   Qt::SmoothTransformation);
}

} // namespace

MediaPreviewPanel::MediaPreviewPanel(QWidget *v_parent)
	: QWidget(v_parent), m_input_card(nullptr), m_input_badge(nullptr),
	  m_input_stack(nullptr), m_input_empty_page(nullptr),
	  m_input_empty_title(nullptr), m_input_empty_hint(nullptr),
	  m_input_image_label(nullptr), m_input_player(nullptr),
	  m_input_file_info_label(nullptr), m_select_file_btn(nullptr),
	  m_delete_file_btn(nullptr), m_empty_select_btn(nullptr),
	  m_output_empty_select_btn(nullptr), m_output_card(nullptr),
	  m_output_badge(nullptr), m_output_stack(nullptr),
	  m_output_empty_page(nullptr), m_output_empty_title(nullptr),
	  m_output_empty_hint(nullptr), m_output_image_label(nullptr),
	  m_output_player(nullptr), m_output_file_info_label(nullptr),
	  m_set_output_path_btn(nullptr), m_open_folder_btn(nullptr),
	  m_save_as_btn(nullptr), m_output_set(false), m_batch_mode(false),
	  m_drop_active(false)
{
	setObjectName(QStringLiteral("mediaPreviewPanel"));

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(12);
	layout->addWidget(build_input_card(), 1);
	layout->addWidget(build_output_card(), 1);

	// 初始图片模式空态：标题 / 提示 / 按钮统一按模式与批量状态刷新
	refresh_empty_state_texts();
	update_output_actions();
}

// ---------------------------------------------------------------------------
// 输入预览卡
// ---------------------------------------------------------------------------
QWidget *
MediaPreviewPanel::build_input_card()
{
	m_input_card = new CardWidget(MediaPreviewPanel::tr("输入预览"), this);
	m_input_card->set_padding(K_CARD_PADDING);
	m_input_card->body_layout()->setSpacing(8);

	m_input_badge = new StatusBadge(MediaPreviewPanel::tr("未选择"),
									StatusBadge::Tone::NEUTRAL, m_input_card);
	m_input_card->add_header_action(m_input_badge);

	// —— 预览区：空态 / 图片 / 视频三页 ——
	m_input_stack = new QStackedWidget(m_input_card);
	m_input_stack->setObjectName(QStringLiteral("mediaPreviewStack"));
	m_input_stack->setMinimumHeight(K_PREVIEW_MIN_HEIGHT);
	m_input_stack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

	m_input_empty_page = make_empty_state(
		m_input_stack, QStringLiteral("image"),
		MediaPreviewPanel::tr("拖入图片 / 视频"),
		MediaPreviewPanel::tr("支持文件、文件夹或直接拖放"),
		MediaPreviewPanel::tr("选择文件"), QStringLiteral("folder-open"),
		ActionButton::Variant::PRIMARY, &m_empty_select_btn,
		&m_input_empty_title, &m_input_empty_hint);
	m_input_image_label = make_surface_label(m_input_stack);
	m_input_player = new MediaPlayerWidget(QStringLiteral("inputVideoWidget"),
										   m_input_stack);
	m_input_stack->addWidget(m_input_empty_page);
	m_input_stack->addWidget(m_input_image_label);
	m_input_stack->addWidget(m_input_player);
	m_input_stack->setCurrentIndex(PAGE_EMPTY);
	m_input_card->body_layout()->addWidget(m_input_stack, 1);

	// —— 文件信息 ——
	m_input_file_info_label = new QLabel(m_input_card);
	m_input_file_info_label->setObjectName(QStringLiteral("mediaInfoLabel"));
	m_input_file_info_label->setWordWrap(true);
	m_input_file_info_label->setSizePolicy(QSizePolicy::Preferred,
										   QSizePolicy::Minimum);
	m_input_card->body_layout()->addWidget(m_input_file_info_label);

	// —— 操作按钮行 ——
	m_select_file_btn = new ActionButton(
		ActionButton::Variant::SECONDARY, MediaPreviewPanel::tr("重新选择"),
		QStringLiteral("folder-open"), m_input_card);
	m_delete_file_btn = new ActionButton(
		ActionButton::Variant::DANGER, MediaPreviewPanel::tr("移除"),
		QStringLiteral("trash-2"), m_input_card);
	m_select_file_btn->hide();
	m_delete_file_btn->hide();

	QHBoxLayout *button_row = new QHBoxLayout();
	button_row->setContentsMargins(0, 0, 0, 0);
	button_row->setSpacing(8);
	button_row->addWidget(m_select_file_btn);
	button_row->addWidget(m_delete_file_btn);
	button_row->addStretch(1);
	m_input_card->body_layout()->addLayout(button_row);

	connect(m_select_file_btn, &QPushButton::clicked, this,
			&MediaPreviewPanel::select_input_requested);
	connect(m_delete_file_btn, &QPushButton::clicked, this,
			&MediaPreviewPanel::delete_input_requested);
	connect(m_empty_select_btn, &QPushButton::clicked, this,
			&MediaPreviewPanel::select_input_requested);

	return m_input_card;
}

// ---------------------------------------------------------------------------
// 输出预览卡
// ---------------------------------------------------------------------------
QWidget *
MediaPreviewPanel::build_output_card()
{
	m_output_card = new CardWidget(MediaPreviewPanel::tr("输出预览"), this);
	m_output_card->set_padding(K_CARD_PADDING);
	m_output_card->body_layout()->setSpacing(8);

	m_output_badge = new StatusBadge(MediaPreviewPanel::tr("等待处理"),
									 StatusBadge::Tone::NEUTRAL, m_output_card);
	m_output_card->add_header_action(m_output_badge);

	m_output_stack = new QStackedWidget(m_output_card);
	m_output_stack->setObjectName(QStringLiteral("mediaPreviewStack"));
	m_output_stack->setMinimumHeight(K_PREVIEW_MIN_HEIGHT);
	m_output_stack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

	m_output_empty_page = make_empty_state(
		m_output_stack, QStringLiteral("images"),
		MediaPreviewPanel::tr("处理后预览"),
		MediaPreviewPanel::tr("设置输出目录即可开始"),
		MediaPreviewPanel::tr("设置输出目录"), QStringLiteral("folder-open"),
		ActionButton::Variant::SECONDARY, &m_output_empty_select_btn,
		&m_output_empty_title, &m_output_empty_hint);
	m_output_image_label = make_surface_label(m_output_stack);
	m_output_player = new MediaPlayerWidget(QStringLiteral("outputVideoWidget"),
											m_output_stack);
	m_output_stack->addWidget(m_output_empty_page);
	m_output_stack->addWidget(m_output_image_label);
	m_output_stack->addWidget(m_output_player);
	m_output_stack->setCurrentIndex(PAGE_EMPTY);
	m_output_card->body_layout()->addWidget(m_output_stack, 1);

	m_output_file_info_label = new QLabel(m_output_card);
	m_output_file_info_label->setObjectName(QStringLiteral("mediaInfoLabel"));
	m_output_file_info_label->setWordWrap(true);
	m_output_file_info_label->setSizePolicy(QSizePolicy::Preferred,
											QSizePolicy::Minimum);
	m_output_card->body_layout()->addWidget(m_output_file_info_label);

	m_set_output_path_btn = new ActionButton(
		ActionButton::Variant::SECONDARY, MediaPreviewPanel::tr("设置输出目录"),
		QStringLiteral("folder-open"), m_output_card);
	m_open_folder_btn
		= new ActionButton(ActionButton::Variant::ICON_COMPACT, QString(),
						   QStringLiteral("external-link"), m_output_card);
	m_save_as_btn
		= new ActionButton(ActionButton::Variant::ICON_COMPACT, QString(),
						   QStringLiteral("plus"), m_output_card);
	m_open_folder_btn->hide();
	m_save_as_btn->hide();
	// 图标按钮在构造期就设置 tooltip：正常启动（未切过语言）时也要有提示，
	// 之前只在 retranslate_ui() 里设置，启动后到首次切换语言前没有 tooltip。
	m_open_folder_btn->setToolTip(MediaPreviewPanel::tr("打开输出文件夹"));
	m_open_folder_btn->setAccessibleName(
		MediaPreviewPanel::tr("打开输出文件夹"));
	m_save_as_btn->setToolTip(MediaPreviewPanel::tr("另存为"));
	m_save_as_btn->setAccessibleName(MediaPreviewPanel::tr("另存为"));

	QHBoxLayout *button_row = new QHBoxLayout();
	button_row->setContentsMargins(0, 0, 0, 0);
	button_row->setSpacing(8);
	button_row->addWidget(m_set_output_path_btn);
	button_row->addWidget(m_open_folder_btn);
	button_row->addWidget(m_save_as_btn);
	button_row->addStretch(1);
	m_output_card->body_layout()->addLayout(button_row);

	connect(m_set_output_path_btn, &QPushButton::clicked, this,
			&MediaPreviewPanel::set_output_path_requested);
	connect(m_output_empty_select_btn, &QPushButton::clicked, this,
			&MediaPreviewPanel::set_output_path_requested);
	connect(m_open_folder_btn, &QPushButton::clicked, this,
			&MediaPreviewPanel::open_output_folder_requested);
	connect(m_save_as_btn, &QPushButton::clicked, this,
			&MediaPreviewPanel::save_as_requested);

	return m_output_card;
}

// ---------------------------------------------------------------------------
// 输入侧状态
// ---------------------------------------------------------------------------
void
MediaPreviewPanel::set_input(const QString &v_path, int v_valid_file_count,
							 bool v_is_directory)
{
	hide_video_preview();
	// 清掉上一次的预览位图：预览失败 / 不支持时 refresh_preview_pixmaps()
	// 不能再把旧图贴回来（否则 resize 后会显示上一个文件的画面）
	m_input_pixmap = QPixmap();
	m_select_file_btn->show();
	m_delete_file_btn->show();

	m_input_path = v_path;
	m_input_valid_file_count = v_valid_file_count;
	m_input_suffix_upper.clear();

	const QFileInfo info(v_path);
	if (v_is_directory || info.isDir())
	{
		m_input_display = InputDisplay::DIRECTORY;
		m_input_preview_kind = InputPreviewKind::NONE;
		m_input_dir_size_text = FileService::format_file_size(
			FileService::directory_total_size(v_path));
		m_input_pixmap = QPixmap();
		set_surface_state(m_input_image_label, QStringLiteral("folder"));
		m_input_stack->setCurrentIndex(PAGE_IMAGE);
		refresh_input_texts();
		return;
	}

	m_input_display = InputDisplay::FILE;
	m_input_preview_kind = InputPreviewKind::NONE;
	const QString suffix = info.suffix().toLower();
	if (K_IMAGE_PREVIEW_EXTENSIONS.contains(suffix))
	{
		QImageReader reader(v_path);
		reader.setAutoTransform(true);
		const QImage image = reader.read();
		if (!image.isNull())
		{
			m_input_pixmap = QPixmap::fromImage(image);
			m_input_preview_kind = InputPreviewKind::IMAGE;
			m_input_image_label->setText(QString());
			set_surface_state(m_input_image_label, QString());
			m_input_stack->setCurrentIndex(PAGE_IMAGE);
			// 首次布局前预览面尺寸可能还没确定，下一轮事件循环再缩放
			QTimer::singleShot(0, this,
							   [this]() { refresh_preview_pixmaps(); });
		}
		else
		{
			m_input_preview_kind = InputPreviewKind::FAILED;
		}
	}
	else if (K_VIDEO_PREVIEW_EXTENSIONS.contains(suffix))
	{
		m_input_preview_kind = InputPreviewKind::VIDEO;
		show_video_preview(v_path);
	}
	else
	{
		m_input_preview_kind = InputPreviewKind::UNSUPPORTED;
		m_input_suffix_upper = suffix.toUpper();
	}

	refresh_input_texts();
}

void
MediaPreviewPanel::clear_input()
{
	hide_video_preview();
	m_input_pixmap = QPixmap();
	m_input_display = InputDisplay::EMPTY;
	m_input_preview_kind = InputPreviewKind::NONE;
	m_input_path.clear();
	m_input_valid_file_count = 0;
	m_input_suffix_upper.clear();
	m_input_dir_size_text.clear();
	m_input_stack->setCurrentIndex(PAGE_EMPTY);
	refresh_input_texts();
}

void
MediaPreviewPanel::show_input_text(const QString &v_text)
{
	m_input_image_label->setPixmap(QPixmap());
	m_input_image_label->setText(v_text);
}

void
MediaPreviewPanel::refresh_input_texts()
{
	switch (m_input_display)
	{
		case InputDisplay::EMPTY:
		{
			show_input_text(QString());
			set_surface_state(m_input_image_label, QString());
			m_input_stack->setCurrentIndex(PAGE_EMPTY);
			m_input_badge->setText(MediaPreviewPanel::tr("未选择"));
			m_input_badge->set_tone(StatusBadge::Tone::NEUTRAL);
			m_input_file_info_label->clear();
			m_input_file_info_label->setToolTip(QString());
			m_select_file_btn->hide();
			m_delete_file_btn->hide();
			return;
		}
		case InputDisplay::DIRECTORY:
		{
			m_input_pixmap = QPixmap();
			show_input_text(
				MediaPreviewPanel::tr("文件夹 · 批量处理\n共 %1 个可处理文件\n"
									  "总大小 %2")
					.arg(m_input_valid_file_count)
					.arg(m_input_dir_size_text));
			set_surface_state(m_input_image_label, QStringLiteral("folder"));
			m_input_stack->setCurrentIndex(PAGE_IMAGE);
			m_input_badge->setText(MediaPreviewPanel::tr("文件夹"));
			m_input_badge->set_tone(StatusBadge::Tone::INFO);
			m_input_file_info_label->setText(
				MediaPreviewPanel::tr("%1\n共 %2 个可处理文件")
					.arg(QFileInfo(m_input_path).fileName())
					.arg(m_input_valid_file_count));
			m_input_file_info_label->setToolTip(m_input_path);
			m_select_file_btn->show();
			m_delete_file_btn->show();
			return;
		}
		case InputDisplay::FILE:
		{
			m_input_badge->setText(MediaPreviewPanel::tr("已选择"));
			m_input_badge->set_tone(StatusBadge::Tone::SUCCESS);
			if (m_input_preview_kind == InputPreviewKind::FAILED)
			{
				show_input_text(MediaPreviewPanel::tr("图片预览失败"));
				set_surface_state(m_input_image_label, QStringLiteral("error"));
				m_input_stack->setCurrentIndex(PAGE_IMAGE);
			}
			else if (m_input_preview_kind == InputPreviewKind::UNSUPPORTED)
			{
				show_input_text(MediaPreviewPanel::tr("%1 文件\n暂不支持预览")
									.arg(m_input_suffix_upper));
				set_surface_state(m_input_image_label, QString());
				m_input_stack->setCurrentIndex(PAGE_IMAGE);
			}
			// IMAGE / VIDEO：预览面与播放器保持原样，只刷新文件信息
			update_file_info(m_input_path);
			m_select_file_btn->show();
			m_delete_file_btn->show();
			return;
		}
	}
}

void
MediaPreviewPanel::update_file_info(const QString &v_file_path)
{
	const QFileInfo info(v_file_path);
	const QString suffix = info.suffix().toUpper();
	const QString size_str = FileService::format_file_size(info.size());

	const QStringList image_extensions
		= { QStringLiteral("JPG"), QStringLiteral("JPEG"),
			QStringLiteral("PNG"), QStringLiteral("WEBP"),
			QStringLiteral("BMP"), QStringLiteral("GIF") };

	if (image_extensions.contains(suffix))
	{
		QImageReader reader(v_file_path);
		const QSize image_size = reader.size();
		if (image_size.isValid())
		{
			m_input_file_info_label->setText(
				QStringLiteral("%1\n%2 · %3 · %4 × %5")
					.arg(info.fileName(), suffix, size_str)
					.arg(image_size.width())
					.arg(image_size.height()));
			m_input_file_info_label->setToolTip(v_file_path);
			return;
		}
	}

	m_input_file_info_label->setText(
		QStringLiteral("%1\n%2 · %3").arg(info.fileName(), suffix, size_str));
	m_input_file_info_label->setToolTip(v_file_path);
}

void
MediaPreviewPanel::show_video_preview(const QString &v_file_path)
{
	m_input_pixmap = QPixmap();
	m_input_image_label->clear();
	set_surface_state(m_input_image_label, QString());
	m_input_stack->setCurrentIndex(PAGE_VIDEO);
	m_input_player->open_and_play(v_file_path);
}

void
MediaPreviewPanel::hide_video_preview()
{
	m_input_player->stop_and_hide();
	if (m_input_stack->currentIndex() == PAGE_VIDEO)
	{
		m_input_stack->setCurrentIndex(PAGE_EMPTY);
	}
}

// ---------------------------------------------------------------------------
// 输出侧状态
// ---------------------------------------------------------------------------
void
MediaPreviewPanel::set_output_path(const QString &v_path)
{
	m_output_set = true;
	update_output_actions();

	// 上一次输出（完成/失败/缺失）的缩略图与「处理完成/预览失败」等 surface
	// 文案都属于旧输出目标：改输出目录后必须清掉，否则旧缩略图会继续显示，
	// 且这些 surface 文案在 PATH_SET 分支不会被重渲染，语言切换后会残留旧
	// 语言。这里先清预览，再按新的输出目标重建摘要/徽章。
	if (m_output_display != OutputDisplay::NONE
		&& m_output_display != OutputDisplay::PATH_SET)
	{
		clear_output_preview();
	}
	// 新输出目标尚未处理：徽章回到「等待处理」语义（PATH_SET 分支按此翻译）
	m_output_badge_kind = OutputBadgeKind::PENDING;

	m_output_path = v_path;
	m_output_display = OutputDisplay::PATH_SET;
	m_output_file_name = QFileInfo(v_path).fileName();
	m_output_dir_file_count = 0;
	m_output_dir_size_text.clear();
	const QFileInfo info(v_path);
	if (info.isDir())
	{
		m_output_dir_file_count
			= QDir(v_path).entryList(QDir::Files | QDir::NoDotAndDotDot).size();
		m_output_dir_size_text = FileService::format_file_size(
			FileService::directory_total_size(v_path));
	}
	refresh_output_texts();
}

void
MediaPreviewPanel::show_output(const QString &v_path)
{
	m_output_player->stop_and_hide();
	m_output_pixmap = QPixmap();
	set_surface_state(m_output_image_label, QString());
	// 先清空输出预览面文字快照，各分支按实际内容重新记录
	m_output_surface_text = OutputSurfaceText::NONE;
	m_output_surface_dir_file_count = 0;
	m_output_surface_suffix_upper.clear();
	m_output_set = true;
	update_output_actions();

	m_output_path = v_path;
	const QFileInfo info(v_path);
	if (!info.exists())
	{
		m_output_display = OutputDisplay::MISSING;
		m_output_badge_kind = OutputBadgeKind::FAILED;
		m_output_surface_text = OutputSurfaceText::MISSING;
		refresh_output_texts();
		return;
	}

	if (info.isDir())
	{
		m_output_display = OutputDisplay::DIRECTORY_DONE;
		m_output_badge_kind = OutputBadgeKind::DONE;
		m_output_file_name = info.fileName();
		m_output_dir_file_count
			= QDir(v_path).entryList(QDir::Files | QDir::NoDotAndDotDot).size();
		m_output_dir_size_text = FileService::format_file_size(
			FileService::directory_total_size(v_path));
		m_output_surface_text = OutputSurfaceText::DIRECTORY_DONE;
		m_output_surface_dir_file_count = m_output_dir_file_count;
		refresh_output_texts();
		return;
	}

	const QString suffix = info.suffix().toLower();
	m_output_file_name = info.fileName();
	m_output_size_text = FileService::format_file_size(info.size());
	m_output_suffix_upper = suffix.toUpper();

	if (K_IMAGE_PREVIEW_EXTENSIONS.contains(suffix))
	{
		QImageReader reader(v_path);
		reader.setAutoTransform(true);
		const QImage image = reader.read();
		if (!image.isNull())
		{
			m_output_pixmap = QPixmap::fromImage(image);
			m_output_image_size = image.size();
			m_output_image_label->setText(QString());
			m_output_stack->setCurrentIndex(PAGE_IMAGE);
			m_output_display = OutputDisplay::IMAGE_DONE;
			m_output_badge_kind = OutputBadgeKind::DONE;
			refresh_output_texts();
			QTimer::singleShot(0, this,
							   [this]() { refresh_preview_pixmaps(); });
			return;
		}
		m_output_display = OutputDisplay::IMAGE_FAILED;
		m_output_badge_kind = OutputBadgeKind::FAILED;
		m_output_surface_text = OutputSurfaceText::IMAGE_FAILED;
		refresh_output_texts();
		return;
	}

	if (K_VIDEO_PREVIEW_EXTENSIONS.contains(suffix))
	{
		m_output_image_label->clear();
		m_output_stack->setCurrentIndex(PAGE_VIDEO);
		m_output_player->open_and_play(v_path);
		m_output_display = OutputDisplay::VIDEO_DONE;
		m_output_badge_kind = OutputBadgeKind::DONE;
		refresh_output_texts();
		return;
	}

	m_output_display = OutputDisplay::UNSUPPORTED_DONE;
	m_output_badge_kind = OutputBadgeKind::DONE;
	m_output_surface_text = OutputSurfaceText::UNSUPPORTED_DONE;
	m_output_surface_suffix_upper = m_output_suffix_upper;
	refresh_output_texts();
}

void
MediaPreviewPanel::clear_output()
{
	clear_output_preview();
	m_output_set = false;
	update_output_actions();
	m_output_display = OutputDisplay::NONE;
	m_output_badge_kind = OutputBadgeKind::PENDING;
	m_output_path.clear();
	m_output_file_name.clear();
	m_output_size_text.clear();
	m_output_suffix_upper.clear();
	m_output_image_size = QSize();
	refresh_output_texts();
}

void
MediaPreviewPanel::clear_output_preview()
{
	m_output_player->stop_and_hide();
	m_output_pixmap = QPixmap();
	m_output_surface_text = OutputSurfaceText::NONE;
	m_output_surface_dir_file_count = 0;
	m_output_surface_suffix_upper.clear();
	m_output_image_label->clear();
	set_surface_state(m_output_image_label, QString());
	m_output_stack->setCurrentIndex(PAGE_EMPTY);
	m_output_file_info_label->clear();
	m_output_file_info_label->setToolTip(QString());
}

void
MediaPreviewPanel::refresh_output_texts()
{
	switch (m_output_display)
	{
		case OutputDisplay::NONE:
			m_output_badge->setText(MediaPreviewPanel::tr("等待处理"));
			m_output_badge->set_tone(StatusBadge::Tone::NEUTRAL);
			return;
		case OutputDisplay::PATH_SET:
			// 徽章语义沿用最近一次输出结果（未处理/已完成/失败），按当前语言重设
			switch (m_output_badge_kind)
			{
				case OutputBadgeKind::PENDING:
					m_output_badge->setText(MediaPreviewPanel::tr("等待处理"));
					m_output_badge->set_tone(StatusBadge::Tone::NEUTRAL);
					break;
				case OutputBadgeKind::DONE:
					m_output_badge->setText(MediaPreviewPanel::tr("已完成"));
					m_output_badge->set_tone(StatusBadge::Tone::SUCCESS);
					break;
				case OutputBadgeKind::FAILED:
					m_output_badge->setText(MediaPreviewPanel::tr("失败"));
					m_output_badge->set_tone(StatusBadge::Tone::DANGER);
					break;
			}
			if (!m_output_path.isEmpty() && QFileInfo(m_output_path).isDir())
			{
				m_output_file_info_label->setText(
					MediaPreviewPanel::tr("%1\n%2 个文件 · %3")
						.arg(m_output_file_name)
						.arg(m_output_dir_file_count)
						.arg(m_output_dir_size_text));
				m_output_file_info_label->setToolTip(m_output_path);
			}
			else
			{
				// 输出目标是文件（如「另存为」）时旧目录摘要已失效，且可能是
				// 旧语言文案，直接清空，避免语言切换后残留中文
				m_output_file_info_label->clear();
				m_output_file_info_label->setToolTip(QString());
			}
			// PATH_SET 不改变预览面内容，但语言切换时必须按当前语言重设
			// surface 文字（DIRECTORY_DONE 的「处理完成」等），否则旧语言残留
			refresh_output_surface_text();
			return;
		case OutputDisplay::DIRECTORY_DONE:
			m_output_surface_text = OutputSurfaceText::DIRECTORY_DONE;
			m_output_surface_dir_file_count = m_output_dir_file_count;
			m_output_surface_suffix_upper.clear();
			show_output_text(
				MediaPreviewPanel::tr("文件夹 · 处理完成\n共 %1 个文件")
					.arg(m_output_dir_file_count));
			set_surface_state(m_output_image_label, QString());
			m_output_stack->setCurrentIndex(PAGE_IMAGE);
			m_output_badge->setText(MediaPreviewPanel::tr("已完成"));
			m_output_badge->set_tone(StatusBadge::Tone::SUCCESS);
			m_output_file_info_label->setText(
				MediaPreviewPanel::tr("%1\n%2 个文件 · %3")
					.arg(m_output_file_name)
					.arg(m_output_dir_file_count)
					.arg(m_output_dir_size_text));
			m_output_file_info_label->setToolTip(m_output_path);
			return;
		case OutputDisplay::IMAGE_DONE:
			m_output_badge->setText(MediaPreviewPanel::tr("已完成"));
			m_output_badge->set_tone(StatusBadge::Tone::SUCCESS);
			m_output_file_info_label->setText(
				QStringLiteral("%1\n%2 · %3 × %4")
					.arg(m_output_file_name, m_output_size_text)
					.arg(m_output_image_size.width())
					.arg(m_output_image_size.height()));
			m_output_file_info_label->setToolTip(m_output_path);
			return;
		case OutputDisplay::VIDEO_DONE:
			m_output_badge->setText(MediaPreviewPanel::tr("已完成"));
			m_output_badge->set_tone(StatusBadge::Tone::SUCCESS);
			m_output_file_info_label->setText(QStringLiteral("%1\n%2").arg(
				m_output_file_name, m_output_size_text));
			m_output_file_info_label->setToolTip(m_output_path);
			return;
		case OutputDisplay::UNSUPPORTED_DONE:
			m_output_surface_text = OutputSurfaceText::UNSUPPORTED_DONE;
			m_output_surface_suffix_upper = m_output_suffix_upper;
			show_output_text(MediaPreviewPanel::tr("处理完成 · %1")
								 .arg(m_output_suffix_upper));
			set_surface_state(m_output_image_label, QString());
			m_output_stack->setCurrentIndex(PAGE_IMAGE);
			m_output_badge->setText(MediaPreviewPanel::tr("已完成"));
			m_output_badge->set_tone(StatusBadge::Tone::SUCCESS);
			m_output_file_info_label->setText(QStringLiteral("%1\n%2").arg(
				m_output_file_name, m_output_size_text));
			m_output_file_info_label->setToolTip(m_output_path);
			return;
		case OutputDisplay::MISSING:
			m_output_surface_text = OutputSurfaceText::MISSING;
			show_output_text(MediaPreviewPanel::tr("输出文件不存在"));
			set_surface_state(m_output_image_label, QStringLiteral("error"));
			m_output_stack->setCurrentIndex(PAGE_IMAGE);
			m_output_badge->setText(MediaPreviewPanel::tr("失败"));
			m_output_badge->set_tone(StatusBadge::Tone::DANGER);
			m_output_file_info_label->clear();
			m_output_file_info_label->setToolTip(QString());
			return;
		case OutputDisplay::IMAGE_FAILED:
			m_output_surface_text = OutputSurfaceText::IMAGE_FAILED;
			show_output_text(MediaPreviewPanel::tr("图片预览失败"));
			set_surface_state(m_output_image_label, QStringLiteral("error"));
			m_output_stack->setCurrentIndex(PAGE_IMAGE);
			m_output_badge->setText(MediaPreviewPanel::tr("失败"));
			m_output_badge->set_tone(StatusBadge::Tone::DANGER);
			// 旧输出的文件摘要与预览失败内容不对应，清掉避免残留
			m_output_file_info_label->clear();
			m_output_file_info_label->setToolTip(QString());
			return;
	}
}

void
MediaPreviewPanel::set_batch_mode(bool v_batch_mode)
{
	m_batch_mode = v_batch_mode;
	refresh_empty_state_texts();
	update_output_actions();
}

void
MediaPreviewPanel::set_media_kind(MediaKind v_kind)
{
	if (m_media_kind == v_kind)
	{
		return;
	}
	m_media_kind = v_kind;
	refresh_empty_state_texts();
}

void
MediaPreviewPanel::update_output_actions()
{
	// 「设置输出目录」常驻：输出设置 / 处理完成后仍可再次修改输出目录；
	// 「打开输出文件夹」在有输出时显示；「另存为」只对单文件模式有意义。
	m_output_empty_select_btn->setVisible(!m_output_set);
	m_set_output_path_btn->setVisible(m_output_set);
	m_open_folder_btn->setVisible(m_output_set);
	m_save_as_btn->setVisible(m_output_set && !m_batch_mode);
}

void
MediaPreviewPanel::show_output_text(const QString &v_text)
{
	m_output_image_label->setPixmap(QPixmap());
	m_output_image_label->setText(v_text);
}

void
MediaPreviewPanel::refresh_output_surface_text()
{
	switch (m_output_surface_text)
	{
		case OutputSurfaceText::NONE:
			return;
		case OutputSurfaceText::DIRECTORY_DONE:
			show_output_text(
				MediaPreviewPanel::tr("文件夹 · 处理完成\n共 %1 个文件")
					.arg(m_output_surface_dir_file_count));
			set_surface_state(m_output_image_label, QString());
			m_output_stack->setCurrentIndex(PAGE_IMAGE);
			return;
		case OutputSurfaceText::UNSUPPORTED_DONE:
			show_output_text(MediaPreviewPanel::tr("处理完成 · %1")
								 .arg(m_output_surface_suffix_upper));
			set_surface_state(m_output_image_label, QString());
			m_output_stack->setCurrentIndex(PAGE_IMAGE);
			return;
		case OutputSurfaceText::MISSING:
			show_output_text(MediaPreviewPanel::tr("输出文件不存在"));
			set_surface_state(m_output_image_label, QStringLiteral("error"));
			m_output_stack->setCurrentIndex(PAGE_IMAGE);
			return;
		case OutputSurfaceText::IMAGE_FAILED:
			show_output_text(MediaPreviewPanel::tr("图片预览失败"));
			set_surface_state(m_output_image_label, QStringLiteral("error"));
			m_output_stack->setCurrentIndex(PAGE_IMAGE);
			return;
	}
}

// ---------------------------------------------------------------------------
// 语言切换
// ---------------------------------------------------------------------------
void
MediaPreviewPanel::refresh_empty_state_texts()
{
	const bool video = (m_media_kind == MediaKind::VIDEO);
	if (m_batch_mode)
	{
		m_input_empty_title->setText(
			video ? MediaPreviewPanel::tr("拖入视频文件夹")
				  : MediaPreviewPanel::tr("拖入图片文件夹"));
		m_input_empty_hint->setText(
			MediaPreviewPanel::tr("选择或拖入包含文件的文件夹"));
		m_empty_select_btn->setText(MediaPreviewPanel::tr("选择目录"));
	}
	else
	{
		m_input_empty_title->setText(video ? MediaPreviewPanel::tr("拖入视频")
										   : MediaPreviewPanel::tr("拖入图片"));
		m_input_empty_hint->setText(
			MediaPreviewPanel::tr("支持文件或直接拖放"));
		m_empty_select_btn->setText(MediaPreviewPanel::tr("选择文件"));
	}

	m_output_empty_title->setText(MediaPreviewPanel::tr("处理后预览"));
	m_output_empty_hint->setText(MediaPreviewPanel::tr("输出目录可选"));
}

void
MediaPreviewPanel::retranslate_ui()
{
	m_input_card->set_title(MediaPreviewPanel::tr("输入预览"));
	m_output_card->set_title(MediaPreviewPanel::tr("输出预览"));

	refresh_empty_state_texts();

	m_select_file_btn->setText(MediaPreviewPanel::tr("重新选择"));
	m_delete_file_btn->setText(MediaPreviewPanel::tr("移除"));
	m_set_output_path_btn->setText(MediaPreviewPanel::tr("设置输出目录"));
	m_output_empty_select_btn->setText(MediaPreviewPanel::tr("设置输出目录"));
	m_output_empty_select_btn->setAccessibleName(
		MediaPreviewPanel::tr("设置输出目录"));
	m_open_folder_btn->setToolTip(MediaPreviewPanel::tr("打开输出文件夹"));
	m_save_as_btn->setToolTip(MediaPreviewPanel::tr("另存为"));
	update_output_actions();

	refresh_input_texts();
	refresh_output_texts();
	m_input_player->retranslate_ui();
	m_output_player->retranslate_ui();
}

// ---------------------------------------------------------------------------
// 通用：缩放 / 拖放高亮
// ---------------------------------------------------------------------------
void
MediaPreviewPanel::refresh_preview_pixmaps()
{
	if (!m_input_pixmap.isNull())
	{
		m_input_image_label->setPixmap(
			fit_pixmap(m_input_pixmap, m_input_image_label->size()));
	}
	if (!m_output_pixmap.isNull())
	{
		m_output_image_label->setPixmap(
			fit_pixmap(m_output_pixmap, m_output_image_label->size()));
	}
}

void
MediaPreviewPanel::set_drop_highlight(bool v_active)
{
	if (v_active == m_drop_active)
	{
		return;
	}
	m_drop_active = v_active;
	for (CardWidget *card : { m_input_card, m_output_card })
	{
		card->setProperty("dropActive",
						  v_active ? QStringLiteral("true") : QVariant());
		card->style()->unpolish(card);
		card->style()->polish(card);
		card->update();
	}
}

void
MediaPreviewPanel::resizeEvent(QResizeEvent *v_event)
{
	QWidget::resizeEvent(v_event);
	refresh_preview_pixmaps();
}
