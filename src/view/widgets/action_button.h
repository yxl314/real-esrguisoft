#ifndef ACTION_BUTTON_H
#define ACTION_BUTTON_H

// ============================================================================
// 视图层 - 操作按钮
//
// 三种形态（对应 ui-spec 3.3 / 3.4）：
//   PRIMARY   主操作：橙色实心，一个视图只应有一个；
//   PRIMARY_ICON 主操作图标按钮：橙色实心 + 纯图标（无文字），用于开始/暂停/继续；
//   SECONDARY 次操作：描边；
//   QUIET     弱操作：透明底，hover 才出现浅底；
//   DANGER    危险操作：浅红底 + 危险色文字；
//   ICON / ICON_COMPACT 图标按钮：28×28 透明底，hover 浅底。
//
// 外观全部交给 app.qss.in（objectName=actionButton + 动态属性 variant），
// 图标颜色由 IconProvider 按当前主题染色；主题切换后自动重建图标。
// 禁用态图标由 IconProvider::icon 生成（TERTIARY 实色，避免细描边图标半
// 透明后在浅色底上几乎不可见）；启用态仍是各 variant 的语义色。
// ============================================================================

#include <QPushButton>

#include "icon_provider.h"

class ActionButton : public QPushButton
{
	Q_OBJECT

  public:
	enum class Variant
	{
		PRIMARY,
		PRIMARY_ICON,
		SECONDARY,
		QUIET,
		DANGER,
		ICON,
		ICON_COMPACT
	};

	explicit ActionButton(Variant v_variant, const QString &v_text = QString(),
						  const QString &v_icon_name = QString(),
						  QWidget *v_parent = nullptr);

	void
	set_variant(Variant v_variant);
	Variant
	variant() const;

	// 设置图标（Lucide 名，不含扩展名）；空串表示不要图标
	void
	set_icon_name(const QString &v_icon_name);
	QString
	icon_name() const;

	// 悬停时图标切换成另一种语义色（如关闭按钮：平时灰、悬停红）
	void
	set_hover_tone(IconProvider::Tone v_tone);

	// 常规（32 高）/ 紧凑（28 高）
	void
	set_compact(bool v_compact);
	bool
	is_compact() const;

  protected:
	void
	enterEvent(QEnterEvent *v_event) override;
	void
	leaveEvent(QEvent *v_event) override;

  private:
	void
	refresh_style();
	void
	refresh_icon();
	IconProvider::Tone
	icon_tone() const;

	Variant m_variant = Variant::SECONDARY;
	QString m_icon_name;
	QString m_text; // 记住纯文本，便于悬停时只换图标不丢文字
	bool m_compact = false;
	bool m_hovered = false;
	bool m_has_hover_tone = false;
	IconProvider::Tone m_hover_tone = IconProvider::Tone::DANGER;
};

#endif // ACTION_BUTTON_H
