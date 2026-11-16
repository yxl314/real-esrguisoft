#include "theme_manager.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

namespace
{
// QSettings 键名：UI 主题选择
const char *const SETTINGS_KEY = "ui/theme_mode";
// QSS 模板资源路径
const char *const APP_QSS_TEMPLATE = ":/theme/app.qss.in";
const char *const PANELS_QSS_TEMPLATE = ":/theme/panels.qss.in";
// QSS 里需要按主题切换颜色的位图（QSS 的 image 无法上色，只能准备两份）
const char *const CHEVRON_LIGHT = ":/icons/chevron-down-light.png";
const char *const CHEVRON_DARK = ":/icons/chevron-down-dark.png";

QSettings
make_settings()
{
	// 使用 main.cpp 统一设置的 QCoreApplication 组织/应用身份。
	return QSettings();
}
} // namespace

ThemeManager &
ThemeManager::instance()
{
	static ThemeManager manager;
	return manager;
}

ThemeManager::ThemeManager(QObject *v_parent) : QObject(v_parent)
{
	load_mode_from_settings();
	m_appearance = resolve_appearance();
}

const char *
ThemeManager::settings_key()
{
	return SETTINGS_KEY;
}

void
ThemeManager::initialize()
{
	// 自检 / 截图钩子：环境变量 RESR_UI_THEME=light|dark|follow 可强制本次启动的
	// 主题（不写回 QSettings），便于自动化截图与视觉回归。
	const QString forced_theme
		= qEnvironmentVariable("RESR_UI_THEME").trimmed().toLower();
	if (forced_theme == QStringLiteral("light"))
	{
		m_mode = Mode::LIGHT;
	}
	else if (forced_theme == QStringLiteral("dark"))
	{
		m_mode = Mode::DARK;
	}
	else if (forced_theme == QStringLiteral("follow")
			 || forced_theme == QStringLiteral("system"))
	{
		m_mode = Mode::FOLLOW_SYSTEM;
	}

	// 字体栈（ui-spec 1.5）：优先中文 UI 字体，Qt 会自动回退到系统字体
	QFont font(QStringLiteral("Microsoft YaHei UI"));
	if (!QFontInfo(font).exactMatch())
	{
		font = QFont(QStringLiteral("Microsoft YaHei"));
	}
	font.setPixelSize(light_theme_tokens().m_metrics.m_fs_body);
	font.setStyleStrategy(QFont::PreferAntialias);
	QApplication::setFont(font);

	// 跟随系统：QStyleHints::colorSchemeChanged（Qt 6.5+）
	if (QGuiApplication::styleHints() != nullptr)
	{
		connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
				this,
				[this](Qt::ColorScheme)
				{
					if (m_mode != Mode::FOLLOW_SYSTEM)
					{
						return;
					}
					const ThemeAppearance next = resolve_appearance();
					if (next != m_appearance)
					{
						apply();
					}
				});
	}

	apply();
}

ThemeManager::Mode
ThemeManager::mode() const
{
	return m_mode;
}

ThemeAppearance
ThemeManager::appearance() const
{
	return m_appearance;
}

bool
ThemeManager::is_dark() const
{
	return m_appearance == ThemeAppearance::DARK;
}

const ThemeTokens &
ThemeManager::tokens() const
{
	return theme_tokens_for(m_appearance);
}

void
ThemeManager::set_mode(Mode v_mode)
{
	if (v_mode == m_mode)
	{
		return;
	}
	m_mode = v_mode;
	save_mode_to_settings();

	const ThemeAppearance next = resolve_appearance();
	if (next != m_appearance)
	{
		apply();
		return;
	}
	// 模式选择变了但解析后的外观相同（如 跟随系统(浅色) -> 浅色）时，
	// apply() 不会执行；仍要通知视图同步侧栏主题标签 / 设置页分段控件的
	// 选中态（它们监听 theme_changed），否则界面停留旧模式。
	emit theme_changed();
}

void
ThemeManager::cycle_mode()
{
	switch (m_mode)
	{
		case Mode::FOLLOW_SYSTEM:
			set_mode(Mode::LIGHT);
			break;
		case Mode::LIGHT:
			set_mode(Mode::DARK);
			break;
		case Mode::DARK:
			set_mode(Mode::FOLLOW_SYSTEM);
			break;
	}
}

QString
ThemeManager::token(const QString &v_name) const
{
	if (v_name == QStringLiteral("qss.chevron"))
	{
		return is_dark() ? QStringLiteral("url(%1)").arg(CHEVRON_DARK)
						 : QStringLiteral("url(%1)").arg(CHEVRON_LIGHT);
	}
	return theme_token_value(tokens(), v_name);
}

QColor
ThemeManager::color(const QString &v_name) const
{
	return theme_color_value(tokens(), v_name);
}

ThemeAppearance
ThemeManager::resolve_appearance() const
{
	if (m_mode == Mode::LIGHT)
	{
		return ThemeAppearance::LIGHT;
	}
	if (m_mode == Mode::DARK)
	{
		return ThemeAppearance::DARK;
	}

	// 跟随系统：Qt 6.5+ 的 colorScheme()，未知时按浅色处理
	if (QGuiApplication::styleHints() != nullptr
		&& QGuiApplication::styleHints()->colorScheme()
			   == Qt::ColorScheme::Dark)
	{
		return ThemeAppearance::DARK;
	}
	return ThemeAppearance::LIGHT;
}

void
ThemeManager::apply()
{
	m_appearance = resolve_appearance();

	const ThemeTokens &t = tokens();
	QHash<QString, QString> map = theme_token_map(t);
	// 位图类 token（两份 PNG 按主题挑选）
	map.insert(QStringLiteral("qss.chevron"),
			   is_dark() ? QStringLiteral("url(%1)").arg(CHEVRON_DARK)
						 : QStringLiteral("url(%1)").arg(CHEVRON_LIGHT));

	QString style_sheet;

	QFile app_template(QString::fromLatin1(APP_QSS_TEMPLATE));
	if (app_template.open(QFile::ReadOnly | QFile::Text))
	{
		style_sheet
			= render_template(QString::fromUtf8(app_template.readAll()), map);
		app_template.close();
	}
	else
	{
		qWarning() << "[ThemeManager] 缺少应用样式模板" << APP_QSS_TEMPLATE;
	}

	// 面板私有样式：由面板侧维护，缺失时静默跳过
	QFile panels_template(QString::fromLatin1(PANELS_QSS_TEMPLATE));
	if (panels_template.open(QFile::ReadOnly | QFile::Text))
	{
		style_sheet += QStringLiteral("\n");
		style_sheet += render_template(
			QString::fromUtf8(panels_template.readAll()), map);
		panels_template.close();
	}

	m_style_sheet = style_sheet;
	if (qApp != nullptr)
	{
		qApp->setStyleSheet(m_style_sheet);
	}
	emit theme_changed();
}

QString
ThemeManager::current_style_sheet() const
{
	return m_style_sheet;
}

QString
ThemeManager::render_template(const QString &v_template,
							  const QHash<QString, QString> &v_tokens) const
{
	QString result = v_template;
	for (auto it = v_tokens.constBegin(); it != v_tokens.constEnd(); ++it)
	{
		result.replace(QStringLiteral("{{%1}}").arg(it.key()), it.value());
	}

	// 未替换的占位说明模板拼写错误，打印一次便于排查（不影响运行）
	const int leftover = result.indexOf(QStringLiteral("{{"));
	if (leftover >= 0)
	{
		qWarning() << "[ThemeManager] QSS 模板存在未解析占位符:"
				   << result.mid(leftover, 40);
	}
	return result;
}

void
ThemeManager::load_mode_from_settings()
{
	QSettings settings = make_settings();
	const int raw
		= settings.value(QString::fromLatin1(SETTINGS_KEY), 0).toInt();
	switch (raw)
	{
		case static_cast<int>(Mode::LIGHT):
			m_mode = Mode::LIGHT;
			break;
		case static_cast<int>(Mode::DARK):
			m_mode = Mode::DARK;
			break;
		default:
			m_mode = Mode::FOLLOW_SYSTEM;
			break;
	}
}

void
ThemeManager::save_mode_to_settings() const
{
	QSettings settings = make_settings();
	settings.setValue(QString::fromLatin1(SETTINGS_KEY),
					  static_cast<int>(m_mode));
}
