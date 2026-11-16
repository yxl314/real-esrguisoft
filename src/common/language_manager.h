#ifndef LANGUAGE_MANAGER_H
#define LANGUAGE_MANAGER_H

// ============================================================================
// 公共层 - 应用级界面语言管理
//
// 职责（详见 .cxx-review/out/real-esrguisoft/i18n-design.md §1）：
//   - 维护用户选择（中文 / English / 跟随系统）与持久化（QSettings ui/language）；
//   - 启动时按 RESR_UI_LANG（system|zh|en，仅本次运行）> QSettings > 跟随系统
//     决定生效语言，并在任何界面对象构造前安装 QTranslator；
//   - 运行期 set_language() 立即保存 → 换 translator → emit language_changed()，
//     由 MainWindow retranslate_all()（先连接）与 AppController（后连接）刷新界面。
//
// 仅依赖 QtCore（不依赖 Widgets），加入 resr_core；测试进程不初始化本类时
// tr()/translate() 直接返回中文源串，既有中文断言不受影响。
// ============================================================================

#include <QLocale>
#include <QObject>
#include <QString>

class QTranslator;

class LanguageManager : public QObject
{
	Q_OBJECT

  public:
	// 应用级界面语言：ZH_CN 为源语言（无需 translator），EN 需 :/i18n/app_en.qm。
	enum class Language
	{
		ZH_CN = 0,		  // 中文（源串）
		EN = 1,			  // English（安装 app_en.qm）
		FOLLOW_SYSTEM = 2 // 跟随系统（按 QLocale::system() 解析）
	};
	Q_ENUM(Language)

	static LanguageManager &
	instance();

	// QSettings 键/值映射：键固定 "ui/language"，值 "zh" / "en" / "system"。
	static QString
	settings_key();
	static QString
	to_settings_value(Language v_language);
	// 非法字符串返回 FOLLOW_SYSTEM；v_ok 非空时回填是否合法。
	static Language
	from_settings_value(const QString &v_value, bool *v_ok = nullptr);

	// 系统语言解析（纯函数）：中文系 locale → 中文，其余 → 英文。
	static Language
	system_language(const QLocale &v_system_locale);

	// 纯函数（可单测）：FOLLOW_SYSTEM 解析 + translator 不可用时回退中文。
	static Language
	resolve_effective(Language v_requested, bool v_translation_available,
					  QLocale v_system_locale = QLocale::system());

	// 英文 → ":/i18n/app_en.qm"，中文 → 空。
	static QString
	translator_path(Language v_effective_language);

	// 启动调用一次：环境变量 > QSettings > FOLLOW_SYSTEM；装 translator；
	// 不写 QSettings、不发信号（此时界面对象尚未构造）。
	void
	initialize();

	// 用户选择（持久化语义，可能为 FOLLOW_SYSTEM）。
	Language
	language() const;
	// 实际生效语言：ZH_CN 或 EN（不含 FOLLOW_SYSTEM）。
	Language
	effective_language() const;

	// 实时切换：保存设置 → 换 translator → emit language_changed()。
	// 选择未变化直接返回 false（不写盘、不发信号）；返回值表示生效语言是否变化。
	// translator 加载失败时回退中文（选择值仍持久化），下一次资源恢复后生效。
	// 仅主线程调用；language_changed 发射期间的重入调用会被拒绝（m_applying）。
	bool
	set_language(Language v_language);

	// ---- 翻译器热切换的后台线程防护（数据竞争） ----
	// Qt 6 的 QCoreApplication::translate() 读取翻译器列表时不加锁，而视频链路
	// 的工作线程（VideoJobWorker）在执行期间会调用 translate。若主线程此时
	// install/removeTranslator，即构成数据竞争（helgrind 可复现，Qt 6.10 未同步）。
	// 处理任务运行期间调用 begin_translation_deferral()，期间 set_language()
	// 只持久化选择并记录待应用语言；end_translation_deferral() 在 worker 退出后
	// 由主线程应用 translator 并广播 language_changed()。
	// 计数支持嵌套；仅主线程调用。
	static void
	begin_translation_deferral();
	// v_apply_pending=false 用于程序退出清理：丢弃待应用切换、不广播。
	static void
	end_translation_deferral(bool v_apply_pending = true);

  signals:
	// 语言已切换：视图先 retranslate，控制器随后重下发动态状态文案。
	void
	language_changed();

  private:
	explicit LanguageManager(QObject *v_parent = nullptr);
	~LanguageManager() override;

	// 卸载旧 translator，按需安装新的；失败返回 false（调用方回退中文）。
	bool
	apply_translator(Language v_effective_language);

	// 应用选择：解析生效语言、装卸 translator、更新 m_effective 并广播。
	bool
	apply_selection(Language v_language);

	QTranslator *m_translator = nullptr;
	Language m_language = Language::FOLLOW_SYSTEM;
	Language m_effective = Language::ZH_CN;
	bool m_initialized = false;
	// language_changed 发射期间保持为 true：防止槽函数再次调用 set_language
	// 造成递归切换/重入。
	bool m_applying = false;
	// 任务运行期间的翻译器切换延迟（见 begin/end_translation_deferral）。
	int m_deferral_depth = 0;
	bool m_pending_translation = false;
	Language m_pending_language = Language::FOLLOW_SYSTEM;
};

#endif // LANGUAGE_MANAGER_H
