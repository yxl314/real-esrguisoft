#ifndef ICON_PROVIDER_H
#define ICON_PROVIDER_H

// ============================================================================
// 视图层 - 图标提供者
//
// 图标来源：Lucide（ISC 协议），已转成「白色描边 + 透明底」的 PNG 放进
// resources/icons/。运行时按需要染成当前主题的颜色（QIcon 本身不能上色），
// 这样一份资源就能服务亮色/暗色、主色/次色、悬停/禁用等所有场景。
//
// 用法：
//     button->setIcon(IconProvider::icon("folder-open", IconProvider::Tone::PRIMARY, 16));
//     label->setPixmap(IconProvider::pixmap("image", color, 22));
// ============================================================================

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QString>

class IconProvider
{
  public:
	// 语义色（自动跟随当前主题）
	enum class Tone
	{
		PRIMARY,   // 主文字色（text.1）
		SECONDARY, // 次文字色（text.2，默认图标色）
		TERTIARY,  // 三级文字色（text.3）
		ON_ACCENT, // 主操作橙上的前景（白）
		ACCENT,	   // 主操作橙 / 链接色
		DANGER,	   // 危险色文字
		SUCCESS,   // 成功色
		WARNING	   // 警告色
	};

	// 按钮图标（启用/悬停 + 禁用两态）：Normal 用语义色，Disabled 用
	// TERTIARY 实色。细描边图标若只把颜色做半透明降级，在浅色底上几乎不可
	// 见，所以禁用态用与禁用文字一致的实色。logical_size 是逻辑像素
	// （16/18/20/22/28…）。
	static QIcon
	icon(const QString &v_name, Tone v_tone, int v_logical_size);

	// 取单位色位图（label->setPixmap / 自绘用）
	static QPixmap
	pixmap(const QString &v_name, const QColor &v_color, int v_logical_size);

	// 语义色 → 当前主题颜色
	static QColor
	tone_color(Tone v_tone);

  private:
	static QPixmap
	tinted_pixmap(const QString &v_name, const QColor &v_color);

	// 缓存键：name|#RRGGBBAA
	static QHash<QString, QPixmap> s_tinted_cache;
};

#endif // ICON_PROVIDER_H
