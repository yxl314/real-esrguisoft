#ifndef THEME_TOKENS_H
#define THEME_TOKENS_H

// ============================================================================
// 视图层 - 设计 Token（视觉语言的唯一数据源）
//
// 本文件把 docs/design/ccswitch-v4-ui-spec.md 里的设计 token 落成 C++ 常量：
//   - 颜色：背景 / 边框 / 文字 / 主操作橙 / 语义色 / 模式色；
//   - 尺寸：圆角、控件高、字号线、间距刻度、侧栏宽度；
//   - 阴影：QSS 无 box-shadow，自绘控件（卡片、浮层）从这里取参数。
//
// 约定：
//   1. 亮 / 暗两套 token 只在 theme_tokens.cpp 里定义，任何控件不得硬编码色值；
//   2. QSS 模板用 {{token.name}} 占位（见 resources/theme/app.qss.in），
//      占位名由 theme_token_map() 生成，键名与本结构体字段一一对应；
//   3. 字体大小单位为逻辑像素（Qt6 高 DPI 下自动缩放）。
// ============================================================================

#include <QColor>
#include <QHash>
#include <QString>

// 当前生效的外观（亮 / 暗）
enum class ThemeAppearance
{
	LIGHT,
	DARK
};

// 阴影参数（自绘控件用；QSS 无法表达阴影）
struct ThemeShadow
{
	QColor m_color;		// 阴影颜色（含 alpha）
	int m_blur = 2;		// 模糊半径（px）
	int m_offset_y = 1; // 垂直偏移（px）
};

// 尺寸类 token
struct ThemeMetrics
{
	// 圆角
	int m_radius_control = 6; // 按钮 / 输入框 / 侧栏行
	int m_radius_panel = 10;  // 卡片 / 分段轨道 / 通知条
	int m_radius_dialog = 14; // 对话框 / 抽屉
	int m_radius_nested = 7;  // 分段选中块（外圆角 10 - 内衬 3）

	// 控件高度
	int m_h_control = 32;	  // 常规按钮 / 输入框 / 下拉框
	int m_h_control_sm = 28;  // 紧凑按钮 / 图标按钮
	int m_h_nav_row = 28;	  // 侧栏导航行
	int m_h_card_min = 58;	  // 卡片最小高
	int m_card_pad_y = 10;	  // 卡片上下内边距
	int m_h_page_header = 52; // 页头高（含 1px 底边框）
	int m_h_segment = 36;	  // 分段控件轨道高
	int m_h_segment_sm = 28;  // 小号分段控件轨道高
	int m_segment_inset = 3;  // 分段控件内衬

	// 布局尺寸
	int m_sidebar_width = 200;		// 侧栏展开宽
	int m_sidebar_collapsed = 72;	// 侧栏收起宽
	int m_page_padding = 24;		// 页面左右留白
	int m_page_padding_top = 16;	// 页面顶部留白
	int m_page_padding_bottom = 48; // 页面底部留白

	// 字号阶梯（与行高一一对应，行高只写注释，Qt 用字体度量）
	int m_fs_badge = 11;   // 11/16
	int m_fs_caption = 12; // 12/18
	int m_fs_body = 13;	   // 13/20
	int m_fs_strong = 14;  // 14/20
	int m_fs_section = 15; // 15/22
	int m_fs_title = 16;   // 16/24
	int m_fs_page = 18;	   // 18/26
	int m_fs_metric = 24;  // 24/32

	// 间距刻度（4px 基准）
	int m_sp_1 = 4;
	int m_sp_2 = 8;
	int m_sp_3 = 12;
	int m_sp_4 = 16;
	int m_sp_5 = 20;
	int m_sp_6 = 24;

	// 图标尺寸
	int m_icon_sm = 14;
	int m_icon_md = 16;
	int m_icon_nav = 18;
	int m_icon_header = 22;
};

// 一套完整主题（颜色 + 阴影 + 尺寸）
struct ThemeTokens
{
	// ---- 背景 ----
	QColor m_bg_app;	  // 内容区底
	QColor m_bg_sidebar;  // 侧边栏底
	QColor m_bg_subtle;	  // hover 底 / 分段轨道 / 图标框底
	QColor m_bg_selected; // 侧栏当前项 / 选中行
	QColor m_bg_card;	  // 卡片、输入框、弹层
	QColor m_bg_popover;  // 菜单 / 下拉弹层底

	// ---- 边框 ----
	QColor m_border_default; // 默认描边、分隔线
	QColor m_border_strong;	 // 描边按钮、输入框、hover 卡片

	// ---- 文字 ----
	QColor m_text_1; // 主文字
	QColor m_text_2; // 次文字、图标
	QColor m_text_3; // 三级文字、占位、辅助说明

	// ---- 主操作（橙）----
	QColor m_action_bg;
	QColor m_action_hover;
	QColor m_action_fg;
	QColor m_action_text;
	QColor m_action_soft;

	// ---- 反色块 / 遮罩 / 开关关态 / 焦点环 ----
	QColor m_inverse_bg;
	QColor m_inverse_fg;
	QColor m_inverse_hover;
	QColor m_overlay;
	QColor m_control_off;
	QColor m_ring;

	// ---- 语义色 ----
	QColor m_success;
	QColor m_success_text;
	QColor m_success_soft;
	QColor m_warning;
	QColor m_warning_text;
	QColor m_warning_soft;
	QColor m_danger;
	QColor m_danger_text;
	QColor m_danger_soft;

	// ---- 模式色（直连 / 路由 / 聚合，复刻 cc-switch 语义）----
	QColor m_direct_fill;
	QColor m_direct_text;
	QColor m_direct_soft;
	QColor m_direct_solid;
	QColor m_route_fill;
	QColor m_route_text;
	QColor m_route_soft;
	QColor m_route_solid;
	QColor m_stack_fill;
	QColor m_stack_text;
	QColor m_stack_soft;
	QColor m_stack_solid;

	// ---- 阴影 ----
	ThemeShadow m_shadow_sm;
	ThemeShadow m_shadow_md;
	ThemeShadow m_shadow_lg;

	// ---- 尺寸 ----
	ThemeMetrics m_metrics;
};

// 亮色 token（对应 ui-spec 1.1）
const ThemeTokens &
light_theme_tokens();

// 暗色 token（对应 ui-spec 1.2）
const ThemeTokens &
dark_theme_tokens();

// 按外观取 token
const ThemeTokens &
theme_tokens_for(ThemeAppearance v_appearance);

// 颜色 token 表：键名（bg.app / text.1 / action.bg / ...）→ QColor。
// 自绘控件取色与 QSS 字符串生成共用同一份键名，避免两处维护。
QHash<QString, QColor>
theme_color_map(const ThemeTokens &v_tokens);

// 单个颜色 token（找不到返回无效 QColor）
QColor
theme_color_value(const ThemeTokens &v_tokens, const QString &v_name);

// 生成 QSS 模板替换表：键名 → 字符串值。
// 键名规则（与 ui-spec 1.x 表一一对应）：bg.app / border.default /
// text.1 / action.bg / radius.control / h.control / fs.body / sp.2 / ...
QHash<QString, QString>
theme_token_map(const ThemeTokens &v_tokens);

// 单个 token 的 QSS 字符串值（找不到返回空串）
QString
theme_token_value(const ThemeTokens &v_tokens, const QString &v_name);

// 颜色 → QSS 字符串：不透明输出 #RRGGBB，半透明输出 rgba(r, g, b, p%)
QString
theme_color_to_qss(const QColor &v_color);

#endif // THEME_TOKENS_H
