#ifndef THEME_MANAGER_H
#define THEME_MANAGER_H

// ============================================================================
// 视图层 - 主题管理器（单例）
//
// 职责：
//   1. 维护「跟随系统 / 浅色 / 深色」三态主题选择，QSettings 持久化；
//   2. 把 resources/theme/*.qss.in 模板里的 {{token}} 占位替换成当前主题的
//      实际值（Qt QSS 没有 CSS 变量，只能启动 / 切换时做文本替换）；
//   3. 替换结果一次性 qApp->setStyleSheet()，并通过 theme_changed 通知自绘
//      控件（分段控件、开关、侧栏、卡片……）重绘 / 重建图标；
//   4. 同时加载 panels.qss.in（面板私有样式，由面板侧维护；文件不存在时静默跳过）。
//
// 用法：
//     ThemeManager::instance().initialize();            // main / 主窗口里调用一次
//     ThemeManager::instance().set_mode(ThemeManager::Mode::DARK);
//     connect(&ThemeManager::instance(), &ThemeManager::theme_changed, ...);
// ============================================================================

#include <QObject>
#include <QString>

#include "theme_tokens.h"

class QWidget;

class ThemeManager : public QObject
{
	Q_OBJECT

  public:
	// 主题模式：跟随系统 / 强制浅色 / 强制深色
	enum class Mode
	{
		FOLLOW_SYSTEM = 0,
		LIGHT = 1,
		DARK = 2
	};
	Q_ENUM(Mode)

	static ThemeManager &
	instance();

	// 初始化：设置应用字体、读取上次选择、连接系统主题变化、应用样式。
	// 重复调用是安全的（幂等）。
	void
	initialize();

	// 当前选择（跟随系统 / 浅色 / 深色）
	Mode
	mode() const;

	// 实际生效的外观（跟随系统时按系统算出来）
	ThemeAppearance
	appearance() const;
	bool
	is_dark() const;

	// 修改选择：立即生效并写 QSettings
	void
	set_mode(Mode v_mode);

	// 在「跟随系统 -> 浅色 -> 深色」之间循环（侧栏底部按钮用）
	void
	cycle_mode();

	// 当前主题的 token
	const ThemeTokens &
	tokens() const;

	// 取单个 token 的 QSS 字符串值（如 "text.2"）
	QString
	token(const QString &v_name) const;

	// 取单个颜色 token（如 "bg.card"）
	QColor
	color(const QString &v_name) const;

	// 重新生成并应用样式表（主题变化 / 模板热更新时调用）
	void
	apply();

	// 供测试 / 诊断：当前生效的完整 QSS 文本
	QString
	current_style_sheet() const;

	// 常量：QSettings 键名
	static const char *
	settings_key();

  signals:
	// 主题已切换（样式表已重新应用），自绘控件应 update() / 重建图标
	void
	theme_changed();

  private:
	explicit ThemeManager(QObject *v_parent = nullptr);

	void
	load_mode_from_settings();
	void
	save_mode_to_settings() const;
	ThemeAppearance
	resolve_appearance() const;
	QString
	render_template(const QString &v_template,
					const QHash<QString, QString> &v_tokens) const;

	Mode m_mode = Mode::FOLLOW_SYSTEM;
	ThemeAppearance m_appearance = ThemeAppearance::LIGHT;
	QString m_style_sheet;
};

#endif // THEME_MANAGER_H
