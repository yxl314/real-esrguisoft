#include "theme_tokens.h"

// ============================================================================
// 视图层 - 设计 Token 数据表
//
// 数值全部来自 docs/design/ccswitch-v4-ui-spec.md（cc-switch v4.0.31）：
//   - 亮色见 §1.1、暗色见 §1.2、阴影见 §1.3、圆角见 §1.4、字号见 §1.5、
//     控件高见 §1.6、间距见 §1.7；
//   - 修改配色只改本文件（QSS 模板与自绘控件都从这里取值）。
// ============================================================================

namespace
{

// 构造亮色主题（ui-spec 1.1 / 1.3 / 1.4 / 1.5 / 1.6 / 1.7）
ThemeTokens
make_light_tokens()
{
	ThemeTokens t;

	// —— 背景 ——
	t.m_bg_app = QColor(QStringLiteral("#FFFFFF"));
	t.m_bg_sidebar = QColor(QStringLiteral("#F5F5F7"));
	t.m_bg_subtle = QColor(QStringLiteral("#EFEFF2"));
	t.m_bg_selected = QColor(QStringLiteral("#E4E4EA"));
	t.m_bg_card = QColor(QStringLiteral("#FFFFFF"));
	t.m_bg_popover = QColor(QStringLiteral("#FFFFFF"));

	// —— 边框 / 文字 ——
	t.m_border_default = QColor(QStringLiteral("#E4E4E9"));
	t.m_border_strong = QColor(QStringLiteral("#CFCFD6"));
	t.m_text_1 = QColor(QStringLiteral("#18181B"));
	t.m_text_2 = QColor(QStringLiteral("#52525B"));
	t.m_text_3 = QColor(QStringLiteral("#65656E"));

	// —— 主操作橙 ——
	// 主操作橙用更深的 #C2410C：白字对比度 5.18:1，满足 WCAG AA
	t.m_action_bg = QColor(QStringLiteral("#C2410C"));
	t.m_action_hover = QColor(QStringLiteral("#9A3412"));
	t.m_action_fg = QColor(QStringLiteral("#FFFFFF"));
	t.m_action_text = QColor(QStringLiteral("#C2410C"));
	t.m_action_soft = QColor(QStringLiteral("#FFF1E6"));

	// —— 反色 / 遮罩 / 开关 / 焦点环 ——
	t.m_inverse_bg = QColor(QStringLiteral("#18181B"));
	t.m_inverse_fg = QColor(QStringLiteral("#FFFFFF"));
	t.m_inverse_hover = QColor(QStringLiteral("#2E2E33"));
	t.m_overlay = QColor(9, 9, 11, 92); // rgba(9,9,11,.36)
	t.m_control_off = QColor(QStringLiteral("#888890"));
	t.m_ring = QColor(QStringLiteral("#F97316"));

	// —— 语义色 ——
	t.m_success = QColor(QStringLiteral("#16A34A"));
	t.m_success_text = QColor(QStringLiteral("#007634"));
	t.m_success_soft = QColor(QStringLiteral("#E8F7EE"));
	t.m_warning = QColor(QStringLiteral("#D97706"));
	t.m_warning_text = QColor(QStringLiteral("#A64A00"));
	t.m_warning_soft = QColor(QStringLiteral("#FEF3C7"));
	t.m_danger = QColor(QStringLiteral("#DC2626"));
	t.m_danger_text = QColor(QStringLiteral("#B91C1C"));
	t.m_danger_soft = QColor(QStringLiteral("#FEE2E2"));

	// —— 模式色 ——
	t.m_direct_fill = QColor(QStringLiteral("#0A84FF"));
	t.m_direct_text = QColor(QStringLiteral("#0063C7"));
	t.m_direct_soft = QColor(QStringLiteral("#EAF3FF"));
	t.m_direct_solid = QColor(QStringLiteral("#0066CC"));
	t.m_route_fill = QColor(QStringLiteral("#0D9488"));
	t.m_route_text = QColor(QStringLiteral("#06726B"));
	t.m_route_soft = QColor(QStringLiteral("#E7F6F4"));
	t.m_route_solid = QColor(QStringLiteral("#0F766E"));
	t.m_stack_fill = QColor(QStringLiteral("#7C3AED"));
	t.m_stack_text = QColor(QStringLiteral("#6D28D9"));
	t.m_stack_soft = QColor(QStringLiteral("#F2ECFE"));
	t.m_stack_solid = QColor(QStringLiteral("#6D28D9"));

	// —— 阴影（ui-spec 1.3 亮色）——
	t.m_shadow_sm.m_color
		= QColor(16, 24, 40, 15); // 0 1px 2px rgba(16,24,40,.06)
	t.m_shadow_sm.m_blur = 2;
	t.m_shadow_sm.m_offset_y = 1;
	t.m_shadow_md.m_color
		= QColor(16, 24, 40, 20); // 0 4px 12px rgba(16,24,40,.08)
	t.m_shadow_md.m_blur = 12;
	t.m_shadow_md.m_offset_y = 4;
	t.m_shadow_lg.m_color
		= QColor(16, 24, 40, 41); // 0 20px 40px rgba(16,24,40,.16)
	t.m_shadow_lg.m_blur = 40;
	t.m_shadow_lg.m_offset_y = 20;

	// —— 尺寸（compact 密度）——
	ThemeMetrics &m = t.m_metrics;
	m.m_radius_control = 6;
	m.m_radius_panel = 10;
	m.m_radius_dialog = 14;
	m.m_radius_nested = 7;
	m.m_h_control = 32;
	m.m_h_control_sm = 28;
	m.m_h_nav_row = 28;
	m.m_h_card_min = 58;
	m.m_card_pad_y = 10;
	m.m_h_page_header = 52;
	m.m_h_segment = 36;
	m.m_h_segment_sm = 28;
	m.m_segment_inset = 3;
	m.m_sidebar_width = 200;
	m.m_sidebar_collapsed = 72;
	m.m_page_padding = 24;
	m.m_page_padding_top = 16;
	m.m_page_padding_bottom = 48;
	m.m_fs_badge = 11;
	m.m_fs_caption = 12;
	m.m_fs_body = 13;
	m.m_fs_strong = 14;
	m.m_fs_section = 15;
	m.m_fs_title = 16;
	m.m_fs_page = 18;
	m.m_fs_metric = 24;
	m.m_sp_1 = 4;
	m.m_sp_2 = 8;
	m.m_sp_3 = 12;
	m.m_sp_4 = 16;
	m.m_sp_5 = 20;
	m.m_sp_6 = 24;
	m.m_icon_sm = 14;
	m.m_icon_md = 16;
	m.m_icon_nav = 18;
	m.m_icon_header = 22;

	return t;
}

// 构造暗色主题（ui-spec 1.2，尺寸与亮色一致）
ThemeTokens
make_dark_tokens()
{
	ThemeTokens t = make_light_tokens();

	t.m_bg_app = QColor(QStringLiteral("#1C1C1E"));
	t.m_bg_sidebar = QColor(QStringLiteral("#161618"));
	t.m_bg_subtle = QColor(QStringLiteral("#26262A"));
	t.m_bg_selected = QColor(QStringLiteral("#313136"));
	t.m_bg_card = QColor(QStringLiteral("#2E2E33"));
	t.m_bg_popover = QColor(QStringLiteral("#2E2E33"));

	t.m_border_default = QColor(QStringLiteral("#3E3E45"));
	t.m_border_strong = QColor(QStringLiteral("#4A4A52"));
	t.m_text_1 = QColor(QStringLiteral("#E4E4E7"));
	t.m_text_2 = QColor(QStringLiteral("#ABABB3"));
	t.m_text_3 = QColor(QStringLiteral("#9A9AA2"));

	t.m_action_bg = QColor(QStringLiteral("#C2410C"));
	t.m_action_hover = QColor(QStringLiteral("#9A3412"));
	t.m_action_fg = QColor(QStringLiteral("#FFFFFF"));
	t.m_action_text = QColor(QStringLiteral("#FB923C"));
	t.m_action_soft = QColor(249, 115, 21, 31); // rgba(249,115,21,.12)

	t.m_inverse_bg = QColor(QStringLiteral("#F4F4F5"));
	t.m_inverse_fg = QColor(QStringLiteral("#18181B"));
	t.m_inverse_hover = QColor(QStringLiteral("#E4E4E7"));
	t.m_overlay = QColor(0, 0, 0, 140); // rgba(0,0,0,.55)
	t.m_control_off = QColor(QStringLiteral("#6F6F77"));
	t.m_ring = QColor(QStringLiteral("#D77A3C"));

	t.m_success = QColor(QStringLiteral("#4ADE80"));
	t.m_success_text = QColor(QStringLiteral("#4ADE80"));
	t.m_success_soft = QColor(QStringLiteral("#0F2B1A"));
	t.m_warning = QColor(QStringLiteral("#FBBF24"));
	t.m_warning_text = QColor(QStringLiteral("#FCD34D"));
	t.m_warning_soft = QColor(QStringLiteral("#33260A"));
	t.m_danger = QColor(QStringLiteral("#F87171"));
	t.m_danger_text = QColor(QStringLiteral("#FCA5A5"));
	t.m_danger_soft = QColor(QStringLiteral("#3A1414"));

	t.m_direct_fill = QColor(QStringLiteral("#4A8FD9"));
	t.m_direct_text = QColor(QStringLiteral("#8FB8E6"));
	t.m_direct_soft = QColor(74, 143, 217, 31);
	t.m_direct_solid = QColor(QStringLiteral("#2F6DB3"));
	t.m_route_fill = QColor(QStringLiteral("#2A9D8F"));
	t.m_route_text = QColor(QStringLiteral("#7CC4BA"));
	t.m_route_soft = QColor(42, 157, 143, 31);
	t.m_route_solid = QColor(QStringLiteral("#1F7D72"));
	t.m_stack_fill = QColor(QStringLiteral("#8B74D6"));
	t.m_stack_text = QColor(QStringLiteral("#B5A6E3"));
	t.m_stack_soft = QColor(139, 116, 214, 31);
	t.m_stack_solid = QColor(QStringLiteral("#6A52BF"));

	// 暗色靠「8% 白描边」分层，阴影本身更黑更深（ui-spec 1.3）
	t.m_shadow_sm.m_color = QColor(0, 0, 0, 102);
	t.m_shadow_sm.m_blur = 2;
	t.m_shadow_sm.m_offset_y = 1;
	t.m_shadow_md.m_color = QColor(0, 0, 0, 115);
	t.m_shadow_md.m_blur = 12;
	t.m_shadow_md.m_offset_y = 4;
	t.m_shadow_lg.m_color = QColor(0, 0, 0, 128);
	t.m_shadow_lg.m_blur = 40;
	t.m_shadow_lg.m_offset_y = 20;

	return t;
}

} // namespace

const ThemeTokens &
light_theme_tokens()
{
	static const ThemeTokens tokens = make_light_tokens();
	return tokens;
}

const ThemeTokens &
dark_theme_tokens()
{
	static const ThemeTokens tokens = make_dark_tokens();
	return tokens;
}

const ThemeTokens &
theme_tokens_for(ThemeAppearance v_appearance)
{
	return (v_appearance == ThemeAppearance::DARK) ? dark_theme_tokens()
												   : light_theme_tokens();
}

QString
theme_color_to_qss(const QColor &v_color)
{
	if (!v_color.isValid())
	{
		return QStringLiteral("transparent");
	}
	if (v_color.alpha() == 255)
	{
		return v_color.name(QColor::HexRgb).toUpper();
	}
	// Qt QSS 支持 rgba(r, g, b, p%) / rgba(r, g, b, a)
	const int percent = qRound(v_color.alphaF() * 100.0);
	return QStringLiteral("rgba(%1, %2, %3, %4%)")
		.arg(v_color.red())
		.arg(v_color.green())
		.arg(v_color.blue())
		.arg(percent);
}

QHash<QString, QColor>
theme_color_map(const ThemeTokens &v_tokens)
{
	QHash<QString, QColor> map;
	map.insert(QStringLiteral("bg.app"), v_tokens.m_bg_app);
	map.insert(QStringLiteral("bg.sidebar"), v_tokens.m_bg_sidebar);
	map.insert(QStringLiteral("bg.subtle"), v_tokens.m_bg_subtle);
	map.insert(QStringLiteral("bg.selected"), v_tokens.m_bg_selected);
	map.insert(QStringLiteral("bg.card"), v_tokens.m_bg_card);
	map.insert(QStringLiteral("bg.popover"), v_tokens.m_bg_popover);

	map.insert(QStringLiteral("border.default"), v_tokens.m_border_default);
	map.insert(QStringLiteral("border.strong"), v_tokens.m_border_strong);

	map.insert(QStringLiteral("text.1"), v_tokens.m_text_1);
	map.insert(QStringLiteral("text.2"), v_tokens.m_text_2);
	map.insert(QStringLiteral("text.3"), v_tokens.m_text_3);

	map.insert(QStringLiteral("action.bg"), v_tokens.m_action_bg);
	map.insert(QStringLiteral("action.hover"), v_tokens.m_action_hover);
	map.insert(QStringLiteral("action.fg"), v_tokens.m_action_fg);
	map.insert(QStringLiteral("action.text"), v_tokens.m_action_text);
	map.insert(QStringLiteral("action.soft"), v_tokens.m_action_soft);

	map.insert(QStringLiteral("inverse.bg"), v_tokens.m_inverse_bg);
	map.insert(QStringLiteral("inverse.fg"), v_tokens.m_inverse_fg);
	map.insert(QStringLiteral("inverse.hover"), v_tokens.m_inverse_hover);
	map.insert(QStringLiteral("overlay"), v_tokens.m_overlay);
	map.insert(QStringLiteral("control.off"), v_tokens.m_control_off);
	map.insert(QStringLiteral("ring"), v_tokens.m_ring);

	map.insert(QStringLiteral("success"), v_tokens.m_success);
	map.insert(QStringLiteral("success.text"), v_tokens.m_success_text);
	map.insert(QStringLiteral("success.soft"), v_tokens.m_success_soft);
	map.insert(QStringLiteral("warning"), v_tokens.m_warning);
	map.insert(QStringLiteral("warning.text"), v_tokens.m_warning_text);
	map.insert(QStringLiteral("warning.soft"), v_tokens.m_warning_soft);
	map.insert(QStringLiteral("danger"), v_tokens.m_danger);
	map.insert(QStringLiteral("danger.text"), v_tokens.m_danger_text);
	map.insert(QStringLiteral("danger.soft"), v_tokens.m_danger_soft);

	map.insert(QStringLiteral("direct.fill"), v_tokens.m_direct_fill);
	map.insert(QStringLiteral("direct.text"), v_tokens.m_direct_text);
	map.insert(QStringLiteral("direct.soft"), v_tokens.m_direct_soft);
	map.insert(QStringLiteral("direct.solid"), v_tokens.m_direct_solid);
	map.insert(QStringLiteral("route.fill"), v_tokens.m_route_fill);
	map.insert(QStringLiteral("route.text"), v_tokens.m_route_text);
	map.insert(QStringLiteral("route.soft"), v_tokens.m_route_soft);
	map.insert(QStringLiteral("route.solid"), v_tokens.m_route_solid);
	map.insert(QStringLiteral("stack.fill"), v_tokens.m_stack_fill);
	map.insert(QStringLiteral("stack.text"), v_tokens.m_stack_text);
	map.insert(QStringLiteral("stack.soft"), v_tokens.m_stack_soft);
	map.insert(QStringLiteral("stack.solid"), v_tokens.m_stack_solid);
	return map;
}

QColor
theme_color_value(const ThemeTokens &v_tokens, const QString &v_name)
{
	return theme_color_map(v_tokens).value(v_name);
}

QHash<QString, QString>
theme_token_map(const ThemeTokens &v_tokens)
{
	QHash<QString, QString> map;

	// —— 颜色：键名与 ui-spec 一一对应（全部走 theme_color_map，避免两处维护）
	const QHash<QString, QColor> colors = theme_color_map(v_tokens);
	for (auto it = colors.constBegin(); it != colors.constEnd(); ++it)
	{
		map.insert(it.key(), theme_color_to_qss(it.value()));
	}

	// —— 尺寸（QSS 里写 {{radius.control}}px 这种形式）——
	const ThemeMetrics &m = v_tokens.m_metrics;
	map.insert(QStringLiteral("radius.control"),
			   QString::number(m.m_radius_control));
	map.insert(QStringLiteral("radius.panel"),
			   QString::number(m.m_radius_panel));
	map.insert(QStringLiteral("radius.dialog"),
			   QString::number(m.m_radius_dialog));
	map.insert(QStringLiteral("radius.nested"),
			   QString::number(m.m_radius_nested));

	map.insert(QStringLiteral("h.control"), QString::number(m.m_h_control));
	map.insert(QStringLiteral("h.control.sm"),
			   QString::number(m.m_h_control_sm));
	map.insert(QStringLiteral("h.nav.row"), QString::number(m.m_h_nav_row));
	map.insert(QStringLiteral("h.card.min"), QString::number(m.m_h_card_min));
	map.insert(QStringLiteral("card.pad.y"), QString::number(m.m_card_pad_y));
	map.insert(QStringLiteral("h.page.header"),
			   QString::number(m.m_h_page_header));
	map.insert(QStringLiteral("h.segment"), QString::number(m.m_h_segment));
	map.insert(QStringLiteral("h.segment.sm"),
			   QString::number(m.m_h_segment_sm));
	map.insert(QStringLiteral("segment.inset"),
			   QString::number(m.m_segment_inset));

	map.insert(QStringLiteral("sidebar.width"),
			   QString::number(m.m_sidebar_width));
	map.insert(QStringLiteral("sidebar.collapsed"),
			   QString::number(m.m_sidebar_collapsed));
	map.insert(QStringLiteral("page.padding"),
			   QString::number(m.m_page_padding));
	map.insert(QStringLiteral("page.padding.top"),
			   QString::number(m.m_page_padding_top));
	map.insert(QStringLiteral("page.padding.bottom"),
			   QString::number(m.m_page_padding_bottom));

	map.insert(QStringLiteral("fs.badge"), QString::number(m.m_fs_badge));
	map.insert(QStringLiteral("fs.caption"), QString::number(m.m_fs_caption));
	map.insert(QStringLiteral("fs.body"), QString::number(m.m_fs_body));
	map.insert(QStringLiteral("fs.strong"), QString::number(m.m_fs_strong));
	map.insert(QStringLiteral("fs.section"), QString::number(m.m_fs_section));
	map.insert(QStringLiteral("fs.title"), QString::number(m.m_fs_title));
	map.insert(QStringLiteral("fs.page"), QString::number(m.m_fs_page));
	map.insert(QStringLiteral("fs.metric"), QString::number(m.m_fs_metric));

	map.insert(QStringLiteral("sp.1"), QString::number(m.m_sp_1));
	map.insert(QStringLiteral("sp.2"), QString::number(m.m_sp_2));
	map.insert(QStringLiteral("sp.3"), QString::number(m.m_sp_3));
	map.insert(QStringLiteral("sp.4"), QString::number(m.m_sp_4));
	map.insert(QStringLiteral("sp.5"), QString::number(m.m_sp_5));
	map.insert(QStringLiteral("sp.6"), QString::number(m.m_sp_6));

	map.insert(QStringLiteral("icon.sm"), QString::number(m.m_icon_sm));
	map.insert(QStringLiteral("icon.md"), QString::number(m.m_icon_md));
	map.insert(QStringLiteral("icon.nav"), QString::number(m.m_icon_nav));
	map.insert(QStringLiteral("icon.header"), QString::number(m.m_icon_header));

	return map;
}

QString
theme_token_value(const ThemeTokens &v_tokens, const QString &v_name)
{
	return theme_token_map(v_tokens).value(v_name);
}
