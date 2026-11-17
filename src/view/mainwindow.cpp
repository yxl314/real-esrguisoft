#include "mainwindow.h"

#include "advancedsettingspanel.h"
#include "app_page_header.h"
#include "basicsettingspanel.h"
#include "card_widget.h"
#include "functionmodegroup.h"
#include "hint_label.h"
#include "language_manager.h"
#include "mediapreviewpanel.h"
#include "processmodegroup.h"
#include "progresspanel.h"
#include "segmented_control.h"
#include "sidebar_nav.h"
#include "status_badge.h"
#include "theme_manager.h"
#include "videosettingsgroup.h"
#include "action_button.h"
#include "logger.h"
#include "section_title.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QCoreApplication>
#include <QMetaObject>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#include <windowsx.h>
#endif

namespace
{
// 窗口尺寸（ui-spec 2.1：Windows 1100×650，最小 1000×640）
const int WINDOW_DEFAULT_WIDTH = 1100;
const int WINDOW_DEFAULT_HEIGHT = 650;
const int WINDOW_MIN_WIDTH = 1000;
const int WINDOW_MIN_HEIGHT = 640;
// 无边框窗口的边缘拖拽热区宽度
const int RESIZE_BORDER = 6;

// 页面索引
const int PAGE_WORKBENCH = 0;
const int PAGE_SETTINGS = 1;
const int PAGE_ABOUT = 2;

// 导航 id
const QString NAV_IMAGE = QStringLiteral("image");
const QString NAV_VIDEO = QStringLiteral("video");
const QString NAV_SETTINGS = QStringLiteral("settings");
const QString NAV_ABOUT = QStringLiteral("about");

QString
app_version()
{
	// 版本号由 main.cpp 从 CMake project(VERSION) 设置，源码里不再保留第二份
	// 硬编码版本号；未设置时如实显示「未知」，避免文档 / 版本漂移。
	const QString version = QCoreApplication::applicationVersion();
	return version.isEmpty() ? MainWindow::tr("未知") : version;
}
} // namespace

MainWindow::MainWindow(QWidget *v_parent) : IMainView(v_parent)
{
	// 先初始化主题（应用字体 + 全局样式表），再创建任何控件
	ThemeManager::instance().initialize();

	setAcceptDrops(true);
	setWindowTitle(MainWindow::tr("RealESRGAN 媒体处理工具"));
	setWindowIcon(QIcon(QStringLiteral(":/app.png")));
	setWindowFlag(Qt::FramelessWindowHint, true);
	setMinimumSize(WINDOW_MIN_WIDTH, WINDOW_MIN_HEIGHT);
	resize(WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT);

	setup_shell();
	setup_window_buttons();
	setup_shortcuts();

	// 双模式默认值：图片沿用当前默认；视频默认动漫视频模型 4x（2x/3x 可选）
	m_image_basic = m_basic->collect();
	m_video_basic.m_image_type = ImageType::ANIME_IMAGE;
	m_video_basic.m_model_type = ModelType::REAL_ESR_ANIMEVIDEOV3;
	m_video_basic.m_scale_factor = ScaleFactor::SCALE4X;

	// 初始状态：图片模式、单文件、就绪
	on_mode_changed_internal(MediaKind::IMAGE);
	show_page(PAGE_WORKBENCH);
	update_status_badge(ProcessStatus());
	update_mode_hint();

	connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
			&MainWindow::on_theme_changed);
	on_theme_changed();

	// 语言：视图先连接（控制器在 initialize() 中后连接，保证「先重译、后下发
	// 动态状态」的顺序）；语言行选中态按已初始化的 LanguageManager 同步。
	connect(&LanguageManager::instance(), &LanguageManager::language_changed,
			this, &MainWindow::retranslate_all);
	sync_language_control();
	// 页面构建时注册的「运行时文案」重设器先跑一遍，填充初值（版本号 /
	// 运行环境 / Qt 版本等）。
	for (const std::function<void()> &updater : m_text_updaters)
	{
		updater();
	}

	apply_debug_hooks();
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// 界面搭建
// ---------------------------------------------------------------------------
void
MainWindow::setup_shell()
{
	QHBoxLayout *root_layout = new QHBoxLayout(this);
	root_layout->setContentsMargins(0, 0, 0, 0);
	root_layout->setSpacing(0);

	QFrame *app_root = new QFrame(this);
	app_root->setObjectName(QStringLiteral("appRoot"));
	app_root->setAttribute(Qt::WA_StyledBackground, true);
	root_layout->addWidget(app_root);

	QHBoxLayout *shell_layout = new QHBoxLayout(app_root);
	shell_layout->setContentsMargins(0, 0, 0, 0);
	shell_layout->setSpacing(0);

	// —— 左侧栏 ——
	m_sidebar = new SidebarNav(app_root);
	m_sidebar->set_brand(QStringLiteral("RealESRGAN"));
	m_sidebar->add_item(NAV_IMAGE, MainWindow::tr("图片处理"),
						QStringLiteral("image"));
	m_sidebar->add_item(NAV_VIDEO, MainWindow::tr("视频处理"),
						QStringLiteral("video"));
	m_sidebar->add_separator();
	m_sidebar->add_item(NAV_SETTINGS, MainWindow::tr("设置"),
						QStringLiteral("settings"));
	m_sidebar->add_item(NAV_ABOUT, MainWindow::tr("关于"),
						QStringLiteral("info"));
	m_sidebar->set_current(NAV_IMAGE);
	shell_layout->addWidget(m_sidebar);

	connect(m_sidebar, &SidebarNav::item_activated, this,
			&MainWindow::on_nav_activated);
	connect(m_sidebar, &SidebarNav::theme_button_clicked, this,
			[]() { ThemeManager::instance().cycle_mode(); });

	// —— 右侧主区 ——
	QWidget *center = new QWidget(app_root);
	center->setObjectName(QStringLiteral("centerArea"));
	QVBoxLayout *center_layout = new QVBoxLayout(center);
	center_layout->setContentsMargins(0, 0, 0, 0);
	center_layout->setSpacing(0);
	shell_layout->addWidget(center, 1);

	m_header = new AppPageHeader(center);
	m_header->set_title_level(AppPageHeader::TitleLevel::APP);
	center_layout->addWidget(m_header);

	m_status_badge = new StatusBadge(MainWindow::tr("就绪"),
									 StatusBadge::Tone::NEUTRAL, m_header);
	m_header->add_action(m_status_badge);

	m_refresh_button
		= new ActionButton(ActionButton::Variant::ICON_COMPACT, QString(),
						   QStringLiteral("rotate-ccw"), m_header);
	m_refresh_button->setToolTip(
		MainWindow::tr("重置全部设置（清空输入/输出并恢复默认参数，F5）"));
	m_refresh_button->setAccessibleName(
		MainWindow::tr("重置全部设置（清空输入/输出并恢复默认参数）"));
	m_header->add_action(m_refresh_button);
	connect(m_refresh_button, &QPushButton::clicked, this,
			&IMainView::refresh_requested);

	// 任务运行中切到设置/关于页时，页头保留一个可点击的任务回跳入口。
	// 单纯靠侧栏在 busy 时被拦截会让用户无法回到工作台停止任务。
	m_task_chip = new ActionButton(ActionButton::Variant::QUIET,
								   MainWindow::tr("返回工作台"),
								   QStringLiteral("panel-left-open"), m_header);
	m_task_chip->hide();
	m_header->add_action(m_task_chip);
	connect(m_task_chip, &QPushButton::clicked, this,
			[this]()
			{
				show_page(PAGE_WORKBENCH);
				m_sidebar->set_current(m_current_nav);
			});

	m_pages = new QStackedWidget(center);
	m_pages->setObjectName(QStringLiteral("pageStack"));
	center_layout->addWidget(m_pages, 1);

	m_workbench_page = create_workbench_page();
	m_settings_page = create_settings_page();
	m_about_page = create_about_page();
	m_pages->addWidget(m_workbench_page);
	m_pages->addWidget(m_settings_page);
	m_pages->addWidget(m_about_page);

	connect(m_header, &AppPageHeader::title_double_clicked, this,
			&MainWindow::toggle_maximized);
}

QWidget *
MainWindow::create_workbench_page()
{
	QWidget *page = new QWidget();
	page->setObjectName(QStringLiteral("workbenchPage"));
	QVBoxLayout *page_layout = new QVBoxLayout(page);
	page_layout->setContentsMargins(0, 0, 0, 0);
	page_layout->setSpacing(0);

	// —— 页头下的模式行（不随内容滚动，对应 cc-switch 模式 tab 行）——
	QWidget *mode_bar = new QWidget(page);
	mode_bar->setObjectName(QStringLiteral("workbenchModeBar"));
	QHBoxLayout *mode_layout = new QHBoxLayout(mode_bar);
	mode_layout->setContentsMargins(24, 12, 24, 4);
	mode_layout->setSpacing(8);

	m_function_mode = new FunctionModeGroup(mode_bar);
	m_process_mode = new ProcessModeGroup(mode_bar);
	mode_layout->addWidget(m_function_mode, 0, Qt::AlignVCenter);
	mode_layout->addSpacing(8);
	mode_layout->addWidget(m_process_mode, 0, Qt::AlignVCenter);
	mode_layout->addStretch(1);

	m_mode_hint = new HintLabel(QString(), mode_bar);
	mode_layout->addWidget(m_mode_hint, 0, Qt::AlignVCenter);
	page_layout->addWidget(mode_bar);

	// —— 可滚动的业务内容：左「预览 + 进度」/ 右「操作 + 设置」——
	m_workbench_scroll = new QScrollArea(page);
	m_workbench_scroll->setObjectName(QStringLiteral("workbenchScroll"));
	m_workbench_scroll->setWidgetResizable(true);
	m_workbench_scroll->setFrameShape(QFrame::NoFrame);
	m_workbench_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	QWidget *content = new QWidget();
	content->setObjectName(QStringLiteral("workbenchContent"));
	QVBoxLayout *content_layout = new QVBoxLayout(content);
	content_layout->setContentsMargins(24, 16, 24, 48);
	content_layout->setSpacing(12);

	QWidget *left_column = new QWidget(content);
	left_column->setObjectName(QStringLiteral("workbenchLeftColumn"));
	QVBoxLayout *left_layout = new QVBoxLayout(left_column);
	left_layout->setContentsMargins(0, 0, 0, 0);
	left_layout->setSpacing(12);

	m_preview = new MediaPreviewPanel(left_column);
	m_progress = new ProgressPanel(left_column);
	left_layout->addWidget(m_preview, 3);
	left_layout->addWidget(m_progress, 0);
	left_layout->addStretch(1);

	QWidget *right_column = new QWidget(content);
	right_column->setObjectName(QStringLiteral("workbenchRightColumn"));
	// 固定右栏宽度：最小窗口 1000 宽时右栏仍有完整表单宽度，避免英文
	// 「Upscale factor」等长标签 + 下拉框被窗口边缘裁切。
	right_column->setFixedWidth(340);
	QVBoxLayout *right_layout = new QVBoxLayout(right_column);
	right_layout->setContentsMargins(0, 0, 0, 0);
	right_layout->setSpacing(12);

	m_basic = new BasicSettingsPanel(right_column);
	m_video_settings = new VideoSettingsGroup(right_column);
	m_advanced = new AdvancedSettingsPanel(right_column);
	right_layout->addWidget(m_basic, 0);
	right_layout->addWidget(m_video_settings, 0);
	right_layout->addWidget(m_advanced, 0);
	right_layout->addStretch(1);

	QHBoxLayout *columns = new QHBoxLayout();
	columns->setContentsMargins(0, 0, 0, 0);
	columns->setSpacing(12);
	columns->addWidget(left_column, 1);
	columns->addWidget(right_column, 0);
	content_layout->addLayout(columns, 1);

	m_workbench_scroll->setWidget(content);
	page_layout->addWidget(m_workbench_scroll, 1);

	// —— 面板手势 → 接口信号 ——
	connect(m_preview, &MediaPreviewPanel::select_input_requested, this,
			&IMainView::select_input_requested);
	connect(m_preview, &MediaPreviewPanel::delete_input_requested, this,
			&IMainView::delete_input_requested);
	connect(m_preview, &MediaPreviewPanel::set_output_path_requested, this,
			&IMainView::set_output_path_requested);
	connect(m_preview, &MediaPreviewPanel::open_output_folder_requested, this,
			&IMainView::open_output_folder_requested);
	connect(m_preview, &MediaPreviewPanel::save_as_requested, this,
			&IMainView::save_as_requested);
	connect(m_progress, &ProgressPanel::start_requested, this,
			&IMainView::start_requested);
	connect(m_progress, &ProgressPanel::pause_resume_requested, this,
			&IMainView::pause_resume_requested);
	connect(m_progress, &ProgressPanel::stop_requested, this,
			&IMainView::stop_requested);

	// 模式切换：视图内部联动 + 转发接口信号
	connect(m_function_mode, &FunctionModeGroup::mode_changed, this,
			&MainWindow::on_mode_changed_internal);
	connect(m_process_mode, &ProcessModeGroup::batch_mode_changed, this,
			[this](bool v_batch)
			{
				m_preview->set_batch_mode(v_batch);
				m_progress->set_batch_mode(v_batch);
				update_mode_hint();
				emit batch_mode_changed(v_batch);
			});

	// 初始：图片模式隐藏视频设置
	m_video_settings->set_controls_visible(false);

	return page;
}

QWidget *
MainWindow::create_settings_page()
{
	QWidget *page = new QWidget();
	page->setObjectName(QStringLiteral("settingsPage"));
	QVBoxLayout *page_layout = new QVBoxLayout(page);
	page_layout->setContentsMargins(0, 0, 0, 0);
	page_layout->setSpacing(0);

	QScrollArea *scroll = new QScrollArea(page);
	scroll->setObjectName(QStringLiteral("settingsScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	QWidget *content = new QWidget();
	content->setObjectName(QStringLiteral("settingsContent"));
	QVBoxLayout *content_layout = new QVBoxLayout(content);
	content_layout->setContentsMargins(24, 16, 24, 48);
	content_layout->setSpacing(12);

	// —— 外观卡片：三态主题切换 + 界面语言 ——
	CardWidget *appearance_card
		= new CardWidget(MainWindow::tr("外观"), content);
	HintLabel *appearance_hint = new HintLabel(
		MainWindow::tr("选择界面主题，切换立即生效并记住选择。"),
		appearance_card);
	appearance_card->body_layout()->addWidget(appearance_hint);

	m_theme_control = new SegmentedControl(appearance_card);
	m_theme_control->set_size_mode(SegmentedControl::SizeMode::MEDIUM);
	m_theme_control->add_item(MainWindow::tr("浅色"), QStringLiteral("sun"));
	m_theme_control->add_item(MainWindow::tr("深色"), QStringLiteral("moon"));
	m_theme_control->add_item(MainWindow::tr("跟随系统"),
							  QStringLiteral("monitor"));
	m_theme_control->setMinimumWidth(300);

	// 主题与语言两行使用同样的标签列宽，避免两条控制左端参差。
	QHBoxLayout *theme_row = new QHBoxLayout();
	theme_row->setContentsMargins(0, 0, 0, 0);
	theme_row->setSpacing(8);
	m_theme_label = new QLabel(MainWindow::tr("界面主题"), appearance_card);
	m_theme_label->setObjectName(QStringLiteral("fieldLabel"));
	m_theme_label->setFixedWidth(108);
	theme_row->addWidget(m_theme_label, 0, Qt::AlignVCenter);
	theme_row->addWidget(m_theme_control, 0, Qt::AlignLeft);
	theme_row->addStretch(1);
	appearance_card->body_layout()->addLayout(theme_row);

	connect(m_theme_control, &SegmentedControl::current_changed, this,
			[](int v_index)
			{
				ThemeManager::Mode mode = ThemeManager::Mode::FOLLOW_SYSTEM;
				if (v_index == 0)
				{
					mode = ThemeManager::Mode::LIGHT;
				}
				else if (v_index == 1)
				{
					mode = ThemeManager::Mode::DARK;
				}
				ThemeManager::instance().set_mode(mode);
			});

	// 界面语言：第二行（中文 / English / 跟随系统），切换立即生效并持久化；
	// 选中态由 retranslate_all() 里的 sync_language_control() 统一点亮。
	QHBoxLayout *language_row = new QHBoxLayout();
	language_row->setContentsMargins(0, 0, 0, 0);
	language_row->setSpacing(8);
	m_language_label = new QLabel(MainWindow::tr("界面语言"), appearance_card);
	m_language_label->setObjectName(QStringLiteral("fieldLabel"));
	m_language_label->setFixedWidth(108);
	language_row->addWidget(m_language_label, 0, Qt::AlignVCenter);

	m_language_segment = new SegmentedControl(appearance_card);
	m_language_segment->setObjectName(QStringLiteral("languageControl"));
	m_language_segment->set_size_mode(SegmentedControl::SizeMode::MEDIUM);
	m_language_segment->add_item(MainWindow::tr("中文"));
	m_language_segment->add_item(QStringLiteral("English"));
	m_language_segment->add_item(MainWindow::tr("跟随系统"));
	m_language_segment->setMinimumWidth(300);
	language_row->addWidget(m_language_segment, 0, Qt::AlignLeft);
	language_row->addStretch(1);
	appearance_card->body_layout()->addLayout(language_row);

	m_language_hint = new HintLabel(
		MainWindow::tr("语言切换立即生效，下次启动保持。"), appearance_card);
	appearance_card->body_layout()->addWidget(m_language_hint);

	connect(m_language_segment, &SegmentedControl::current_changed, this,
			[](int v_index)
			{
				LanguageManager::Language language
					= LanguageManager::Language::FOLLOW_SYSTEM;
				if (v_index == 0)
				{
					language = LanguageManager::Language::ZH_CN;
				}
				else if (v_index == 1)
				{
					language = LanguageManager::Language::EN;
				}
				// 由 Manager 负责持久化 + 换翻译器 + 发信号；视图不自己改 index
				LanguageManager::instance().set_language(language);
			});

	register_text_updater(
		[this, appearance_card, appearance_hint]()
		{
			appearance_card->set_title(MainWindow::tr("外观"));
			appearance_hint->setText(
				MainWindow::tr("选择界面主题，切换立即生效并记住选择。"));
		});

	// —— 超分引擎路径卡片：手动指定优先，自动发现回退 ——
	CardWidget *engine_card
		= new CardWidget(MainWindow::tr("超分引擎路径"), content);
	HintLabel *engine_hint = new HintLabel(
		MainWindow::tr("默认自动发现：从程序目录向上查找 bin/realesrgan。"
					   "手动指定后优先使用，改动在下次开始处理时生效。"),
		engine_card);
	engine_hint->setMaximumWidth(760);
	engine_card->body_layout()->addWidget(engine_hint);

	// 路径独占一行（长路径不截断），操作按钮与来源说明放下一行
	m_engine_path_field = new QLineEdit(engine_card);
	m_engine_path_field->setReadOnly(true);
	m_engine_path_field->setProperty("mono", true);
	m_engine_path_field->setPlaceholderText(
		MainWindow::tr("正在检测超分引擎位置…"));
	m_engine_path_field->setToolTip(
		MainWindow::tr("当前实际使用的超分引擎可执行文件路径"));
	engine_card->body_layout()->addWidget(m_engine_path_field);

	QHBoxLayout *engine_action_row = new QHBoxLayout();
	engine_action_row->setContentsMargins(0, 0, 0, 0);
	engine_action_row->setSpacing(8);

	m_engine_status_label = new HintLabel(
		MainWindow::tr("来源：自动发现（未手动指定）"), engine_card);
	engine_action_row->addWidget(m_engine_status_label, 1);

	ActionButton *browse_file_button = new ActionButton(
		ActionButton::Variant::SECONDARY, MainWindow::tr("选择文件…"),
		QStringLiteral("cpu"), engine_card);
	browse_file_button->setToolTip(
		MainWindow::tr("选择 realesrgan-ncnn-vulkan 可执行文件"));
	engine_action_row->addWidget(browse_file_button, 0, Qt::AlignVCenter);

	ActionButton *browse_dir_button = new ActionButton(
		ActionButton::Variant::SECONDARY, MainWindow::tr("选择目录…"),
		QStringLiteral("folder-open"), engine_card);
	browse_dir_button->setToolTip(
		MainWindow::tr("选择包含 realesrgan/ 的 bin 目录"));
	engine_action_row->addWidget(browse_dir_button, 0, Qt::AlignVCenter);

	m_engine_reset_button = new ActionButton(
		ActionButton::Variant::QUIET, MainWindow::tr("恢复自动发现"),
		QStringLiteral("rotate-ccw"), engine_card);
	m_engine_reset_button->setToolTip(
		MainWindow::tr("清除手动指定，回到自动发现"));
	m_engine_reset_button->setEnabled(false);
	engine_action_row->addWidget(m_engine_reset_button, 0, Qt::AlignVCenter);
	engine_card->body_layout()->addLayout(engine_action_row);

	connect(browse_file_button, &QPushButton::clicked, this,
			&IMainView::engine_executable_browse_requested);
	connect(browse_dir_button, &QPushButton::clicked, this,
			&IMainView::engine_directory_browse_requested);
	connect(m_engine_reset_button, &QPushButton::clicked, this,
			&IMainView::engine_path_reset_requested);

	register_text_updater(
		[this, engine_card, engine_hint, browse_file_button,
		 browse_dir_button]()
		{
			engine_card->set_title(MainWindow::tr("超分引擎路径"));
			engine_hint->setText(
				MainWindow::tr("默认自动发现：从程序目录向上查找 "
							   "bin/realesrgan。手动指定后优先使用，改动在下次"
							   "开始处理时生效。"));
			m_engine_path_field->setPlaceholderText(
				MainWindow::tr("正在检测超分引擎位置…"));
			m_engine_path_field->setToolTip(
				MainWindow::tr("当前实际使用的超分引擎可执行文件路径"));
			browse_file_button->setText(MainWindow::tr("选择文件…"));
			browse_file_button->setToolTip(
				MainWindow::tr("选择 realesrgan-ncnn-vulkan 可执行文件"));
			browse_dir_button->setText(MainWindow::tr("选择目录…"));
			browse_dir_button->setToolTip(
				MainWindow::tr("选择包含 realesrgan/ 的 bin 目录"));
			m_engine_reset_button->setText(MainWindow::tr("恢复自动发现"));
			m_engine_reset_button->setToolTip(
				MainWindow::tr("清除手动指定，回到自动发现"));
		});

	// —— 日志卡片：目录由日志层固定，只提供展示与打开入口 ——
	CardWidget *log_card = new CardWidget(MainWindow::tr("日志"), content);
	HintLabel *log_hint = new HintLabel(
		MainWindow::tr("运行日志固定写入程序目录下的 logs/"
					   "（5 MB 轮转、保留 5 份），用于排查问题。"),
		log_card);
	log_card->body_layout()->addWidget(log_hint);

	QHBoxLayout *log_row = new QHBoxLayout();
	log_row->setContentsMargins(0, 0, 0, 0);
	log_row->setSpacing(8);
	m_log_path_field = new QLineEdit(log_card);
	m_log_path_field->setReadOnly(true);
	m_log_path_field->setProperty("mono", true);
	m_log_path_field->setText(Logger::instance()->log_directory());
	m_log_path_field->setToolTip(MainWindow::tr("当前日志目录"));
	log_row->addWidget(m_log_path_field, 1);

	ActionButton *open_log_button = new ActionButton(
		ActionButton::Variant::SECONDARY, MainWindow::tr("打开日志目录"),
		QStringLiteral("folder-open"), log_card);
	open_log_button->setToolTip(MainWindow::tr("在文件管理器中打开日志目录"));
	log_row->addWidget(open_log_button, 0, Qt::AlignVCenter);
	log_card->body_layout()->addLayout(log_row);

	connect(open_log_button, &QPushButton::clicked, this,
			&IMainView::open_log_directory_requested);

	register_text_updater(
		[this, log_card, log_hint, open_log_button]()
		{
			log_card->set_title(MainWindow::tr("日志"));
			log_hint->setText(
				MainWindow::tr("运行日志固定写入程序目录下的 logs/"
							   "（5 MB 轮转、保留 5 份），用于排查问题。"));
			m_log_path_field->setToolTip(MainWindow::tr("当前日志目录"));
			open_log_button->setText(MainWindow::tr("打开日志目录"));
			open_log_button->setToolTip(
				MainWindow::tr("在文件管理器中打开日志目录"));
		});

	// —— 处理默认值卡片 ——
	m_defaults_card = new CardWidget(MainWindow::tr("处理默认值"), content);
	HintLabel *defaults_hint = new HintLabel(
		MainWindow::tr("把图片 / 视频的全部处理参数恢复为出厂默认值。"),
		m_defaults_card);
	m_defaults_card->body_layout()->addWidget(defaults_hint);
	m_defaults_reset_button = new ActionButton(
		ActionButton::Variant::SECONDARY, MainWindow::tr("恢复默认设置"),
		QStringLiteral("rotate-ccw"), m_defaults_card);
	ActionButton *reset_button = m_defaults_reset_button;
	reset_button->setToolTip(
		MainWindow::tr("重置模型、倍数、格式、编码等参数"));
	m_defaults_card->body_layout()->addWidget(reset_button, 0, Qt::AlignLeft);
	connect(reset_button, &QPushButton::clicked, this,
			&IMainView::reset_to_defaults);

	register_text_updater(
		[this, defaults_hint, reset_button]()
		{
			m_defaults_card->set_title(MainWindow::tr("处理默认值"));
			defaults_hint->setText(MainWindow::tr(
				"把图片 / 视频的全部处理参数恢复为出厂默认值。"));
			reset_button->setText(MainWindow::tr("恢复默认设置"));
			reset_button->setToolTip(
				MainWindow::tr("重置模型、倍数、格式、编码等参数"));
		});

	// —— 运行环境卡片 ——
	CardWidget *runtime_card
		= new CardWidget(MainWindow::tr("运行环境"), content);
	HintLabel *runtime_hint = new HintLabel(QString(), runtime_card);
	runtime_card->body_layout()->addWidget(runtime_hint);
	register_text_updater(
		[this, runtime_card, runtime_hint]()
		{
			runtime_card->set_title(MainWindow::tr("运行环境"));
			runtime_hint->setText(
				MainWindow::tr("界面：Qt %1 Widgets · C++17 · 无 QML\n"
							   "超分引擎：Real-ESRGAN（realesrgan-ncnn-vulkan）"
							   "\n"
							   "并发：单任务串行 —— 同一时刻只处理一项任务，"
							   "当前版本没有可配置的并发数\n"
							   "图标：Lucide（ISC 许可）")
					.arg(QString::fromLatin1(qVersion())));
		});

	content_layout->addWidget(appearance_card);
	content_layout->addWidget(engine_card);
	content_layout->addWidget(log_card);
	content_layout->addWidget(m_defaults_card);
	content_layout->addWidget(runtime_card);
	content_layout->addStretch(1);

	scroll->setWidget(content);
	page_layout->addWidget(scroll, 1);
	return page;
}

QWidget *
MainWindow::create_about_page()
{
	QWidget *page = new QWidget();
	page->setObjectName(QStringLiteral("aboutPage"));
	QScrollArea *scroll = new QScrollArea(page);
	scroll->setObjectName(QStringLiteral("aboutScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	QWidget *content = new QWidget();
	content->setObjectName(QStringLiteral("aboutContent"));
	QVBoxLayout *content_layout = new QVBoxLayout(content);
	content_layout->setContentsMargins(24, 32, 24, 48);
	content_layout->setSpacing(12);

	QHBoxLayout *centered = new QHBoxLayout();
	centered->setContentsMargins(0, 0, 0, 0);
	centered->setSpacing(0);
	centered->addStretch(1);

	CardWidget *card = new CardWidget(content);
	card->set_padding(28);
	card->setMaximumWidth(560);
	card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

	QLabel *icon_label = new QLabel(card);
	icon_label->setObjectName(QStringLiteral("aboutAppIcon"));
	QPixmap app_icon(QStringLiteral(":/app.png"));
	if (!app_icon.isNull())
	{
		icon_label->setPixmap(app_icon.scaled(96, 96, Qt::KeepAspectRatio,
											  Qt::SmoothTransformation));
	}
	icon_label->setAlignment(Qt::AlignCenter);
	card->body_layout()->addWidget(icon_label, 0, Qt::AlignHCenter);

	QLabel *title = new QLabel(MainWindow::tr("RealESRGAN 媒体处理工具"), card);
	title->setObjectName(QStringLiteral("pageHeaderTitle"));
	title->setProperty("level", QStringLiteral("app"));
	title->setAlignment(Qt::AlignCenter);
	card->body_layout()->addWidget(title);

	HintLabel *version = new HintLabel(QString(), card);
	version->setAlignment(Qt::AlignCenter);
	version->setWordWrap(false);
	card->body_layout()->addWidget(version);

	QLabel *description = new QLabel(QString(), card);
	description->setObjectName(QStringLiteral("aboutDescription"));
	description->setWordWrap(true);
	description->setAlignment(Qt::AlignCenter);
	card->body_layout()->addWidget(description);

	register_text_updater(
		[this, title, version, description]()
		{
			title->setText(MainWindow::tr("RealESRGAN 媒体处理工具"));
			version->setText(
				MainWindow::tr("版本 %1 · 纯 C++17 + Qt6 Widgets 实现")
					.arg(app_version()));
			description->setText(
				MainWindow::tr("基于 Real-ESRGAN 的图片 / 视频超分与放大工具："
							   "图片逐张超分、视频拆帧超分后重新编码，"
							   "支持批量处理、暂停 / 恢复 / 取消与实时进度。"));
		});

	QHBoxLayout *chips = new QHBoxLayout();
	chips->setContentsMargins(0, 4, 0, 4);
	chips->setSpacing(6);
	chips->addStretch(1);
	// 技术标识（Qt 6 / C++17 / Real-ESRGAN）不翻译；只有「Lucide 图标」可翻译
	const QStringList chip_texts
		= { QStringLiteral("Qt 6"), QStringLiteral("C++17"),
			QStringLiteral("Real-ESRGAN") };
	for (const QString &text : chip_texts)
	{
		StatusBadge *chip
			= new StatusBadge(text, StatusBadge::Tone::OUTLINE, card);
		chips->addWidget(chip, 0, Qt::AlignVCenter);
	}
	StatusBadge *lucide_chip = new StatusBadge(
		MainWindow::tr("Lucide 图标"), StatusBadge::Tone::OUTLINE, card);
	chips->addWidget(lucide_chip, 0, Qt::AlignVCenter);
	chips->addStretch(1);
	card->body_layout()->addLayout(chips);

	QHBoxLayout *buttons = new QHBoxLayout();
	buttons->setContentsMargins(0, 4, 0, 0);
	buttons->setSpacing(8);
	buttons->addStretch(1);
	ActionButton *license_button = new ActionButton(
		ActionButton::Variant::SECONDARY, MainWindow::tr("开源许可"),
		QStringLiteral("info"), card);
	buttons->addWidget(license_button, 0, Qt::AlignVCenter);
	buttons->addStretch(1);
	card->body_layout()->addLayout(buttons);

	connect(license_button, &QPushButton::clicked, this,
			[this]()
			{
				QMessageBox::information(
					this, MainWindow::tr("开源许可"),
					MainWindow::tr(
						"本工程：Real-ESRGAN 媒体处理工具（C++17 + Qt6）。\n\n"
						"界面图标：Lucide（https://lucide.dev），ISC 许可。\n"
						"超分引擎：Real-ESRGAN（BSD-3-Clause）及其 ncnn-vulkan "
						"可执行文件。\n"
						"Qt 6：GNU LGPL v3。\n\n"
						"应用图标为本工程自绘。"));
			});

	HintLabel *icon_hint = new HintLabel(QString(), card);
	card->body_layout()->addWidget(icon_hint);

	register_text_updater(
		[this, license_button, lucide_chip, icon_hint]()
		{
			license_button->setText(MainWindow::tr("开源许可"));
			lucide_chip->setText(MainWindow::tr("Lucide 图标"));
			icon_hint->setText(MainWindow::tr(
				"图标来自 Lucide（ISC 许可），应用图标为自绘。"));
		});

	centered->addWidget(card);
	centered->addStretch(1);
	content_layout->addLayout(centered, 1);

	scroll->setWidget(content);
	QVBoxLayout *page_layout = new QVBoxLayout(page);
	page_layout->setContentsMargins(0, 0, 0, 0);
	page_layout->setSpacing(0);
	page_layout->addWidget(scroll, 1);
	return page;
}

void
MainWindow::setup_window_buttons()
{
	// Windows 风格：页头最右侧 3 个 28×28 自绘按钮
	m_minimize_button
		= new ActionButton(ActionButton::Variant::ICON_COMPACT, QString(),
						   QStringLiteral("minus"), m_header);
	m_minimize_button->setObjectName(QStringLiteral("windowButton"));
	m_minimize_button->setToolTip(MainWindow::tr("最小化"));
	m_minimize_button->setAccessibleName(MainWindow::tr("最小化"));
	m_header->add_window_action(m_minimize_button);

	m_maximize_button
		= new ActionButton(ActionButton::Variant::ICON_COMPACT, QString(),
						   QStringLiteral("square"), m_header);
	m_maximize_button->setObjectName(QStringLiteral("windowButton"));
	m_maximize_button->setToolTip(MainWindow::tr("最大化"));
	m_maximize_button->setAccessibleName(MainWindow::tr("最大化"));
	m_header->add_window_action(m_maximize_button);

	m_close_button = new ActionButton(ActionButton::Variant::ICON_COMPACT,
									  QString(), QStringLiteral("x"), m_header);
	m_close_button->setObjectName(QStringLiteral("windowCloseButton"));
	m_close_button->setToolTip(MainWindow::tr("关闭"));
	m_close_button->setAccessibleName(MainWindow::tr("关闭"));
	m_close_button->set_hover_tone(IconProvider::Tone::DANGER);
	m_header->add_window_action(m_close_button);

	connect(m_minimize_button, &QPushButton::clicked, this,
			&QWidget::showMinimized);
	connect(m_maximize_button, &QPushButton::clicked, this,
			&MainWindow::toggle_maximized);
	connect(m_close_button, &QPushButton::clicked, this, &QWidget::close);

	update_window_button_icons();
}

// ---------------------------------------------------------------------------
// 自检 / 截图钩子（仅由环境变量触发，正常使用时不产生任何影响）
//   RESR_UI_PAGE = image|video|settings|about  启动后直接切到指定页面
//   RESR_UI_BATCH = 1                          单文件 / 批量模式批量态
//   RESR_UI_WIDTH / RESR_UI_HEIGHT             截图窗口尺寸（最小 1000×640）
//   RESR_UI_SHOT = <png 路径>                  启动后自动截图并退出
// 设计目的：让 UI 视觉回归不依赖窗口句柄 / 前台焦点，可在任意 CI 环境跑。
// ---------------------------------------------------------------------------
void
MainWindow::apply_debug_hooks()
{
	const QString page
		= qEnvironmentVariable("RESR_UI_PAGE").trimmed().toLower();
	if (page == QStringLiteral("settings"))
	{
		m_sidebar->set_current(NAV_SETTINGS);
		show_page(PAGE_SETTINGS);
	}
	else if (page == QStringLiteral("about"))
	{
		m_sidebar->set_current(NAV_ABOUT);
		show_page(PAGE_ABOUT);
	}
	else if (page == QStringLiteral("video"))
	{
		m_function_mode->set_mode(MediaKind::VIDEO);
		show_page(PAGE_WORKBENCH);
	}

	if (qEnvironmentVariable("RESR_UI_BATCH").trimmed() == QStringLiteral("1"))
	{
		m_process_mode->set_batch_mode(true);
	}

	// 视觉回归钩子：RESR_UI_SHOT 指定路径时，离屏渲染当前界面后退出（见下方说明）。
	// 渲染放到事件循环的第一拍：控制器在 MainWindow 构造之后才 initialize()，
	// 立即渲染会拍不到「设置页引擎路径」等由控制器下发的初始状态。
	const QString shot_path = qEnvironmentVariable("RESR_UI_SHOT").trimmed();
	if (shot_path.isEmpty())
	{
		return;
	}
	QTimer::singleShot(
		0, this,
		[this, shot_path]()
		{
			// 离屏渲染：不需要窗口前台焦点，也不依赖任何系统的屏幕抓取 /
			// 键盘注入，可在无人值守的 CI 里跑视觉回归。
			// RESR_UI_WIDTH/HEIGHT 用于最小窗口 / DPI 边界截图；
			// 低于最小尺寸时由 setMinimumSize 自动钳制。
			bool width_ok = false;
			bool height_ok = false;
			const int width
				= qEnvironmentVariable("RESR_UI_WIDTH").toInt(&width_ok);
			const int height
				= qEnvironmentVariable("RESR_UI_HEIGHT").toInt(&height_ok);
			resize(width_ok && width > 0 ? width : WINDOW_DEFAULT_WIDTH,
				   height_ok && height > 0 ? height : WINDOW_DEFAULT_HEIGHT);
			ensurePolished();
			// 尺寸 / 侧栏折叠会触发多轮布局；离屏截图必须等布局稳定，
			// 否则最小窗口会拍到旧宽度下的裁切画面。
			for (int i = 0; i < 3; ++i)
			{
				if (layout() != nullptr)
				{
					layout()->activate();
				}
				QCoreApplication::processEvents();
			}
			// 任务状态视觉回归：RESR_UI_DEMO_STATE= running|paused|
			// stopping|cancelled|completed|failed；仅截图模式有效。
			const QString demo_state
				= qEnvironmentVariable("RESR_UI_DEMO_STATE")
					  .trimmed()
					  .toLower();
			if (!demo_state.isEmpty())
			{
				ProcessStatus status;
				ControlState control;
				control.m_busy = true;
				control.m_primary_enabled = true;
				control.m_stop_enabled = true;
				control.m_primary_action = PrimaryAction::PAUSE;
				if (demo_state == QStringLiteral("running"))
				{
					status = ProcessStatus(ProcessState::RUNNING, 42, 3, 5,
										   QStringLiteral("photo_03.png"),
										   MainWindow::tr("处理中 3/5"),
										   QString());
				}
				else if (demo_state == QStringLiteral("paused"))
				{
					status = ProcessStatus(ProcessState::PAUSED, 42, 3, 5,
										   QStringLiteral("photo_03.png"),
										   MainWindow::tr("已暂停"), QString());
					control.m_primary_action = PrimaryAction::RESUME;
				}
				else if (demo_state == QStringLiteral("stopping"))
				{
					control.m_primary_action = PrimaryAction::START;
					status = ProcessStatus(ProcessState::IDLE, 62, 3, 5,
										   QStringLiteral("photo_03.png"),
										   MainWindow::tr("正在停止处理…"),
										   QString());
					control.m_primary_enabled = false;
					control.m_stop_enabled = false;
				}
				else if (demo_state == QStringLiteral("cancelled"))
				{
					control.m_primary_action = PrimaryAction::START;
					status = ProcessStatus(ProcessState::IDLE, 62, 3, 5,
										   QStringLiteral("photo_03.png"),
										   MainWindow::tr("已取消处理"),
										   QString());
					control.m_busy = false;
					control.m_stop_enabled = false;
				}
				else if (demo_state == QStringLiteral("completed"))
				{
					control.m_primary_action = PrimaryAction::START;
					status
						= ProcessStatus(ProcessState::COMPLETED, 100, 5, 5,
										QStringLiteral("photo_05.png"),
										MainWindow::tr("处理完成"), QString());
					control.m_busy = false;
					control.m_stop_enabled = false;
				}
				else if (demo_state == QStringLiteral("failed"))
				{
					control.m_primary_action = PrimaryAction::START;
					status = ProcessStatus(ProcessState::FAILED, 42, 3, 5,
										   QStringLiteral("photo_03.png"),
										   MainWindow::tr("处理失败: 引擎异常"),
										   QString());
					control.m_busy = false;
					control.m_stop_enabled = false;
				}
				m_last_status = status;
				m_last_control_state = control;
				m_progress->apply_status(status);
				m_progress->apply_control_state(control);
				update_status_badge(status);
				set_workbench_busy(control.m_busy);
				update_task_chip();
			}

			QPixmap pixmap(size());
			pixmap.fill(Qt::white);
			render(&pixmap);
			pixmap.save(shot_path);
			// 关闭走 force_close：Qt6 的 quit() 会先给顶层窗口发
			// QCloseEvent，而本窗口按契约在未 force 时会 ignore 并
			// 交给控制器；直接 quit() 会被取消导致离屏进程退不出。
			force_close();
			QTimer::singleShot(0, qApp, &QCoreApplication::quit);
		});
}

void
MainWindow::setup_shortcuts()
{
	// 文件 / 处理
	m_open_shortcut = new QShortcut(QKeySequence("Ctrl+O"), this);
	connect(m_open_shortcut, &QShortcut::activated, this,
			&IMainView::select_input_requested);

	m_output_shortcut = new QShortcut(QKeySequence("Ctrl+Shift+O"), this);
	connect(m_output_shortcut, &QShortcut::activated, this,
			&IMainView::set_output_path_requested);

	m_save_as_shortcut = new QShortcut(QKeySequence("Ctrl+Shift+S"), this);
	connect(m_save_as_shortcut, &QShortcut::activated, this,
			&IMainView::save_as_requested);

	m_refresh_shortcut = new QShortcut(QKeySequence("F5"), this);
	connect(m_refresh_shortcut, &QShortcut::activated, this,
			&IMainView::refresh_requested);

	// 视图
	QShortcut *sidebar_shortcut = new QShortcut(QKeySequence("Ctrl+B"), this);
	connect(sidebar_shortcut, &QShortcut::activated, this,
			[this]() { m_sidebar->set_collapsed(!m_sidebar->is_collapsed()); });

	QShortcut *theme_shortcut
		= new QShortcut(QKeySequence("Ctrl+Shift+T"), this);
	connect(theme_shortcut, &QShortcut::activated, this,
			[]() { ThemeManager::instance().cycle_mode(); });

	// Ctrl+1..4 切换页面；Esc 从设置 / 关于回到工作台
	const QList<QKeySequence> page_keys
		= { QKeySequence("Ctrl+1"), QKeySequence("Ctrl+2"),
			QKeySequence("Ctrl+3"), QKeySequence("Ctrl+4") };
	const QList<QString> page_ids
		= { NAV_IMAGE, NAV_VIDEO, NAV_SETTINGS, NAV_ABOUT };
	for (int i = 0; i < page_keys.size(); ++i)
	{
		QShortcut *shortcut = new QShortcut(page_keys.at(i), this);
		const QString id = page_ids.at(i);
		connect(shortcut, &QShortcut::activated, this,
				[this, id]() { on_nav_activated(id); });
	}

	// 任务控制快捷键：Ctrl+Enter 按当前状态开始/暂停/继续；Ctrl+Shift+X 停止。
	QShortcut *primary_shortcut
		= new QShortcut(QKeySequence("Ctrl+Return"), this);
	connect(primary_shortcut, &QShortcut::activated, this,
			[this]()
			{
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
	QShortcut *stop_shortcut
		= new QShortcut(QKeySequence("Ctrl+Shift+X"), this);
	connect(stop_shortcut, &QShortcut::activated, this,
			&IMainView::stop_requested);

	QShortcut *escape_shortcut = new QShortcut(QKeySequence("Escape"), this);
	connect(escape_shortcut, &QShortcut::activated, this,
			[this]()
			{
				if (m_pages->currentIndex() != PAGE_WORKBENCH)
				{
					show_page(PAGE_WORKBENCH);
					m_sidebar->set_current(is_video_mode() ? NAV_VIDEO
														   : NAV_IMAGE);
				}
				else
				{
					clearFocus();
				}
			});

	QShortcut *close_shortcut = new QShortcut(QKeySequence("Ctrl+Q"), this);
	connect(close_shortcut, &QShortcut::activated, this, &QWidget::close);

	// Tab 顺序：侧栏 → 页头操作 → 页面内容
	setTabOrder(m_sidebar, m_status_badge);
	setTabOrder(m_status_badge, m_refresh_button);
	setTabOrder(m_refresh_button, m_minimize_button);
	setTabOrder(m_minimize_button, m_maximize_button);
	setTabOrder(m_maximize_button, m_close_button);
	setTabOrder(m_close_button, m_pages);
}

// ---------------------------------------------------------------------------
// 交互
// ---------------------------------------------------------------------------
void
MainWindow::on_nav_activated(const QString &v_id)
{
	if (v_id == NAV_SETTINGS)
	{
		show_page(PAGE_SETTINGS);
		m_sidebar->set_current(NAV_SETTINGS);
		return;
	}
	if (v_id == NAV_ABOUT)
	{
		show_page(PAGE_ABOUT);
		m_sidebar->set_current(NAV_ABOUT);
		return;
	}
	if (v_id == NAV_IMAGE || v_id == NAV_VIDEO)
	{
		if (m_workbench_busy)
		{
			// 处理中禁止切换图片/视频模式，但**允许从设置/关于页回到工作台**
			// —— 否则任务控制（暂停/停止）不可达。侧栏高亮按当前模式恢复。
			if (m_pages->currentIndex() != PAGE_WORKBENCH)
			{
				show_page(PAGE_WORKBENCH);
			}
			m_sidebar->set_current(m_current_nav);
			return;
		}
		m_function_mode->set_mode((v_id == NAV_VIDEO) ? MediaKind::VIDEO
													  : MediaKind::IMAGE);
		show_page(PAGE_WORKBENCH);
		// 模式未变化时 set_mode 不会触发模式联动，这里显式保证侧栏高亮
		// 与即将显示的工作台页一致（含从设置 / 关于页按 Ctrl+1/2 的场景）。
		if (m_sidebar->current_id() != v_id)
		{
			m_sidebar->set_current(v_id);
		}
	}
}

void
MainWindow::on_mode_changed_internal(MediaKind v_kind)
{
	const bool video_mode = (v_kind == MediaKind::VIDEO);

	// 先保存离开模式的选择，再切换到目标模式并恢复其设置，避免视频默认模型
	// 或图片模型在模式间互相覆盖。
	if (m_basic != nullptr)
	{
		if (m_current_media_kind == MediaKind::IMAGE)
		{
			m_image_basic = m_basic->collect();
		}
		else
		{
			m_video_basic = m_basic->collect();
		}
		m_basic->set_media_kind(v_kind);
		m_basic->apply(video_mode ? m_video_basic : m_image_basic);
	}
	m_video_settings->set_controls_visible(video_mode);
	m_video_settings->setVisible(video_mode);
	m_preview->set_media_kind(v_kind);
	m_advanced->apply_media_kind(v_kind);
	// 图片模式右栏只有基本/高级两张卡，默认展开高级补齐高度并露出输出格式；
	// 视频模式有视频设置卡，收起高级保持三卡平衡。
	m_advanced->set_expanded(!video_mode);

	m_current_nav = video_mode ? NAV_VIDEO : NAV_IMAGE;
	// 仅工作台页由模式切换驱动侧栏高亮；设置 / 关于页保持自身选中项
	// （reset_to_defaults 在设置页触发模式复位时不应跳走侧栏高亮）。
	if (m_pages->currentIndex() == PAGE_WORKBENCH
		&& m_sidebar->current_id() != m_current_nav)
	{
		m_sidebar->set_current(m_current_nav);
	}

	m_current_media_kind = v_kind;
	update_header_for_current_page();
	update_mode_hint();
	update_task_chip();
	emit mode_changed(v_kind);
}

void
MainWindow::show_page(int v_page_index)
{
	m_pages->setCurrentIndex(v_page_index);
	if (m_preview != nullptr)
	{
		// 页面切换时清掉可能残留的拖放高亮（拖放只在工作台页有效）
		m_preview->set_drop_highlight(false);
	}
	update_header_for_current_page();
	update_mode_hint();
}

void
MainWindow::update_header_for_current_page()
{
	const int page = m_pages->currentIndex();
	const ThemeTokens &tokens = ThemeManager::instance().tokens();
	Q_UNUSED(tokens)

	if (page == PAGE_WORKBENCH)
	{
		const bool video_mode = is_video_mode();
		m_header->set_title_level(AppPageHeader::TitleLevel::APP);
		m_header->set_title(video_mode ? MainWindow::tr("视频处理")
									   : MainWindow::tr("图片处理"));
		m_header->set_subtitle(
			video_mode ? MainWindow::tr("拆帧超分 · 重新编码 · 保留原音轨")
					   : MainWindow::tr("Real-ESRGAN 图片超分与放大"));
		m_header->set_leading_icon(video_mode ? QStringLiteral("video")
											  : QStringLiteral("image"));
		m_status_badge->setVisible(true);
		// 图片模式也需要重置入口（F5 一直有效），避免按钮隐藏而快捷键可触发。
		m_refresh_button->setVisible(true);
	}
	else if (page == PAGE_SETTINGS)
	{
		m_header->set_title_level(AppPageHeader::TitleLevel::PAGE);
		m_header->set_title(MainWindow::tr("设置"));
		m_header->set_subtitle(
			MainWindow::tr("外观、引擎路径、日志与运行环境"));
		m_header->set_leading_icon(QStringLiteral("settings"));
		m_status_badge->setVisible(false);
		m_refresh_button->setVisible(false);
	}
	else
	{
		m_header->set_title_level(AppPageHeader::TitleLevel::PAGE);
		m_header->set_title(MainWindow::tr("关于"));
		m_header->set_subtitle(MainWindow::tr("版本与开源信息"));
		m_header->set_leading_icon(QStringLiteral("info"));
		m_status_badge->setVisible(false);
		m_refresh_button->setVisible(false);
	}
	update_task_chip();
}

void
MainWindow::update_task_chip()
{
	if (m_task_chip == nullptr)
	{
		return;
	}
	const bool non_workbench_page
		= (m_pages != nullptr && m_pages->currentIndex() != PAGE_WORKBENCH);
	const bool show = m_workbench_busy && non_workbench_page;
	m_task_chip->setVisible(show);
	if (!show)
	{
		return;
	}
	QString text;
	if (m_last_status.m_state == ProcessState::PAUSED)
	{
		text = MainWindow::tr("已暂停 %1%")
				   .arg(m_last_status.m_progress_percent);
	}
	else
	{
		text = MainWindow::tr("处理中 %1%")
				   .arg(m_last_status.m_progress_percent);
	}
	const QString tooltip = MainWindow::tr("返回工作台 · %1").arg(text);
	m_task_chip->setText(text);
	m_task_chip->setToolTip(tooltip);
	m_task_chip->setAccessibleName(tooltip);
}

void
MainWindow::update_mode_hint()
{
	if (m_mode_hint == nullptr)
	{
		return;
	}
	// 模式/处理方式已由左侧 tab 表达，这里不再重复；只保留一条简短引导。
	m_mode_hint->setText(is_batch_mode()
							 ? MainWindow::tr("选择或拖入文件夹即可开始")
							 : MainWindow::tr("选择或拖入文件即可开始"));
}

void
MainWindow::resizeEvent(QResizeEvent *v_event)
{
	IMainView::resizeEvent(v_event);
	const bool narrow = (v_event->size().width() < 1080);

	// 窄窗口自动折叠侧栏，把横向空间让给预览/设置；回到宽窗口再恢复。
	// 若用户本来手动折叠，m_auto_collapsed_sidebar=false，不会替用户展开。
	if (m_sidebar != nullptr)
	{
		if (narrow && !m_sidebar->is_collapsed())
		{
			m_sidebar->set_collapsed(true);
			m_auto_collapsed_sidebar = true;
		}
		else if (!narrow && m_auto_collapsed_sidebar)
		{
			m_sidebar->set_collapsed(false);
			m_auto_collapsed_sidebar = false;
		}
	}

	// 窄窗口下模式行横向空间不足：隐藏可选的模式提示，避免顶行被裁切；
	// 默认/更大窗口仍显示提示。
	if (m_mode_hint != nullptr)
	{
		m_mode_hint->setVisible(!narrow);
	}
}

void
MainWindow::on_theme_changed()
{
	m_sidebar->set_theme_mode(
		static_cast<int>(ThemeManager::instance().mode()));
	sync_theme_control();
	update_header_for_current_page();
	update_window_button_icons();
	m_sidebar->update();
	m_workbench_page->update();
	m_settings_page->update();
	m_about_page->update();
	update();
}

void
MainWindow::sync_theme_control()
{
	if (m_theme_control == nullptr)
	{
		return;
	}
	int index = 2;
	switch (ThemeManager::instance().mode())
	{
		case ThemeManager::Mode::LIGHT:
			index = 0;
			break;
		case ThemeManager::Mode::DARK:
			index = 1;
			break;
		case ThemeManager::Mode::FOLLOW_SYSTEM:
			index = 2;
			break;
	}
	m_theme_control->set_current_index(index, false);
}

void
MainWindow::register_text_updater(std::function<void()> v_updater)
{
	if (v_updater)
	{
		m_text_updaters.append(std::move(v_updater));
	}
}

void
MainWindow::sync_language_control()
{
	if (m_language_segment == nullptr)
	{
		return;
	}
	int index = 2; // FOLLOW_SYSTEM
	switch (LanguageManager::instance().language())
	{
		case LanguageManager::Language::ZH_CN:
			index = 0;
			break;
		case LanguageManager::Language::EN:
			index = 1;
			break;
		case LanguageManager::Language::FOLLOW_SYSTEM:
			index = 2;
			break;
	}
	// 程序化设置选中态不触发业务回调（真正切换由用户点击触发）
	const QSignalBlocker blocker(m_language_segment);
	m_language_segment->set_current_index(index, false);
}

void
MainWindow::update_language_hint()
{
	if (m_language_hint == nullptr)
	{
		return;
	}
	// 任务运行中 LanguageManager 会延迟 translator 装卸（视频 worker 线程
	// 数据竞争保护），此期间切换语言控件会变但界面不会立即刷新；视图侧
	// 统一在 busy 时禁用语言选择，并把提示改成「任务结束后可切换」，避免
	// 控件选中态与界面语言长期不一致、也避免与「立即生效」文档矛盾。
	m_language_hint->setText(
		m_workbench_busy
			? MainWindow::tr("任务运行中暂不可切换界面语言，任务结束后可切换。")
			: MainWindow::tr("语言切换立即生效，下次启动保持。"));
}

void
MainWindow::retranslate_all()
{
	// —— 窗口 / 侧栏 ——
	setWindowTitle(MainWindow::tr("RealESRGAN 媒体处理工具"));
	m_sidebar->set_item_text(NAV_IMAGE, MainWindow::tr("图片处理"));
	m_sidebar->set_item_text(NAV_VIDEO, MainWindow::tr("视频处理"));
	m_sidebar->set_item_text(NAV_SETTINGS, MainWindow::tr("设置"));
	m_sidebar->set_item_text(NAV_ABOUT, MainWindow::tr("关于"));
	m_sidebar->retranslate_ui();

	// —— 页头（标题 / 副标题 / 刷新按钮 / 状态徽章 / 模式提示）——
	update_header_for_current_page();
	m_refresh_button->setToolTip(
		MainWindow::tr("重置全部设置（清空输入/输出并恢复默认参数，F5）"));
	m_refresh_button->setAccessibleName(
		MainWindow::tr("重置全部设置（清空输入/输出并恢复默认参数）"));
	update_status_badge(m_last_status);
	update_mode_hint();

	// —— 外观卡片：主题 / 语言分段控件条目文本与语言选中态 ——
	if (m_theme_control != nullptr)
	{
		m_theme_control->set_item_text(0, MainWindow::tr("浅色"));
		m_theme_control->set_item_text(1, MainWindow::tr("深色"));
		m_theme_control->set_item_text(2, MainWindow::tr("跟随系统"));
	}
	if (m_language_segment != nullptr)
	{
		m_language_segment->set_item_text(0, MainWindow::tr("中文"));
		m_language_segment->set_item_text(1, QStringLiteral("English"));
		m_language_segment->set_item_text(2, MainWindow::tr("跟随系统"));
	}
	if (m_theme_label != nullptr)
	{
		m_theme_label->setText(MainWindow::tr("界面主题"));
	}
	if (m_language_label != nullptr)
	{
		m_language_label->setText(MainWindow::tr("界面语言"));
	}
	update_language_hint();
	sync_language_control();

	// —— 工作台各面板 ——
	if (m_function_mode != nullptr)
	{
		m_function_mode->retranslate_ui();
	}
	if (m_process_mode != nullptr)
	{
		m_process_mode->retranslate_ui();
	}
	if (m_basic != nullptr)
	{
		m_basic->retranslate_ui();
	}
	if (m_advanced != nullptr)
	{
		m_advanced->retranslate_ui();
	}
	if (m_video_settings != nullptr)
	{
		m_video_settings->retranslate_ui();
	}
	if (m_preview != nullptr)
	{
		m_preview->retranslate_ui();
	}
	if (m_progress != nullptr)
	{
		m_progress->retranslate_ui();
	}

	// —— 设置页 / 关于页静态文案（构建时注册的重设器）——
	for (const std::function<void()> &updater : m_text_updaters)
	{
		updater();
	}

	// —— 窗口按钮 ——
	if (m_minimize_button != nullptr)
	{
		m_minimize_button->setToolTip(MainWindow::tr("最小化"));
	}
	update_window_button_icons();
	if (m_close_button != nullptr)
	{
		m_close_button->setToolTip(MainWindow::tr("关闭"));
	}

	// —— 重绘（自绘控件按新 tr 文本重排）——
	m_sidebar->update();
	update();
}

void
MainWindow::update_status_badge(const ProcessStatus &v_status)
{
	if (m_status_badge == nullptr)
	{
		return;
	}
	QString text = MainWindow::tr("就绪");
	StatusBadge::Tone tone = StatusBadge::Tone::NEUTRAL;
	switch (v_status.m_state)
	{
		case ProcessState::IDLE:
		{
			// IDLE 承载「正在停止处理…/已取消处理」等显式消息；只显示
			// 泛化的「待处理」时仍回到「就绪」，避免重复。
			const QString idle_message = v_status.m_message.trimmed();
			// 「待处理」来自 AppController/ProcessService 两个上下文，不能只用
			// MainWindow::tr()（MainWindow 上下文没有该源串，英文下会误显示 Pending）。
			const QString idle_pending_process
				= QCoreApplication::translate("ProcessService", "待处理");
			const QString idle_pending_controller
				= QCoreApplication::translate("AppController", "待处理");
			if (!idle_message.isEmpty() && idle_message != idle_pending_process
				&& idle_message != idle_pending_controller
				&& idle_message != MainWindow::tr("就绪"))
			{
				// 页头徽章要短：完整消息放在 tooltip / 进度卡状态行。
				if (idle_message.contains(MainWindow::tr("停止"),
										  Qt::CaseInsensitive))
				{
					text = MainWindow::tr("停止中");
					tone = StatusBadge::Tone::WARNING;
				}
				else if (idle_message.contains(MainWindow::tr("取消"),
											   Qt::CaseInsensitive))
				{
					text = MainWindow::tr("已取消");
					tone = StatusBadge::Tone::NEUTRAL;
				}
				else
				{
					text = idle_message;
					tone = StatusBadge::Tone::NEUTRAL;
				}
			}
			break;
		}
		case ProcessState::RUNNING:
			text = MainWindow::tr("处理中");
			tone = StatusBadge::Tone::ACCENT;
			break;
		case ProcessState::PAUSED:
			text = MainWindow::tr("已暂停");
			tone = StatusBadge::Tone::WARNING;
			break;
		case ProcessState::COMPLETED:
			text = MainWindow::tr("已完成");
			tone = StatusBadge::Tone::SUCCESS;
			break;
		case ProcessState::FAILED:
			text = MainWindow::tr("失败");
			tone = StatusBadge::Tone::DANGER;
			break;
	}
	m_status_badge->setText(text);
	m_status_badge->set_tone(tone);
	m_status_badge->setToolTip(v_status.m_message);
}

void
MainWindow::update_window_button_icons()
{
	if (m_maximize_button == nullptr)
	{
		return;
	}
	const bool maximized = isMaximized();
	m_maximize_button->set_icon_name(maximized ? QStringLiteral("minimize")
											   : QStringLiteral("maximize"));
	m_maximize_button->setToolTip(maximized ? MainWindow::tr("还原")
											: MainWindow::tr("最大化"));

	QFrame *app_root = findChild<QFrame *>(QStringLiteral("appRoot"));
	if (app_root != nullptr)
	{
		app_root->setProperty("maximized",
							  maximized ? QStringLiteral("true") : QVariant());
		app_root->style()->unpolish(app_root);
		app_root->style()->polish(app_root);
		app_root->update();
	}
}

void
MainWindow::toggle_maximized()
{
	if (isMaximized())
	{
		showNormal();
	}
	else
	{
		showMaximized();
	}
	update_window_button_icons();
}

void
MainWindow::changeEvent(QEvent *v_event)
{
	QWidget::changeEvent(v_event);
	if (v_event->type() == QEvent::WindowStateChange)
	{
		update_window_button_icons();
	}
}

void
MainWindow::closeEvent(QCloseEvent *v_event)
{
	if (m_force_close)
	{
		v_event->accept();
		return;
	}
	// 关闭统一交给控制器：确认 / 停止运行中任务后再调用 force_close()。
	v_event->ignore();
	emit close_requested();
}

void
MainWindow::force_close()
{
	m_force_close = true;
	// 不能在这里直接 close()：force_close() 常常是在 closeEvent 的处理栈内
	// 被控制器同步调用的（用户点 X → event->ignore() → close_requested →
	// 控制器判定可关 → force_close）。在事件处理途中重入 close()，Qt 会把
	// 这次关闭并入外层事件，而外层已经 ignore()，结果窗口关不掉——表现为
	// "需要点两次 X"。排队到事件循环下一轮，等外层 closeEvent 返回后再关。
	QMetaObject::invokeMethod(
		this, [this]() { close(); }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// IMainView 实现
// ---------------------------------------------------------------------------
void
MainWindow::set_input_selection(const QString &v_path, int v_valid_file_count,
								bool v_is_directory)
{
	m_preview->set_input(v_path, v_valid_file_count, v_is_directory);
	// 批量目录 0 个有效文件时不显示「准备就绪」，同时控制器会禁用开始。
	m_progress->set_input_ready(!v_is_directory || v_valid_file_count > 0);
}

void
MainWindow::clear_input_selection()
{
	m_preview->clear_input();
	m_progress->set_input_ready(false);
}

void
MainWindow::set_output_path(const QString &v_path)
{
	m_preview->set_output_path(v_path);
}

void
MainWindow::clear_output()
{
	m_preview->clear_output();
}

void
MainWindow::show_output_preview(const QString &v_output_path)
{
	m_preview->show_output(v_output_path);
}

void
MainWindow::apply_status(const ProcessStatus &v_status)
{
	m_last_status = v_status;
	m_progress->apply_status(v_status);
	update_status_badge(v_status);
	update_task_chip();
}

void
MainWindow::set_progress_detail(int v_current, int v_total, int v_percent)
{
	m_progress->set_detail(v_current, v_total, v_percent);
}

void
MainWindow::apply_control_state(const ControlState &v_state)
{
	m_last_control_state = v_state;
	m_progress->apply_control_state(v_state);
	set_workbench_busy(v_state.m_busy);
}

void
MainWindow::set_workbench_busy(bool v_busy)
{
	if (m_workbench_busy == v_busy)
	{
		return;
	}
	m_workbench_busy = v_busy;
	update_task_chip();

	// 工作台编辑区：输入/输出预览、参数卡、模式分段、页头刷新。
	// 进度卡内的开始/暂停/继续与停止按钮由 ControlState 独立控制，不在此
	// 整体禁用；设置页 / 侧栏 / 窗口按钮也不受影响。
	if (m_preview != nullptr)
	{
		m_preview->setEnabled(!v_busy);
	}
	if (m_basic != nullptr)
	{
		m_basic->setEnabled(!v_busy);
	}
	if (m_video_settings != nullptr)
	{
		m_video_settings->setEnabled(!v_busy);
	}
	if (m_advanced != nullptr)
	{
		m_advanced->setEnabled(!v_busy);
	}
	if (m_function_mode != nullptr)
	{
		m_function_mode->setEnabled(!v_busy);
	}
	if (m_process_mode != nullptr)
	{
		m_process_mode->setEnabled(!v_busy);
	}
	if (m_refresh_button != nullptr)
	{
		m_refresh_button->setEnabled(!v_busy);
	}
	// 设置页的「恢复默认设置」会直接改工作台参数（绕过控制器保护），
	// 任务运行中禁用，避免 UI 与后台任务会话不一致。
	if (m_defaults_reset_button != nullptr)
	{
		m_defaults_reset_button->setEnabled(!v_busy);
	}
	// 语言选择：任务运行中 translator 装卸被延迟（model/common 的 deferral
	// 设计），此时切换控件已变但界面不会刷新。视图侧统一在 busy 时禁用
	// 语言段并在提示中明确「任务结束后可切换」，busy 结束后恢复。
	if (m_language_segment != nullptr)
	{
		m_language_segment->setEnabled(!v_busy);
		m_language_segment->setToolTip(
			v_busy ? MainWindow::tr(
						 "任务运行中暂不可切换界面语言，任务结束后可切换。")
				   : QString());
	}
	update_language_hint();
	if (v_busy && m_preview != nullptr)
	{
		// 任务开始时清掉可能残留的拖放高亮（busy 下不再接受拖放）
		m_preview->set_drop_highlight(false);
	}
	// 键盘入口同样禁用（按钮禁用对 QShortcut 无效）；Esc / Ctrl+3/4 等
	// 纯页面导航仍保留，以便处理中查看设置页。
	if (m_open_shortcut != nullptr)
	{
		m_open_shortcut->setEnabled(!v_busy);
	}
	if (m_output_shortcut != nullptr)
	{
		m_output_shortcut->setEnabled(!v_busy);
	}
	if (m_save_as_shortcut != nullptr)
	{
		m_save_as_shortcut->setEnabled(!v_busy);
	}
	if (m_refresh_shortcut != nullptr)
	{
		m_refresh_shortcut->setEnabled(!v_busy);
	}
}

void
MainWindow::apply_engine_path_info(const QString &v_effective_executable,
								   const QString &v_configured_path,
								   const QString &v_status_text)
{
	if (m_engine_path_field != nullptr)
	{
		m_engine_path_field->setText(v_effective_executable);
		// 路径从左开始显示，避免长路径只露出末尾
		m_engine_path_field->setCursorPosition(0);
	}
	if (m_engine_status_label != nullptr)
	{
		m_engine_status_label->setText(v_status_text);
	}
	// 「恢复自动发现」只在存在手动指定时可用
	if (m_engine_reset_button != nullptr)
	{
		m_engine_reset_button->setEnabled(!v_configured_path.isEmpty());
	}
}

void
MainWindow::reset_to_defaults()
{
	m_function_mode->set_mode(MediaKind::IMAGE);
	m_process_mode->set_batch_mode(false);
	m_preview->set_batch_mode(false);
	m_basic->reset_to_defaults();
	m_advanced->reset_to_defaults();
	m_video_settings->reset_to_defaults();
	m_video_settings->set_controls_visible(false);
	m_video_settings->setVisible(false);
	m_image_basic = m_basic->collect();
	m_video_basic = BasicSettings();
	m_video_basic.m_image_type = ImageType::ANIME_IMAGE;
	m_video_basic.m_model_type = ModelType::REAL_ESR_ANIMEVIDEOV3;
	m_video_basic.m_scale_factor = ScaleFactor::SCALE4X;
	m_current_nav = NAV_IMAGE;
	// 只在工作台页同步导航高亮；在设置页 / 关于页点「恢复默认设置」时
	// 不能把侧栏选中项跳到「图片处理」而页面还停在设置页。
	if (m_pages->currentIndex() == PAGE_WORKBENCH)
	{
		m_sidebar->set_current(NAV_IMAGE);
	}
	update_header_for_current_page();
	update_mode_hint();
}

MediaSettings
MainWindow::collect_settings() const
{
	MediaSettings settings;
	settings.m_kind = m_function_mode->media_kind();
	settings.m_basic = m_basic->collect();
	settings.m_advanced = m_advanced->collect();
	settings.m_process_mode = m_process_mode->is_batch_mode()
								  ? ProcessMode::BATCH_PROCESS
								  : ProcessMode::SINGLE_FILE;
	settings.m_video_encoding = m_video_settings->encoding();
	settings.m_video_quality = m_video_settings->quality();
	return settings;
}

bool
MainWindow::is_video_mode() const
{
	return m_function_mode->media_kind() == MediaKind::VIDEO;
}

bool
MainWindow::is_batch_mode() const
{
	return m_process_mode->is_batch_mode();
}

// ---------------------------------------------------------------------------
// 拖放
// ---------------------------------------------------------------------------
void
MainWindow::dragEnterEvent(QDragEnterEvent *v_event)
{
	// 拖放高亮由主窗口按「是否接受」统一驱动：只有工作台（图片 / 视频处理）
	// 且非 busy 时接受；设置 / 关于页、处理中一律不高亮，避免面板事件过滤器
	// 在 drop 被拒绝时仍显示可拖放暗示。
	if (m_workbench_busy || m_pages->currentIndex() != PAGE_WORKBENCH)
	{
		if (m_preview != nullptr)
		{
			m_preview->set_drop_highlight(false);
		}
		v_event->ignore();
		return;
	}
	if (v_event->mimeData()->hasUrls())
	{
		if (m_preview != nullptr)
		{
			m_preview->set_drop_highlight(true);
		}
		v_event->acceptProposedAction();
	}
	else if (m_preview != nullptr)
	{
		m_preview->set_drop_highlight(false);
	}
}

void
MainWindow::dragLeaveEvent(QDragLeaveEvent *v_event)
{
	if (m_preview != nullptr)
	{
		m_preview->set_drop_highlight(false);
	}
	QWidget::dragLeaveEvent(v_event);
}

void
MainWindow::dropEvent(QDropEvent *v_event)
{
	if (m_preview != nullptr)
	{
		m_preview->set_drop_highlight(false);
	}
	if (m_workbench_busy || m_pages->currentIndex() != PAGE_WORKBENCH)
	{
		v_event->ignore();
		return;
	}
	QStringList files;
	const QList<QUrl> urls = v_event->mimeData()->urls();
	for (const QUrl &url : urls)
	{
		const QString local_path = url.toLocalFile();
		if (!local_path.isEmpty())
		{
			files << local_path;
		}
	}

	if (!files.isEmpty())
	{
		emit files_dropped(files);
	}
}

// ---------------------------------------------------------------------------
// 无边框窗口：Windows 原生 8 方向边缘缩放
// （页头拖动走 QWindow::startSystemMove，这里只处理边缘 / 角落命中）
// ---------------------------------------------------------------------------
bool
MainWindow::nativeEvent(const QByteArray &v_event_type, void *v_message,
						qintptr *v_result)
{
#ifdef Q_OS_WIN
	if (v_event_type == "windows_generic_MSG" && v_message != nullptr
		&& !isMaximized() && !isFullScreen())
	{
		MSG *msg = static_cast<MSG *>(v_message);
		if (msg->message == WM_NCHITTEST)
		{
			const HWND hwnd = reinterpret_cast<HWND>(winId());
			RECT window_rect;
			if (::GetWindowRect(hwnd, &window_rect))
			{
				const int border = qRound(RESIZE_BORDER * devicePixelRatioF());
				const int x = GET_X_LPARAM(msg->lParam);
				const int y = GET_Y_LPARAM(msg->lParam);

				const bool left
					= (x >= window_rect.left && x < window_rect.left + border);
				const bool right = (x <= window_rect.right - 1
									&& x > window_rect.right - border);
				const bool top
					= (y >= window_rect.top && y < window_rect.top + border);
				const bool bottom = (y <= window_rect.bottom - 1
									 && y > window_rect.bottom - border);

				qintptr hit = 0;
				if (top && left)
				{
					hit = HTTOPLEFT;
				}
				else if (top && right)
				{
					hit = HTTOPRIGHT;
				}
				else if (bottom && left)
				{
					hit = HTBOTTOMLEFT;
				}
				else if (bottom && right)
				{
					hit = HTBOTTOMRIGHT;
				}
				else if (left)
				{
					hit = HTLEFT;
				}
				else if (right)
				{
					hit = HTRIGHT;
				}
				else if (top)
				{
					hit = HTTOP;
				}
				else if (bottom)
				{
					hit = HTBOTTOM;
				}

				if (hit != 0)
				{
					*v_result = hit;
					return true;
				}
			}
		}
	}
#endif
	return IMainView::nativeEvent(v_event_type, v_message, v_result);
}