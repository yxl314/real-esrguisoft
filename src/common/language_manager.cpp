#include "language_manager.h"

#include "logger.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTranslator>

namespace
{

// QSettings 键：界面语言选择（值 zh / en / system）。
const char *const K_SETTINGS_KEY = "ui/language";

// 英文翻译资源（qrc 嵌入；缺失时回退中文源串）。
const char *const K_EN_TRANSLATOR_PATH = ":/i18n/app_en.qm";

// 启动覆盖钩子：仅影响本次运行，不写 QSettings。
const char *const K_LANGUAGE_ENV = "RESR_UI_LANG";

QSettings
make_settings()
{
	// 使用 QCoreApplication 的身份（main.cpp 统一设置 org/app）；测试可在
	// initTestCase 中通过 setDefaultFormat + setPath 隔离到临时目录。
	return QSettings();
}

} // namespace

LanguageManager::LanguageManager(QObject *v_parent) : QObject(v_parent) {}

LanguageManager::~LanguageManager()
{
	if (m_translator != nullptr)
	{
		// QSettings 是静态单例，析构可能晚于 QApplication（此时 removeTranslator
		// 会打印 "Please instantiate the QApplication object first" 噪音告警）。
		// 应用已销毁时 translator 随本对象一并 delete 即可，无需再 remove。
		if (QCoreApplication::instance() != nullptr)
		{
			QCoreApplication::removeTranslator(m_translator);
		}
		delete m_translator;
		m_translator = nullptr;
	}
}

LanguageManager &
LanguageManager::instance()
{
	static LanguageManager manager;
	return manager;
}

QString
LanguageManager::settings_key()
{
	return QString::fromLatin1(K_SETTINGS_KEY);
}

QString
LanguageManager::to_settings_value(Language v_language)
{
	switch (v_language)
	{
		case Language::EN:
			return QStringLiteral("en");
		case Language::FOLLOW_SYSTEM:
			return QStringLiteral("system");
		case Language::ZH_CN:
		default:
			return QStringLiteral("zh");
	}
}

LanguageManager::Language
LanguageManager::from_settings_value(const QString &v_value, bool *v_ok)
{
	const QString value = v_value.trimmed().toLower();
	if (value == QStringLiteral("zh"))
	{
		if (v_ok != nullptr)
		{
			*v_ok = true;
		}
		return Language::ZH_CN;
	}
	if (value == QStringLiteral("en"))
	{
		if (v_ok != nullptr)
		{
			*v_ok = true;
		}
		return Language::EN;
	}
	if (value == QStringLiteral("system"))
	{
		if (v_ok != nullptr)
		{
			*v_ok = true;
		}
		return Language::FOLLOW_SYSTEM;
	}
	if (v_ok != nullptr)
	{
		*v_ok = false;
	}
	return Language::FOLLOW_SYSTEM;
}

LanguageManager::Language
LanguageManager::system_language(const QLocale &v_system_locale)
{
	return (v_system_locale.language() == QLocale::Chinese) ? Language::ZH_CN
															: Language::EN;
}

LanguageManager::Language
LanguageManager::resolve_effective(Language v_requested,
								   bool v_translation_available,
								   QLocale v_system_locale)
{
	Language effective = v_requested;
	if (effective == Language::FOLLOW_SYSTEM)
	{
		effective = system_language(v_system_locale);
	}
	// 英文资源不可用（.qm 缺失/损坏）时回退源语言中文，保证界面不出现空串。
	if (effective == Language::EN && !v_translation_available)
	{
		effective = Language::ZH_CN;
	}
	return effective;
}

QString
LanguageManager::translator_path(Language v_effective_language)
{
	if (v_effective_language == Language::EN)
	{
		return QString::fromLatin1(K_EN_TRANSLATOR_PATH);
	}
	return QString();
}

void
LanguageManager::initialize()
{
	if (m_initialized)
	{
		return;
	}

	Language requested = Language::FOLLOW_SYSTEM;
	bool from_environment = false;

	// 1) 启动覆盖：RESR_UI_LANG=system|zh|en（大小写不敏感，仅本次生效）。
	const QString env_value
		= qEnvironmentVariable(K_LANGUAGE_ENV).trimmed().toLower();
	if (!env_value.isEmpty())
	{
		if (env_value == QStringLiteral("zh"))
		{
			requested = Language::ZH_CN;
			from_environment = true;
		}
		else if (env_value == QStringLiteral("en"))
		{
			requested = Language::EN;
			from_environment = true;
		}
		else if (env_value == QStringLiteral("system"))
		{
			requested = Language::FOLLOW_SYSTEM;
			from_environment = true;
		}
		else
		{
			// 日志不翻译（保持英文）；非法值按未设置处理。
			LOG_WARNING(
				QStringLiteral("[LanguageManager] Invalid RESR_UI_LANG value "
							   "ignored: %1")
					.arg(env_value));
		}
	}

	// 2) 否则读 QSettings（缺失/非法 → 跟随系统）。
	if (!from_environment)
	{
		QSettings settings = make_settings();
		const QString stored
			= settings.value(settings_key()).toString().trimmed();
		if (!stored.isEmpty())
		{
			bool ok = false;
			requested = from_settings_value(stored, &ok);
			if (!ok)
			{
				LOG_WARNING(
					QStringLiteral("[LanguageManager] Invalid ui/language "
								   "setting ignored: %1")
						.arg(stored));
			}
		}
	}

	m_language = requested;

	// 3) 解析生效语言并安装 translator（英文资源不可用时回退中文）。
	const Language tentative = resolve_effective(requested, true);
	Language effective = tentative;
	if (tentative == Language::EN && !apply_translator(Language::EN))
	{
		effective = Language::ZH_CN;
	}
	else if (tentative != Language::EN)
	{
		apply_translator(Language::ZH_CN);
	}
	m_effective = effective;
	m_initialized = true;

	// 日志保持英文、不参与翻译；from_environment 仅用于诊断。
	LOG_INFO(QStringLiteral("[LanguageManager] initialized: requested=%1 "
							"effective=%2 source=%3")
				 .arg(to_settings_value(m_language),
					  to_settings_value(m_effective),
					  from_environment ? QStringLiteral("environment")
									   : QStringLiteral("settings")));
}

LanguageManager::Language
LanguageManager::language() const
{
	return m_language;
}

LanguageManager::Language
LanguageManager::effective_language() const
{
	return m_effective;
}

bool
LanguageManager::set_language(Language v_language)
{
	// 同一选择不重复写盘/发信号；m_applying 覆盖 language_changed 发射期间的
	// 重入调用（槽函数若再次切换会被拒绝，避免递归切换 / 重复广播）。
	if (v_language == m_language || m_applying)
	{
		return false;
	}

	// 先持久化用户选择（即使 translator 加载失败也保存意图）。
	QSettings settings = make_settings();
	settings.setValue(settings_key(), to_settings_value(v_language));
	settings.sync();

	m_language = v_language;
	m_initialized = true;

	// 后台线程（视频 worker）运行期间不能装卸 translator：只记录待应用，
	// 等 end_translation_deferral() 在主线程统一应用并广播。
	if (m_deferral_depth > 0)
	{
		m_pending_translation = true;
		m_pending_language = v_language;
		return false;
	}

	return apply_selection(v_language);
}

bool
LanguageManager::apply_selection(Language v_language)
{
	const Language previous_effective = m_effective;

	// 整个「换 translator + 广播」期间保持重入保护：language_changed 的槽
	// 可能再次调用 set_language()，此时必须被拒绝而不是递归切换。
	m_applying = true;

	const Language tentative = resolve_effective(v_language, true);
	Language effective = tentative;
	if (tentative == Language::EN)
	{
		if (!apply_translator(Language::EN))
		{
			effective = Language::ZH_CN;
		}
	}
	else
	{
		apply_translator(Language::ZH_CN);
	}
	m_effective = effective;

	// 视图先 retranslate、控制器后重下发动态状态（连接顺序见 main/initialize）。
	emit language_changed();

	m_applying = false;
	return m_effective != previous_effective;
}

void
LanguageManager::begin_translation_deferral()
{
	++instance().m_deferral_depth;
}

void
LanguageManager::end_translation_deferral(bool v_apply_pending)
{
	LanguageManager &manager = instance();
	if (manager.m_deferral_depth > 0)
	{
		--manager.m_deferral_depth;
	}
	if (manager.m_deferral_depth > 0 || !manager.m_pending_translation)
	{
		return;
	}
	manager.m_pending_translation = false;

	// 退出清理：QApplication 已/正在销毁，丢弃待应用切换、不广播。
	if (!v_apply_pending || QCoreApplication::instance() == nullptr)
	{
		return;
	}

	manager.apply_selection(manager.m_pending_language);
}

bool
LanguageManager::apply_translator(Language v_effective_language)
{
	// 先卸载旧 translator；Qt 会向所有 widget 投递 LanguageChange 事件，
	// 本项目不依赖该事件做重译（显式 retranslate），不影响行为。
	if (m_translator != nullptr)
	{
		QCoreApplication::removeTranslator(m_translator);
		delete m_translator;
		m_translator = nullptr;
	}

	if (v_effective_language != Language::EN)
	{
		return true;
	}

	const QString path = translator_path(Language::EN);
	QTranslator *translator = new QTranslator(this);
	if (!translator->load(path))
	{
		LOG_WARNING(
			QStringLiteral("[LanguageManager] Failed to load translator %1; "
						   "falling back to Chinese")
				.arg(path));
		delete translator;
		return false;
	}
	if (!QCoreApplication::installTranslator(translator))
	{
		LOG_WARNING(
			QStringLiteral(
				"[LanguageManager] Failed to install translator %1; falling back "
				"to Chinese")
				.arg(path));
		delete translator;
		return false;
	}
	m_translator = translator;
	return true;
}
