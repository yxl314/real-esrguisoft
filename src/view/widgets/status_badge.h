#ifndef STATUS_BADGE_H
#define STATUS_BADGE_H

// ============================================================================
// 视图层 - 状态徽章（18 高全圆角胶囊）
//
// 对应 cc-switch 卡片内的「官方 / 需要路由 / 使用中」小胶囊。
// 颜色通过动态属性 tone 交给 app.qss.in 控制，不在这里写死色值。
// ============================================================================

#include <QLabel>

class StatusBadge : public QLabel
{
	Q_OBJECT

  public:
	// 语义色系
	enum class Tone
	{
		NEUTRAL, // 浅底 + 默认描边（普通标签）
		OUTLINE, // 白底 + 强描边（「官方」这类中性徽章）
		SUCCESS, // 成功
		WARNING, // 警告 / 将尽
		DANGER,	 // 失败 / 删除
		INFO,	 // 直连模式蓝
		ACCENT,	 // 主操作橙
		ROUTE	 // 路由模式青
	};

	explicit StatusBadge(const QString &v_text = QString(),
						 Tone v_tone = Tone::NEUTRAL,
						 QWidget *v_parent = nullptr);

	void
	set_tone(Tone v_tone);
	Tone
	tone() const;

  private:
	static QString
	tone_name(Tone v_tone);
	void
	refresh_style();

	Tone m_tone = Tone::NEUTRAL;
};

#endif // STATUS_BADGE_H
