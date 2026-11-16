#ifndef TOGGLE_SWITCH_H
#define TOGGLE_SWITCH_H

// ============================================================================
// 视图层 - 开关（自绘：30×18 胶囊轨道 + 14 圆点，150ms 过渡）
//
// 对应 ui-spec 3.8。继承 QAbstractButton（checkable），可直接 connect
// &QAbstractButton::toggled。tone=neutral 用于不抢眼的列表场景。
// ============================================================================

#include <QAbstractButton>

class QVariantAnimation;

class ToggleSwitch : public QAbstractButton
{
	Q_OBJECT

  public:
	explicit ToggleSwitch(QWidget *v_parent = nullptr);

	// 中性色调（开态轨道用次级文字色而非主操作橙）
	void
	set_neutral_tone(bool v_neutral);
	bool
	neutral_tone() const;

	QSize
	sizeHint() const override;
	QSize
	minimumSizeHint() const override;

  protected:
	void
	paintEvent(QPaintEvent *v_event) override;

  private:
	void
	animate_to(bool v_checked);

	QVariantAnimation *m_animation = nullptr;
	qreal m_position = 0.0; // 0=关，1=开
	bool m_neutral = false;
};

#endif // TOGGLE_SWITCH_H
