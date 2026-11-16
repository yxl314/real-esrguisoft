// ============================================================================
// 界面语言基础设施单元测试（Qt Test，Linux / Windows 通用）
//
// 覆盖 LanguageManager 的纯函数与持久化语义（设计文档 i18n-design.md §1.2）：
//   - QSettings 键/值映射（zh / en / system，非法值回退跟随系统）；
//   - 跟随系统的 locale 解析与「翻译资源不可用时回退中文」；
//   - translator 资源路径；
//   - set_language() 的持久化 / 同值短路 / .qm 缺失回退中文。
//
// 注意：测试目标不链接 qrc（没有 :/i18n/app_en.qm），因此 translator 加载
// 必然失败 → 正好覆盖「回退中文」路径；英文资产由 task-27 生成并由集成
// 截图验证。测试进程不调用 initialize() 的启动覆盖分支（环境变量部分由
// 集成启动用例验证）。
// ============================================================================

#include "language_manager.h"
#include "mediasettings.h"
#include "processstatus.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class LanguageManagerTest : public QObject
{
	Q_OBJECT

  private slots:
	void
	initTestCase();
	void
	settings_value_mapping();
	void
	system_locale_resolution();
	void
	resolve_effective_fallback();
	void
	translator_resource_path();
	void
	domain_display_names_use_source_chinese();
	void
	set_language_persists_and_falls_back();
	void
	set_language_reentrancy_guard();
	void
	translation_deferral_applies_after_worker_scope();

  private:
	// 临时配置目录放在测试可执行文件所在构建目录（仓库内）：Windows 的
	// 仓库目录受限令牌禁止向仓库外写入，默认 QTemporaryDir（系统 TEMP）
	// 会导致 QSettings 持久化用例在仓库内误失败（仓库外副本则 PASS）。
	QTemporaryDir m_config_dir{ QDir(QCoreApplication::applicationDirPath())
									.filePath(QStringLiteral(
										"i18n-test-config-XXXXXX")) };
};

void
LanguageManagerTest::initTestCase()
{
	// 隔离 QSettings 到临时 XDG 目录：必须在任何 QSettings 创建之前设置，
	// 避免污染真实用户配置（与 verify/截图脚本的隔离策略一致）。
	QVERIFY2(m_config_dir.isValid(), "无法创建临时配置目录");
	qputenv("XDG_CONFIG_HOME", m_config_dir.path().toUtf8());

	// QSettings 默认构造依赖 QCoreApplication 的身份；生产由 main.cpp 设置，
	// 测试在此设置同样身份（否则组织/应用名为空）。
	QCoreApplication::setOrganizationName(QStringLiteral("real-esrguisoft"));
	QCoreApplication::setApplicationName(QStringLiteral("real-esrguisoft"));

	// 跨平台隔离：默认格式改为 IniFormat 并指向构建目录内的临时目录
	// （Windows 仓库受限令牌禁止写仓库外，避免误失败/污染真实注册表）。
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
					   m_config_dir.path());
}

void
LanguageManagerTest::settings_value_mapping()
{
	QCOMPARE(LanguageManager::settings_key(), QStringLiteral("ui/language"));

	QCOMPARE(
		LanguageManager::to_settings_value(LanguageManager::Language::ZH_CN),
		QStringLiteral("zh"));
	QCOMPARE(LanguageManager::to_settings_value(LanguageManager::Language::EN),
			 QStringLiteral("en"));
	QCOMPARE(LanguageManager::to_settings_value(
				 LanguageManager::Language::FOLLOW_SYSTEM),
			 QStringLiteral("system"));

	bool ok = false;
	QCOMPARE(LanguageManager::from_settings_value(QStringLiteral("zh"), &ok),
			 LanguageManager::Language::ZH_CN);
	QVERIFY(ok);
	QCOMPARE(LanguageManager::from_settings_value(QStringLiteral(" EN "), &ok),
			 LanguageManager::Language::EN);
	QVERIFY(ok);
	QCOMPARE(
		LanguageManager::from_settings_value(QStringLiteral("system"), &ok),
		LanguageManager::Language::FOLLOW_SYSTEM);
	QVERIFY(ok);

	// 缺失/非法值：回退跟随系统且标记非法（不崩溃）。
	QCOMPARE(LanguageManager::from_settings_value(QString(), &ok),
			 LanguageManager::Language::FOLLOW_SYSTEM);
	QVERIFY(!ok);
	QCOMPARE(
		LanguageManager::from_settings_value(QStringLiteral("klingon"), &ok),
		LanguageManager::Language::FOLLOW_SYSTEM);
	QVERIFY(!ok);
}

void
LanguageManagerTest::system_locale_resolution()
{
	// 中文系 locale（zh_CN / zh_TW / zh_HK…）→ 中文，其余一律英文。
	QCOMPARE(LanguageManager::system_language(
				 QLocale(QLocale::Chinese, QLocale::China)),
			 LanguageManager::Language::ZH_CN);
	QCOMPARE(LanguageManager::system_language(
				 QLocale(QLocale::Chinese, QLocale::Taiwan)),
			 LanguageManager::Language::ZH_CN);
	QCOMPARE(LanguageManager::system_language(
				 QLocale(QLocale::English, QLocale::UnitedStates)),
			 LanguageManager::Language::EN);
	QCOMPARE(LanguageManager::system_language(
				 QLocale(QLocale::German, QLocale::Germany)),
			 LanguageManager::Language::EN);
}

void
LanguageManagerTest::resolve_effective_fallback()
{
	using Language = LanguageManager::Language;
	const auto resolve_effective = &LanguageManager::resolve_effective;

	const QLocale chinese(QLocale::Chinese, QLocale::China);
	const QLocale english(QLocale::English, QLocale::UnitedStates);

	// 显式选择
	QCOMPARE(resolve_effective(Language::ZH_CN, true, english),
			 Language::ZH_CN);
	QCOMPARE(resolve_effective(Language::EN, true, chinese), Language::EN);
	// 英文资源不可用 → 回退源语言中文（含显式选择与跟随系统）
	QCOMPARE(resolve_effective(Language::EN, false, english), Language::ZH_CN);
	QCOMPARE(resolve_effective(Language::FOLLOW_SYSTEM, false, english),
			 Language::ZH_CN);
	// 跟随系统解析
	QCOMPARE(resolve_effective(Language::FOLLOW_SYSTEM, true, chinese),
			 Language::ZH_CN);
	QCOMPARE(resolve_effective(Language::FOLLOW_SYSTEM, true, english),
			 Language::EN);
}

void
LanguageManagerTest::translator_resource_path()
{
	QCOMPARE(LanguageManager::translator_path(LanguageManager::Language::EN),
			 QStringLiteral(":/i18n/app_en.qm"));
	QCOMPARE(LanguageManager::translator_path(LanguageManager::Language::ZH_CN),
			 QString());
	QCOMPARE(LanguageManager::translator_path(
				 LanguageManager::Language::FOLLOW_SYSTEM),
			 QString());
	// 翻译资产由 task-27 生成并 qrc 嵌入；本测试目标无 qrc，加载应失败。
	QVERIFY(!QFileInfo::exists(QStringLiteral(":/i18n/app_en.qm")));
}

void
LanguageManagerTest::domain_display_names_use_source_chinese()
{
	// 测试进程没有安装 translator → tr/translate 返回中文源串；现有中文
	// 断言（tests/test_pipeline.cpp 等）不因 i18n 改造而失效。
	QCOMPARE(media_kind_to_string(MediaKind::VIDEO), QStringLiteral("视频"));
	QCOMPARE(media_kind_to_string(MediaKind::IMAGE), QStringLiteral("图片"));
	QCOMPARE(media_kind_token(MediaKind::VIDEO), QStringLiteral("video"));
	QCOMPARE(media_kind_token(MediaKind::IMAGE), QStringLiteral("image"));
	QCOMPARE(image_type_to_string(ImageType::REAL_IMAGE),
			 QStringLiteral("真实图片"));
	QCOMPARE(scale_factor_to_string(ScaleFactor::SCALE4X),
			 QStringLiteral("4倍"));
	QCOMPARE(process_mode_to_string(ProcessMode::BATCH_PROCESS),
			 QStringLiteral("批量处理"));
	QCOMPARE(video_quality_to_string(VideoQuality::BALANCED),
			 QStringLiteral("均衡"));
	QCOMPARE(output_format_to_string(OutputFormat::ORIGINAL),
			 QStringLiteral("原格式"));
	QCOMPARE(gpu_device_to_string(GPUDevice::AUTO), QStringLiteral("自动"));
	QCOMPARE(process_state_label(ProcessState::IDLE), QStringLiteral("待处理"));
	QCOMPARE(tile_size_auto_token(), QStringLiteral("自动"));

	const QStringList image_names = image_output_format_names();
	QCOMPARE(image_names,
			 (QStringList{ QStringLiteral("原格式"), QStringLiteral("jpg"),
						   QStringLiteral("png"), QStringLiteral("webp") }));
	const QStringList video_names = video_output_format_names();
	QCOMPARE(video_names.size(), 6);
	QVERIFY(!video_names.contains(QStringLiteral("gif")));

	const QList<OutputFormat> image_options = image_output_format_options();
	QCOMPARE(image_options.size(), 4);
	QCOMPARE(image_options.first(), OutputFormat::ORIGINAL);
	const QList<OutputFormat> video_options = video_output_format_options();
	QCOMPARE(video_options.size(), 6);
	QVERIFY(!video_options.contains(OutputFormat::GIF));

	// string_to_* 规范值保持兼容（测试/CLI/旧配置解析依赖中文比较值）。
	QCOMPARE(string_to_output_format(QStringLiteral("原格式")),
			 OutputFormat::ORIGINAL);
	QCOMPARE(string_to_image_type(QStringLiteral("动漫图片")),
			 ImageType::ANIME_IMAGE);
	QCOMPARE(string_to_video_quality(QStringLiteral("均衡")),
			 VideoQuality::BALANCED);
}

void
LanguageManagerTest::set_language_persists_and_falls_back()
{
	LanguageManager &manager = LanguageManager::instance();
	QSettings settings;
	settings.clear();

	// 测试目标没有 qrc 英文资源：选择英文会写盘，但生效语言回退中文。
	QVERIFY(!manager.set_language(LanguageManager::Language::EN)
			|| manager.effective_language() == LanguageManager::Language::EN);
	QCOMPARE(manager.language(), LanguageManager::Language::EN);
	QCOMPARE(manager.effective_language(), LanguageManager::Language::ZH_CN);
	settings.sync();
	QCOMPARE(settings.value(QStringLiteral("ui/language")).toString(),
			 QStringLiteral("en"));
	// 同上选择重复调用：不写盘、不发信号（返回 false）。
	QVERIFY(!manager.set_language(LanguageManager::Language::EN));

	// 切回中文：选择值与生效值均为中文，选择值持久化。
	QVERIFY(!manager.set_language(LanguageManager::Language::ZH_CN));
	QCOMPARE(manager.language(), LanguageManager::Language::ZH_CN);
	settings.sync();
	QCOMPARE(settings.value(QStringLiteral("ui/language")).toString(),
			 QStringLiteral("zh"));

	// 跟随系统：解析为 locale 对应语言；英文不可用时同样回退中文。
	manager.set_language(LanguageManager::Language::FOLLOW_SYSTEM);
	QCOMPARE(manager.language(), LanguageManager::Language::FOLLOW_SYSTEM);
	settings.sync();
	QCOMPARE(settings.value(QStringLiteral("ui/language")).toString(),
			 QStringLiteral("system"));
}

void
LanguageManagerTest::set_language_reentrancy_guard()
{
	using Language = LanguageManager::Language;
	LanguageManager &manager = LanguageManager::instance();

	// 先把语言选择归位到中文（此前的持久化测试可能留下 system）。
	manager.set_language(Language::ZH_CN);

	int signal_count = 0;
	bool nested_result = true;
	bool nested_called = false;
	const QMetaObject::Connection connection
		= connect(&manager, &LanguageManager::language_changed, this,
				  [&]()
				  {
					  ++signal_count;
					  if (!nested_called)
					  {
						  nested_called = true;
						  // 在 language_changed 槽里再次切换：必须被 m_applying 拒绝，
						  // 不能递归广播或改写外层选择。
						  nested_result = manager.set_language(Language::ZH_CN);
					  }
				  });

	manager.set_language(Language::EN);
	disconnect(connection);

	QVERIFY(nested_called);
	QCOMPARE(signal_count, 1);
	QVERIFY(!nested_result);
	QCOMPARE(static_cast<int>(manager.language()),
			 static_cast<int>(Language::EN));
	// 英文资源缺失 → 生效语言回退中文。
	QCOMPARE(static_cast<int>(manager.effective_language()),
			 static_cast<int>(Language::ZH_CN));

	manager.set_language(Language::ZH_CN);
}

void
LanguageManagerTest::translation_deferral_applies_after_worker_scope()
{
	using Language = LanguageManager::Language;
	LanguageManager &manager = LanguageManager::instance();
	manager.set_language(Language::ZH_CN);

	int signal_count = 0;
	const QMetaObject::Connection connection
		= connect(&manager, &LanguageManager::language_changed,
				  [&signal_count]() { ++signal_count; });

	// 视频任务运行期间的切换被延迟：选择立即持久化，但不装卸 translator、
	// 不广播（worker 线程仍在使用当前翻译器列表）。
	LanguageManager::begin_translation_deferral();
	LanguageManager::begin_translation_deferral(); // 嵌套计数
	QVERIFY(!manager.set_language(Language::EN));
	QCOMPARE(signal_count, 0);
	QCOMPARE(static_cast<int>(manager.language()),
			 static_cast<int>(Language::EN));
	QCOMPARE(static_cast<int>(manager.effective_language()),
			 static_cast<int>(Language::ZH_CN));

	// 内层作用域结束不应用；最外层结束后应用并广播一次。
	LanguageManager::end_translation_deferral();
	QCOMPARE(signal_count, 0);
	LanguageManager::end_translation_deferral();
	QCOMPARE(signal_count, 1);
	QCOMPARE(static_cast<int>(manager.effective_language()),
			 static_cast<int>(Language::ZH_CN));

	// 退出清理路径：丢弃待应用切换、不广播。
	LanguageManager::begin_translation_deferral();
	QVERIFY(!manager.set_language(Language::FOLLOW_SYSTEM));
	QCOMPARE(signal_count, 1);
	LanguageManager::end_translation_deferral(false);
	QCOMPARE(signal_count, 1);
	QCOMPARE(static_cast<int>(manager.language()),
			 static_cast<int>(Language::FOLLOW_SYSTEM));

	disconnect(connection);
	manager.set_language(Language::ZH_CN);
}

QTEST_GUILESS_MAIN(LanguageManagerTest)

#include "test_i18n.moc"
