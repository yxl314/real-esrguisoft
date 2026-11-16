# cc-switch v4.0.31 界面设计规范（供 Qt Widgets + QSS 复刻）

> 素材来源：GitHub `farion1231/cc-switch` tag `v4.0.31`（本地仓库快照 `C:\Users\yxl\AppData\Local\Temp\ccs\repo`，路径均相对仓库根）。
> 截图：`C:\Users\yxl\AppData\Local\Temp\ccs\img\*.png`（Retina @2x，逻辑坐标 = 设备像素 / 2）。
> 数值标注规则：`来源: 文件:行号` 表示源码取值；`测量值` 表示从 rn-main.png 像素测量并换算成逻辑 px。

---

## 0. 结论速览（复刻时先锁定这 10 组数值）

| 项 | 值 | 来源 |
|---|---|---|
| 侧栏宽 | 展开 **200**，收起 **72**（macOS **84**），窗口 < 960 自动收起 | `src/hooks/useSidebarCollapsed.ts:12,16,18` |
| 页头高 | **52**（含 1px 底边框，即内容 51 + border 1） | `src/components/shell/AppPageHeader.tsx:18`；测量 52 |
| 窗口 | macOS 1000×650（min 900×600）；Windows/Linux 1100×650（min 1000×640） | `src-tauri/tauri.conf.json`、`src-tauri/tauri.windows.conf.json` |
| 控件高 | **32 / 28 / 36**（regular / compact / segmented-pageTabs）；舒适密度 36 / 32 / 34(导航行) | `src/index.css:401-406,426-430` |
| 圆角 | **6（control）/ 10（panel）/ 14（dialog）/ 9999（full）** | `docs/design-system.html:53-55` |
| 主操作橙 | 亮 `#F97316`（hover `#EA620A`）；暗 `#EA580C`（hover `#F06D1F`） | `src/index.css:81-83,167-169` |
| 卡片 | min-h **58**、py **10**、ps **8** pe **12**、gap **12**、radius **10**、两行实测高 61、卡间距 8 | `ProviderCard.tsx:449`、`index.css:404-405`；测量值 |
| 分段控件 | 轨道 **36** 高 / 内衬 **3** / 选中块圆角 **7** / 滑块滑动 **200ms**；sm 档 28/8/5 | `segmented-control.tsx:50-53,71-75` |
| 字号阶梯 | 11/16、12/18、13/20、14/20、15/22、16/24、18/26、24/32 | `src/index.css:384-399`、`tailwind.config.cjs:182-209` |
| 焦点环 / 过渡 | 焦点 **2px solid ring, offset 2px**；过渡 **150ms cubic-bezier(.4,0,.2,1)** | `index.css:549-552`、`design-system.html:61-62` |

---

## 1. 设计 Token 表

### 1.1 颜色（亮色）

| 语义 token | 亮色值 | 用途 | 来源 |
|---|---|---|---|
| `--bg-app` | `#FFFFFF` | 内容区底 | `src/index.css:72` |
| `--bg-sidebar` | `#F5F5F7` | 侧边栏底 | `src/index.css:73` |
| `--bg-subtle` | `#EFEFF2` | hover 底、分段轨道、通知、图标框底 | `src/index.css:74` |
| `--bg-selected` | `#E4E4EA` | 侧栏当前项 | `src/index.css:75` |
| `--bg-card` / surface | `#FFFFFF` | 卡片、弹层、输入框 | `src/index.css:76` |
| `--border`（hsl） | `hsl(240 10.2% 90.4%)` ≈ `#E4E4E9` | 默认描边、分隔线 | `src/index.css:30`、`design-system.html:28` |
| `--border-strong` | `#CFCFD6` | 描边按钮、hover 卡片、输入框、复选框描边 | `src/index.css:77` |
| `--text-1` | `#18181B` | 主文字（标题/正文） | `src/index.css:78` |
| `--text-2` | `#52525B` | 次文字、图标 | `src/index.css:79` |
| `--text-3` | `#65656E` | 三级文字、占位、辅助说明 | `src/index.css:80` |
| `--action-bg` | `#F97316` | 主操作橙（按钮/开关开/勾选） | `src/index.css:81` |
| `--action-hover` | `#EA620A` | 主操作 hover | `src/index.css:83` |
| `--action-fg` | `#FFFFFF` | 主操作前景 | `src/index.css:82` |
| `--action-text` | `#C2410C` | 橙色当文字用（链接/选中文字，5.2:1） | `src/index.css:84` |
| `--action-soft` | `#FFF1E6` | 橙色浅底 | `src/index.css:85` |
| `--inverse-bg/-fg` | `#18181B` / `#FFFFFF` | HoverTip 胶囊、反色块 | `src/index.css:86-87` |
| `--inverse-hover` | `#2E2E33` | 反色块 hover | `src/index.css:88` |
| `--overlay` | `rgba(9,9,11,0.36)` | 对话框遮罩 | `src/index.css:89` |
| `--control-off` | `#888890` | 开关关态轨道 | `src/index.css:90` |
| 焦点环 `--ring` | `#F97316` | 全局 focus-visible 描边 | `index.css:32-33`、`design-system.html:20` |
| `--success` / text / soft | `#16A34A` / `#007634` / `#E8F7EE` | 健康、额度充足 | `index.css:114-116` |
| `--warning` / text / soft | `#D97706` / `#A64A00` / `#FEF3C7` | 额度将尽、需重登 | `index.css:117-119` |
| `--danger` / text / soft | `#DC2626` / `#B91C1C` / `#FEE2E2` | 失败、删除 | `index.css:120-122` |
| 模式-直连 fill/text/soft/solid | `#0A84FF` / `#0063C7` / `#EAF3FF` / `#0066CC` | 直连模式（当前卡蓝底蓝边） | `index.css:93-98` |
| 模式-路由 fill/text/soft/solid | `#0D9488` / `#06726B` / `#E7F6F4` / `#0F766E` | 路由模式 | `index.css:100-105` |
| 模式-聚合 fill/text/soft/solid | `#7C3AED` / `#6D28D9` / `#F2ECFE` / `#6D28D9` | 聚合模式 | `index.css:107-112` |
| `--chart-1` / `--chart-2` / `--chart-grid` | `#0891B2` / `#DB2777` / `#ECECF0` | 图表、热力图 | `index.css:123,133,134` |
| 热力图 1–4 | chart-1 32%/62%/100% + `#0E7490` | 热力图色阶 | `index.css:129-132` |

### 1.2 颜色（暗色）

| token | 暗色值 | 来源 |
|---|---|---|
| `--bg-app` | `#1C1C1E` | `index.css:155` |
| `--bg-sidebar` | `#161618` | `index.css:156` |
| `--bg-subtle` | `#26262A` | `index.css:157` |
| `--bg-selected` | `#313136` | `index.css:158` |
| `--bg-card` | `#2E2E33` | `index.css:159` |
| `--border` | `hsl(240 5.3% 25.7%)` ≈ `#3E3E45` | `index.css:61`、`design-system.html:88` |
| `--border-strong` | `#4A4A52` | `index.css:160` |
| `--text-1` / `--text-2` / `--text-3` | `#E4E4E7` / `#ABABB3` / `#9A9AA2` | `index.css:163-165` |
| `--action-bg` / hover / fg | `#EA580C` / `#F06D1F` / `#FFFFFF` | `index.css:167-169` |
| `--action-text` | `#FB923C` | `index.css:170` |
| `--action-soft` | `rgba(249,115,21,0.12)` | `index.css:172` |
| `--ring` | `hsl(24 66% 54%)` ≈ `#D77A3C` | `index.css:64`、`design-system.html:86` |
| `--inverse-bg` / fg / hover | `#F4F4F5` / `#18181B` / `#E4E4E7` | `index.css:173-175` |
| `--overlay` | `rgba(0,0,0,0.55)` | `index.css:176` |
| `--control-off` | `#6F6F77` | `index.css:177` |
| `--success` / text / soft | `#4ADE80` / `#4ADE80` / `#0F2B1A` | `index.css:202-204` |
| `--warning` / text / soft | `#FBBF24` / `#FCD34D` / `#33260A` | `index.css:205-207` |
| `--danger` / text / soft | `#F87171` / `#FCA5A5` / `#3A1414` | `index.css:208-210` |
| 模式-直连 | fill `#4A8FD9`、text `#8FB8E6`、soft `rgba(74,143,217,.12)`、solid `#2F6DB3`、border = fill 50% | `index.css:181-186` |
| 模式-路由 | fill `#2A9D8F`、text `#7CC4BA`、soft `rgba(42,157,143,.12)`、solid `#1F7D72` | `index.css:188-193` |
| 模式-聚合 | fill `#8B74D6`、text `#B5A6E3`、soft `rgba(139,116,214,.12)`、solid `#6A52BF` | `index.css:195-200` |
| `--chart-1` / `--chart-2` / `--chart-grid` | `#38BDF8` / `#F472B6` / `#2C2C31` | `index.css:211,217,218` |

> 暗色卡片描边规则：`--shadow-sm` 在暗色下是「0 0 0 1px rgba(255,255,255,.08) + 投影」，即深色靠 8% 白描边分层（`index.css:229-232`、`design-system.html:689`）。
> 三档文字对 `--bg-card` / `--bg-selected` 对比度均 ≥ 4.5:1（`index.css:161-162` 注释）。

### 1.3 阴影

| token | 值 | 用途 | 来源 |
|---|---|---|---|
| `--shadow-sm` / `shadow-v7-sm` | 亮 `0 1px 2px rgba(16,24,40,.06)`；暗 `0 0 0 1px rgba(255,255,255,.08), 0 1px 2px rgba(0,0,0,.4)` | 分段选中块、卡片 hover | `index.css:147,229-230` |
| `--shadow-md` | 亮 `0 4px 12px rgba(16,24,40,.08), 0 1px 3px rgba(16,24,40,.06)`；暗 `0 4px 12px rgba(0,0,0,.45), 0 1px 3px rgba(0,0,0,.4)` | 菜单、说明卡、HoverTip、拖拽中 | `index.css:148-149,231` |
| `--shadow-lg` | 亮 `0 20px 40px rgba(16,24,40,.16), 0 4px 12px rgba(16,24,40,.08)`；暗 `0 20px 40px rgba(0,0,0,.5), 0 4px 12px rgba(0,0,0,.4)` | 对话框、抽屉、命令面板 | `index.css:150-151,232` |

### 1.4 圆角

| token | 值 | 用途 | 来源 |
|---|---|---|---|
| control | **6px** | 按钮、输入框、侧栏行、菜单项、图标按钮 | `design-system.html:53,675`、`tailwind.config.cjs:177` |
| panel | **10px** | 卡片、分段控件轨道、通知条、Toast、菜单容器 | `design-system.html:54,676`、`tailwind.config.cjs:178` |
| dialog | **14px** | 对话框、抽屉、命令面板 | `design-system.html:55,677`、`tailwind.config.cjs:179` |
| full | 9999px | 徽章、开关、HoverTip、页签下划线、滚动条 | `design-system.html:678` |
| 嵌套规则 | 内圆角 = 外圆角 − 内衬（分段轨道 10 − 内衬 3 = **7**） | 分段选中块 | `design-system.html:682` |
| 供应商图标框 | **8px**（32×32） | ProviderIconBox | `ProviderIconBox.tsx`（h-8 w-8 rounded-[8px]） |
| 对话框内小控件 | 6（按钮）/ 5（小块） | — | `design-system.html:300,309` |

### 1.5 字体栈与字号阶梯

字体栈（不加载网络字体，`design-system.html:57-59`、`tailwind.config.cjs:220-247`）：

```
--font-sans: -apple-system, BlinkMacSystemFont, "SF Pro Text", "PingFang SC",
  "Hiragino Sans GB", "Segoe UI Variable Text", "Segoe UI",
  "Microsoft YaHei UI", "Microsoft YaHei", Roboto, "Helvetica Neue", Arial, system-ui, sans-serif;
--font-mono: ui-monospace, SFMono-Regular, "SF Mono", Consolas, "Liberation Mono", Menlo, monospace;
```

| 角色 | 字号/行高 | 字重 | 用途 | 来源 |
|---|---|---|---|---|
| badge | **11 / 16** | 500 | 徽章、分组标题、侧栏行尾小字 | `index.css:384-385` |
| caption | **12 / 18** | 400 | 卡片副行、说明、菜单快捷键 | `index.css:386-387` |
| body | **13 / 20** | 400 | 正文、按钮、菜单项、输入框 | `index.css:388-389` |
| strong | **14 / 20** | 500 | 卡片名、区块小标题 | `index.css:390-391` |
| section | **15 / 22** | 600 | 空态标题、设置分组标题 | `index.css:392-393` |
| title | **16 / 24** | 600 | 应用页页头（应用名）、对话框标题 | `index.css:394-395` |
| page | **18 / 26** | 600 | 全局页/设置页页头标题 | `index.css:396-397` |
| metric | **24 / 32** | 600 + tabular-nums | 统计数值 | `tailwind.config.cjs:206` |
| mono | 12（表格内 11.5） | 400 | 密钥、URL、模型名、状态码 | `design-system.html:108,441` |

> 界面密度：默认 compact（上表）；`<html data-density="comfortable">` 时字号整体 +1 档、控件高 +4（`index.css:408-431`）。复刻时建议先只做 compact。

### 1.6 控件高度（`--h-*`）

| token | compact（默认） | comfortable | 用途 | 来源 |
|---|---|---|---|---|
| `--h-control` | **32px** | 36px | 按钮 regular、输入框、下拉、页头按钮 | `index.css:401,426` |
| `--h-control-sm` | **28px** | 32px | 按钮 compact、图标按钮 icon-compact | `index.css:402,427` |
| `--h-nav-row` | **28px** | 34px | 侧栏导航行 | `index.css:403,428` |
| `--h-card`（min-h） | **58px** | 68px | 供应商卡片最小高 | `index.css:404,429` |
| `--card-py` | **10px** | 14px | 卡片上下内边距 | `index.css:405,430` |
| 分段控件 / 页签 | **36px** | — | md 分段、一级页签 | `segmented-control.tsx:50`、`page-tabs.tsx:96` |
| 大按钮 lg | 36px | — | 少数强调按钮 | `button.tsx`（h-9） |

### 1.7 间距刻度（4px 基准）

| 刻度 | 值 | 用途 | 来源 |
|---|---|---|---|
| 1 | **4px** | 图标与文字 | `design-system.html:665` |
| 1.5 | **6px** | 按钮内图标间距、字段 label 与控件、HoverTip 距触发元素 | `design-system.html:666,756` |
| 2 | **8px** | 卡片列表间距、按钮组 gap | `design-system.html:667` |
| 3 | **12px** | 卡片内元素间距（gap-3） | `design-system.html:668` |
| 4 | **16px** | 模式行元素间距、页头左右内边距（pe-4） | `design-system.html:669`、`AppPageHeader.tsx:74` |
| 5 | **20px** | 对话框内边距 | `design-system.html:670`、`.dialog{padding:20px}` |
| 6 | **24px** | 页面左右留白（px-6） | `design-system.html:671`、`App.tsx:1694` |
| 12 | **48px** | 列表底部留白（pb-12） | `App.tsx:1694` |

---

## 2. 布局骨架

### 2.1 窗口与无边框标题栏

| 项 | 值 | 来源 |
|---|---|---|
| macOS 窗口 | 1000×650，min 900×600，`titleBarStyle: "Overlay"`，`trafficLightPosition {x:14,y:16}`，`title: ""` | `src-tauri/tauri.conf.json` |
| Windows/Linux 窗口 | 1100×650，min 1000×640，`decorations: false`、`shadow: true`、`titleBarStyle: "Visible"`，标题栏由前端自绘 | `src-tauri/tauri.windows.conf.json` |
| macOS 红黄绿 | 系统原生 12px 圆点，左侧 14px、顶部 16px 起；测量：圆心 y≈16，x≈20.7 / 43.7 / 66.7，直径约 12–14（含抗锯齿） | 配置 + `rn-main.png` 测量值 |
| Windows 自绘按钮 | 页头最右侧 3 个 28×28 图标按钮（最小化 / 最大化还原 / 关闭），`h-7 w-7 rounded-control`，正常 `text-fg-2`，hover `bg-subtle text-fg-1`；关闭 hover 用 `bg-danger-soft text-danger-text` | `WindowControls.tsx`（BUTTON 常量与 className） |
| 拖拽区 | 页头整条 + 侧栏顶条 `data-tauri-drag-region`；按钮区 `no-drag` | `AppPageHeader.tsx:73-77`、`Sidebar.tsx:50` |
| 圆角 | macOS 窗口圆角由系统绘制；Windows 用 `decorations:false + shadow:true`，客户端区域无圆角（若 Qt 复刻需自绘 12px 圆角 + 半透明背景） | 同上 |

### 2.2 整体网格

```
┌──────────────┬────────────────────────────────────────────┐
│ Sidebar      │ AppPageHeader  52（含 1px 底边框 #E4E4E9）  │
│ 200 / 72     ├────────────────────────────────────────────┤
│ bg #F5F5F7   │ 模式行 / 页签，高 36，y=64 起（pt 12）      │
│ border-e 1px │────────────────────────────────────────────│
│              │ 列表滚动区 px-6(24) pt-4(16) pb-12(48)      │
│              │ 卡片宽 = 视口 − 48；卡间距 8                │
└──────────────┴────────────────────────────────────────────┘
```

- 外层：`flex h-screen overflow-hidden bg-app`（`App.tsx:1919`）；侧栏 `border-e border-border`（`Sidebar.tsx:87`）。
- 主区：`relative isolate flex min-w-0 flex-1 flex-col overflow-hidden bg-app`（`App.tsx:1936-1939`）。
- 内容滚动容器：`overflow-y-auto scroll-stable px-6 pb-12 pt-4`（`App.tsx:1694`）。`scroll-stable` = `overflow-y: scroll` 常留 8px 滚动条槽（`index.css:581-583`），避免列表跨过可滚动临界时整列横跳。
- 测量校验（`rn-main.png` @2x，逻辑 1072×703）：侧栏 0–199 + 1px 边框；内容 200–1064；卡左 224、右 1039（padding 24）；页头下边框 y=52；模式行 y=64–100；首卡 y=112。

### 2.3 侧边栏

| 项 | 值 | 来源 |
|---|---|---|
| 宽 | 展开 **200**，收起 **72**（macOS 84，给红绿灯让位）；<960 自动收起；展开/收起动画 200ms（盖板 transform，宽度不过渡） | `useSidebarCollapsed.ts:12,16,18,20` |
| 背景 / 右边框 | `#F5F5F7` / 1px `--border` | `Sidebar.tsx:87` |
| 顶条 | 高 **44**（`h-11`），macOS 只放红绿灯；Windows 放 logo 22×22 + “CC Switch”（strong 14/600）+ 右侧折叠按钮（28×28） | `Sidebar.tsx:136-205` |
| 品牌行（macOS 展开） | 高 36，上移 18（`-mt-[18px]`），`ps-4 pe-2`，logo 20×20，字 14/600，右侧折叠按钮 28×28 | `Sidebar.tsx:142-157` |
| 导航项（展开） | 高 **28**（`h-nav-row`）、宽 **184**（`w-[184px]`，`mx-2` 即左右 8）、`px-2`（8）、`gap-2`（8）、圆角 **6**；图标 16×16（应用图标）/ 18×18（全局项），字 13/400，行尾模式标签 | `Sidebar.tsx:485-489,646-652` |
| 导航项（收起） | 高 **32**（`h-8`）× 宽 **48**（`w-12`）居中，圆角 6，图标 20×20 | `Sidebar.tsx:455,616` |
| 选中态 | 底 `--bg-selected`（`#E4E4EA` / `#313136`）+ 字 500（`font-medium`）+ 字色 `--text-1`；hover 非选中 `--bg-subtle` | `Sidebar.tsx:486-487,649` |
| 图标色 | `--text-2`，选中行内仍 `text-fg-2` | `Sidebar.tsx:579` |
| 分组标题 | 11px / 500 / `--text-3`，`px-4 pb-1`（设置目录） | `Sidebar.tsx:741` |
| 分组分隔线 | 1px `--border`；展开 `mx-4 mb-1.5 mt-0.5`，收起 `mx-[18px] my-1.5` | `Sidebar.tsx:325-330,349-354` |
| 行尾标签（模式） | 高 **18**、`px-1.5`（6）、圆角 full、11/18：路由 = `bg-route-soft text-route-text`，聚合 = `bg-stack-soft text-stack-text`；直连不显示标签 | `Sidebar.tsx:391-408,500-509` |
| 行尾小字（今日用量） | 12px / 13 行高、`--text-3`、tabular-nums、右对齐 `pe-1.5` | `Sidebar.tsx:660` |
| 红点 / 角标 | 6×6（`h-1.5 w-1.5`）`bg-danger`；图标角标 8×8~14×14 圆形带 2px 背景描边；更新圆点 6×6（中性 `bg-fg-1` / 成功 `bg-success`） | `Sidebar.tsx:494-496,581-599,684-688` |
| 结构 | 顶条 → 应用列表（唯一滚动区，`overflow-y-auto`）→ 分隔线 → 全局 6 项（MCP / Skills / 提示词 / 会话 / 授权中心 / 用量统计）→ 分隔线 → 底栏「应用 · 设置」两个并排 compact 项（`gap-1 px-2`） | `Sidebar.tsx:298-388` |
| 收起态提示 | 图标的 HoverTip 从右侧弹出；展开态文字可见不挂提示 | `Sidebar.tsx:73` 注释 |

### 2.4 主区页头

| 项 | 值 | 来源 |
|---|---|---|
| 高 | **52**（`h-[52px]`，含 `border-b 1px`） | `AppPageHeader.tsx:18,74` |
| 内边距 / 间距 | `ps-6`（24）、`pe-4`（16）、`gap-5`（20） | `AppPageHeader.tsx:74` |
| 标题 | 应用页 `text-title` 16/24/600 + 副标题 13px `--text-3`（gap 4）；全局页/设置页 `text-page` 18/26/600 | `AppPageHeader.tsx:23,110-118` |
| 前置图标 | 22×22，`text-fg-1` | `AppPageHeader.tsx:103-107` |
| 右侧操作 | `flex gap-2`（8），主按钮最后一个，`⋯` 在它后面；Windows 再追加 3 个窗口按钮（`gap-1`，28×28） | `AppPageHeader.tsx:128-142`、`WindowControls.tsx` |
| 返回按钮（二级页） | 28×28 图标按钮，`-ms-2` | `AppPageHeader.tsx:96-99` |
| 测量值 | 主按钮「添加供应商」**116×32**，y=10–42；右端距窗口右 16+28(⋯)+8 → 按钮右缘 ≈1020 | `rn-main.png` 测量值 |
| 通知槽 | 页头之下、模式行之上，`NoticeSlot` `flex flex-col gap-2`、`role=status`；空时不占位 | `notice.tsx`（NoticeSlot） |

---

## 3. 组件解剖

通用状态约定（`design-system.html:702`、`button.tsx`）：
- hover 只改底色/描边/颜色，**150ms**；按下 `scale 0.96`（仅“执行动作”类按钮；带 `aria-haspopup` 的菜单触发器不缩放）。
- disabled 统一 **opacity 45%**（`tailwind.config.cjs:8`、`index.css:302-306`）。
- 焦点环：全局 `*:focus-visible { outline: 2px solid hsl(var(--ring)); outline-offset: 2px }`（`index.css:549-552`）；输入框另加 3px 光晕（见 3.7）。
- 键盘模态才画焦点环：鼠标点击后不显示主题色描边（`index.css:554-565`，`inputModality.ts`）。

### 3.1 供应商卡片（ProviderCard）

结构（`ProviderCard.tsx:439-499`）：

```
div.pcard  (relative, rounded-panel 10, border 1px, transition border/shadow/bg 150ms)
 ├ div.cardtone  (flex, min-h-card=58, items-center, gap-3=12, py-card-y=10, ps-2=8, pe-3=12)
 │  ├ 拖拽手柄 16×24 (h-6 w-4, 默认 opacity 0，hover/focus 显示；图标 GripVertical 16，text-fg-3)
 │  ├ 图标框 32×32 (rounded 8, border --border；透明 logo 内图形 22，tile 图标铺满)
 │  ├ div.body (flex-1 min-w-0)
 │  │  ├ 名称 14/20/500 text-strong + 徽章们 (gap-1.5=6)
 │  │  └ 副行 12/18 text-caption text-fg-2 (mt-0.5=2；URL / 上次使用 / 额度提示)
 │  ├ div.quota (max-w-[208px], text-end, gap-1)
 │  └ ProviderCardActions (gap-2=8)
 │     ├ 主操作位 (min-w-20=80, justify-end, gap-1) — 状态文字「● 使用中」或 1 个图标按钮
 │     ├ 上移/下移 (有队列时) 图标按钮
 │     ├ 打开终端 图标按钮
 │     ├ 编辑 图标按钮
 │     └ ⋯ 图标按钮 → DropdownMenu(min-w 180)
```

尺寸与状态：

| 项 | 值 | 来源 |
|---|---|---|
| 最小高 | **58**；两行内容实测 **61**（border 1×2 + py 10×2 + 名称 20 + 副行 18 + margin 2 ≈ 61） | `index.css:404`；`rn-main.png` 测量值 |
| 圆角 / 描边 | 10 / 1px `--border` | `ProviderCard.tsx:442,445` |
| 内边距 | `py 10 / ps 8 / pe 12`，元素间距 12 | `ProviderCard.tsx:449` |
| 卡间距 | **8**（列表 `space-y-2` 语义；测量 8） | 测量值 + `design-system.html:667` |
| hover | `border-color → --border-strong` + `shadow-v7-sm`（150ms） | `ProviderCard.tsx:445`、`design-system.html:343` |
| 拖拽中 | `cursor-grabbing` + `shadow-v7-md`；拖拽手柄常显 opacity 1 | `ProviderCard.tsx:446,454-455` |
| **激活（当前生效）** | 模式色 soft 底 + 模式色 fill 描边（1px）：直连 `border-direct-border bg-direct-soft`，路由 `border-route-border bg-route-soft`，聚合 `border-stack-border bg-stack-soft`；无阴影 | `ProviderCard.tsx:96-100`；测量 直连边框 `#0B84FF`、底 `#EAF3FF` |
| 激活卡按钮 | 右侧操作常显（不依赖 hover）——见 `ProviderCardActions`，激活卡的状态位写「● 使用中 / 路由中」文字（6px 圆点 + 13/500 文字），不是按钮 | `rn-main.png`、`design-system.html:848,859` |
| 卡内徽章 | 高 **18**（`h-[18px]`）、`px-1.5`、full、11/16/500；outline = `border border-border-strong text-fg-2`；模式/语义色 = xxx-soft 底 + xxx-text 字 | `ProviderCard.tsx:102-125` |
| 禁用/暗化卡 | `opacity-60` | `ProviderCard.tsx:475` |
| 卡片菜单 | 宽 min 180，item 高 30、`px-2`、圆角 6、hover `bg-subtle`；删除项 `text-danger-text`；带禁用原因时副行 12px `--text-3` | `design-system.html:384-390`、`ProviderCardActions.tsx` |

### 3.2 分段控件（SegmentedControl）

| 项 | md（模式 tab / 页内筛选） | sm（面板内小切换） | 来源 |
|---|---|---|---|
| 轨道高 | **36**（`h-9`） | **28**（`h-control-sm`） | `segmented-control.tsx:50-51` |
| 轨道底 / 圆角 | `--bg-subtle` / **10** | `--bg-subtle` / **8** | 同上 |
| 内衬 | **3px**（`p-[3px]`） | 3px | 同上 |
| 选中块 | `--bg-card` + `shadow-v7-sm`，圆角 **7**（md）/ **5**（sm），不画底色到按钮上 | `segmented-control.tsx:53,71-76` | 
| 按钮 | `px-3.5`（14）、字 13/500 `--text-2`；选中 `--text-1`；hover `--text-1` | sm `px-2.5`（10）、12px | `segmented-control.tsx:80-92` |
| 按钮间距 | `gap-1`（实际由内衬+按钮排布，模式 tab 每格最小 **96** 宽） | — | `design-system.html:311`；测量 96 |
| 图标 | md 16×16 / sm 14×14，`strokeWidth 2`，与文字 gap 6 | 同上 |
| 滑块动画 | **200ms ease-out**，动 `left/top/width/height`（不用 transform，避免 WebKit 半像素抖动）；首帧不播；`prefers-reduced-motion` 直接跳 | `sliding-indicator.tsx`、`design-system.html:802` |
| 模式 tab 附加 | 图标 + 文字 + 6px 模式色圆点（生效中）；选中/生效格 `font-weight 600`；文字用模式色 `--*‑text` | `design-system.html:311-316,990-1001` |
| **测量值** | 轨道 **298×36**（x=224，y=64），选中块 **96×30**（inset 3，圆角 7） | `rn-main.png` |

### 3.3 主按钮（橙色实心，variant=solid，size=regular/compact）

| 项 | regular | compact | 来源 |
|---|---|---|---|
| 高 | **32**（`h-control`） | **28**（`h-control-sm`） | `button.tsx` size 表 |
| 内边距 | `px-3.5`（14） | `px-3`（12） | 同上 |
| 圆角 | 6 | 6 | 同上 |
| 底 / 字 | `--action-bg` / `--action-fg`，字重 **600**（白字对比度低，靠 600 补偿，`index.css:69` 注释） | 同 | `button.tsx` solid |
| hover | 底 → `--action-hover`（150ms） | 同 | 同上 |
| active | `scale .96` | 同 | 同上 |
| disabled | `opacity 50`（通用），neutral 专有 45% 样式 | — | `button.tsx`、`tailwind.config.cjs:8` |
| 内容 | 图标 16×16 + 文字 gap **6**（`gap-1.5`），`white-space: nowrap` | 同 | `button.tsx` |
| 约束 | **一个视图只有一个实心按钮**；其余用 neutral（描边）或 quiet（透明） | — | `design-system.html:716` |
| 测量值 | 「+ 添加供应商」**116×32**，文字白色，图标 16，圆角 6 | — | `rn-main.png` |

### 3.4 次按钮 / 图标按钮

| 变体 | 外观 | 尺寸 | 来源 |
|---|---|---|---|
| neutral（描边） | `border --border-strong` + `bg-surface` + 字 `--text-1`；hover `bg-subtle`；disabled：`border --border` + 透明底 + `--text-3` | 高 28/32 | `button.tsx` neutral |
| quiet（透明） | 透明底、字 `--text-1`；hover `bg-subtle` | 高 28/32 | `button.tsx` quiet |
| destructive | `bg-danger` + 白字；hover `brightness(1.1)` | 高 28/32 | `button.tsx` destructive |
| 模式色实心 | `bg-direct-solid / route-solid / stack-solid` + on 色；hover brightness 1.1 | 32 | `button.tsx` direct/route/stack |
| 图标按钮 icon-compact | **28×28**，`p-0`，圆角 6，图标 16×16 `strokeWidth 1.5`，字色 `--text-2`；hover `bg-subtle` + `--text-1`；active scale .96 | 28 | `button.tsx` `icon-compact` |
| 图标按钮 icon（常规） | **32×32**，同色规则 | 32 | `button.tsx` icon |
| 卡片内连排图标按钮 | 间距 `gap-2`（8）；主操作位定宽 **80**（`min-w-20`）保证切换后不左右跳 | — | `ProviderCardActions.tsx` |

### 3.5 标签 / 徽章

| 类型 | 高 | 内边距 | 圆角 | 字号 | 底色/字色 | 来源 |
|---|---|---|---|---|---|---|
| 卡片 chip | **18** | `px-1.5`(6) | full | 11/16/500 | outline `border --border-strong` + `--text-2`；状态用 soft+text | `ProviderCard.tsx:112-125` |
| design-system chip | **20** | `px-7`→`0 7px` | full | 11/16/500 | 同上，可带 6px 圆点 `currentColor` | `design-system.html:326-335` |
| 「官方」 | 18 | 6 | full | 11 | `border --border-strong`，`--text-2` | `rn-main.png`、`presentation.ts` |
| 「需要路由」 | 18 | 6 | full | 11 | 同上（outline 中性） | `rn-main.png` |
| 「当前使用 / 使用中」 | — | — | — | 13/500 | 文字 + 6px 模式色圆点（`--direct-fill` 等），非彩色块 | `ProviderCardActions.tsx`（status）、`rn-main.png` |
| 开关旁小徽章（npm） | 18 | 6 | full | 11 | outline | `rn-apps.png` |

约束：一张卡片最多 2 个徽章（`design-system.html:830`）；徽章不用实心彩色块。

### 3.6 输入框 / 下拉框 / 搜索框

**输入框**（`input.tsx` fieldClass + `design-system.html:259-270`）：
- 高 **32**（`h-control`），圆角 **6**，`border 1px --border-strong`，底 `--bg-card`，`px-2.5`（10），字 13/20 `--text-1`，placeholder `--text-3`。
- focus：`border-color → --ring` + `box-shadow 0 0 0 3px ring/20%`（暗色 15%），150ms。
- invalid：`border --danger`；label 在上（12/500 `--text-1`），hint 在下 12 `--text-2`，错误说明换 `--danger-text`，不弹 toast。
- disabled：`opacity 50`，`cursor not-allowed`。
- mono 输入（密钥/URL/模型名）：12px 等宽。

**下拉框 Select**（`select.tsx`）：触发器复用 fieldClass（32 高/圆角 6），右侧 `ChevronDown` 16 且 `opacity 50`；展开时保持 focus 外观（`border-ring + ring 3px`）。弹层：`rounded-md`（8）、`border --border`、`bg-popover`、`shadow-md`、`p-1`，item `py-1.5 pl-7 pr-2`（左侧 14px 位置放勾选 16×16），hover `bg-subtle`，进场 `fade-in-0 zoom-in-95`。

**搜索框 SearchField**：输入框 `ps-8 pe-8`（左右 32）；左侧放大镜 14×14 `--text-3` 定位 `start-2.5`（10）、垂直居中；有值时右侧 24×24 清除按钮（圆角 6，hover `bg-subtle`）；busy 时放大镜换转圈。

### 3.7 复选框 / 单选框（16px、1.5px 描边）

`src/index.css:236-307`（原生 input 加 `.ui-checkbox` / `.ui-radio`）：

| 状态 | 复选框 | 单选框 |
|---|---|---|
| 尺寸 | **16×16**，圆角 **4** | 16×16，圆角 full |
| 描边 | **1.5px --border-strong** | 同 |
| 底 | `--bg-card` | `--bg-card` |
| 选中 | 底/描边 = `--action-bg`，内部白色对勾（10×10，mask 描边 4，`scale 0→1` 120ms ease-out） | `border: 5px solid --action-bg`（粗主题色圈） |
| indeterminate | 同选中底 + 白色横线 | — |
| hover（未选中） | 描边 → `--text-3` | 同 |
| focus-visible | `outline 2px solid --ring; outline-offset 2px` | 同 |
| disabled | `opacity .45`，`cursor not-allowed` | 同 |
| 过渡 | `background-color/border-color 150ms` | 同 |
| 标签排布 | `inline-flex items-center gap-2`（8），字 13 | — |

### 3.8 开关（Switch）

`switch.tsx` + `index.css`/`design-system.html:272-286`：

| 项 | default | sm |
|---|---|---|
| 轨道 | **30×18**，full 圆角，`border 2px transparent` | **26×16** |
| 开态 | 轨道 `--action-bg` | 同 |
| 关态 | 轨道 `--control-off` | 同 |
| 圆点 | **14×14**，开态 `--action-fg`、关态白色，`shadow-v7-sm` | 12×12 |
| 位移 | 开态 `translateX(12px)` | `translateX(10px)` |
| 点击热区 | 伪元素外扩到 **38×28**（`before:-inset-x-1.5 -inset-y-1.5`） | — |
| 过渡 | `background-color / transform 150ms` | 同 |
| 焦点 | `ring 2px --ring + offset 2` | 同 |
| 禁用 | `opacity 45%` | 同 |
| tone=neutral | 开态轨道 `--text-2`，圆点 `--bg-card`（成排/不抢眼场景，如应用页列表） | 同 |

### 3.9 进度条 / 额度条

- 轨道高 **6**，full 圆角，底 `--bg-subtle`；填充同高、继承圆角（`design-system.html:435-436`）。
- 颜色阈值：>30% `--success`，10–30% `--warning`，<10% `--danger`（`design-system.html:979`）。
- 卡片内额度以文字行展示（如「5 小时剩余 100%」「每周 41% · Fable 73%」），右对齐、tabular-nums、12/18（`rn-main.png`）。
- 统计卡（`stat`）：`border --border` + 圆角 10 + `p 12/14`；键 12 `--text-2`，值 **24/32/600** tabular-nums，差异 12 `--text-3`（`design-system.html:430-434`）。

### 3.10 滚动条

`src/index.css:526-547`：

| 项 | 值 |
|---|---|
| 宽/高 | **8px**（`::-webkit-scrollbar { width: 8px; height: 8px }`） |
| 轨道 / 角 | 透明 |
| 滑块 | `--border-strong`，**2px 透明边框 + background-clip: padding-box**（视觉宽 4px，两侧各内缩 2px），圆角 **full** |
| hover | 滑块 → `--text-3` |
| 常显策略 | `.scroll-stable { overflow-y: scroll }` 预留 8px 槽，避免横跳；轨道透明，不可滚时不画滑块 |
| 隐藏 | `.no-scrollbar`（收起侧栏时用） |

### 3.11 通知条 / Toast / 对话框 / 菜单 / HoverTip（复刻常需）

| 组件 | 关键尺寸 | 来源 |
|---|---|---|
| 通知条 Notice | 圆角 **10**，`gap-2.5`(10)，`py-2.5`(10) `pe-3.5`(14) `ps-[19px]`；图标 16×16 `strokeWidth 1.5`；单行 items-center（min-h 44），双行图标对齐首行；tone 底：neutral `bg-subtle` / warning `warning-soft` / danger `danger-soft` / route `route-soft`；无左边框强调色 | `notice.tsx`、`design-system.html:357-368` |
| Toast | 宽 **340**（max 100%），圆角 10，`p 10/14/10/12`，`border --border` + `shadow-lg`；图标状态色；关闭按钮 20×20 压右上（-8,-8） | `design-system.html:403-414` |
| 对话框 | 圆角 **14**，`max-w-lg`（512），`max-h 90vh`，`p 20`，遮罩 `--overlay`，`shadow-lg`，标题 16/24/600，按钮区右对齐 `gap-2` `mt-5` | `dialog.tsx`、`design-system.html:394-400` |
| 菜单 | 宽 200，`p-1`(4)，圆角 10，item 高 30、圆角 6、`px-2`、hover `bg-subtle`；`kbd` 11px `--text-3` 右对齐 | `design-system.html:384-390` |
| HoverTip | 深色胶囊：`bg-inverse`、字 12/500 `inverse-fg`、`px-3 py-1.5`、full 圆角、`shadow-md`；**延迟 0**，淡入 **80–100ms**；距触发元素 **6px**，两侧碰撞留 8px；`max-w 280` | `tooltip.tsx`、`design-system.html:243-252,756` |
| HelpTip「?」 | 20×20 圆形按钮；说明卡宽 max **300**，圆角 **8**，`p 9/12/10`，字 12/18，`shadow-md`；悬停 300ms 开、移开 200ms 关，点一下可固定 | `design-system.html:370-381,924` |
| 空态 | 居中：图标底 56×56（`h-14 w-14`）圆角 16 `bg-subtle` + `border --border`，图标 24；标题 15/600；说明 13 `--text-2` `max-w-lg`；按钮区 `gap-2`，一律描边（页头已有实心按钮） | `ProviderEmptyState.tsx` |
| 加载态 | 页面级 `Suspense fallback`；按钮内 spinner 14×14（`Loader2 animate-spin`）；骨架不做，用转圈 + 保留旧内容 | `App.tsx:1975`、`ProviderCardActions.tsx` |
| 页签 PageTabs | 高 **36**（一级）/ **32**（二级 sm，12px）；项间距 `gap-4`(16) / `gap-3`(12)；下划线 **2px** `bg-fg-1`（**不用主题色**），`-bottom-px` 压住整行底边框；切换 200ms | `page-tabs.tsx:96,110-130`、`design-system.html:318-323,822` |

---

## 4. 交互与动效

| 项 | 规范 | 来源 |
|---|---|---|
| 默认时长/曲线 | **150ms** `cubic-bezier(.4,0,.2,1)` | `design-system.html:61-62` |
| 滑块（分段选中块、页签下划线） | **200ms ease-out**；首帧不播；`prefers-reduced-motion` 直接跳 | `sliding-indicator.tsx` |
| 浮层进出场 | 150–260ms `fade-in-0 zoom-in-95`（Radix/tailwindcss-animate）；HoverTip 80–100ms、0 延迟 | `index.css:471-508`、`tooltip.tsx`、`design-system.html:702` |
| 按下反馈 | `scale 0.96`；菜单/弹层触发器不缩放 | `button.tsx` |
| hover 过渡属性 | 卡片：`border-color, box-shadow, background-color`；按钮：`background-color, filter, scale`；控件：颜色/底色 | `ProviderCard.tsx:442`、`button.tsx` |
| 焦点环 | **2px solid `--ring`，offset 2px**；仅键盘模态（`data-keyboard`）显示；输入框/文本域额外 3px 22% 光晕 | `index.css:549-565` |
| 拖拽排序 | 手柄 16×24 默认透明，hover/聚焦 opacity 1；拖拽中 `cursor grabbing` + `shadow-md`；无占位动画规范，列表用 transform 平移 | `ProviderCard.tsx:450-462`、`design-system.html:692` |
| 卡片展开 | 额度多套餐时行内展开：`border-t` 分隔 + `mx-4 mb-3 pt-3`，chevron 14px 翻转 | `ProviderCard.tsx:786`、`729-734` |
| 主题切换 | 切换期间 `<html class="theme-switching">` 暂停所有 transition 两帧，避免整页闪 | `index.css:309-314` |
| 减少动效 | `@media (prefers-reduced-motion: reduce)` 关闭动画/过渡 | `index.css:510-524` |
| 空态/加载态 | 见 3.11；空态无插图，仅图标框 + 文案 + 描边按钮 | `ProviderEmptyState.tsx` |
| 键盘 | ⌘K 命令面板、Escape 关闭浮层、Tab 焦点环、禁用按钮保留 hover 说明 | `design-system.html:1037` |
| z 轴 | 对话框 40 → 嵌套 50 → 确认 60 → 命令面板 80 → 说明卡 120 → Tooltip 130 | `design-system.html:695` |

---

## 5. Qt（Qt Widgets + QSS）落地映射建议

### 5.1 QSS 能直接做到 / 需要自绘

| 效果 | QSS 可行性 | 落地方式 |
|---|---|---|
| 颜色 / 边框 / 圆角 / 内外边距 / 字号 / 字重 | ✅ 直接支持 | `background-color`、`border: 1px solid #CFCFD6`、`border-radius: 6px`、`padding: 0 14px`、`min-height: 32px`、`font-size: 13px`、`font-weight: 600` |
| hover / pressed / focus / checked / disabled / selected | ✅（`QPushButton:hover`、`:pressed`、`:focus`、`:checked`、`:disabled`、`QListView::item:selected`） | 注意 Qt 无 `:focus-visible`，可用自定义 property + `[keyboardFocus="true"]` 选择器切换 |
| 控件高度三档 | ✅ | `min-height/max-height` + `QSizePolicy Fixed`；token 生成时写入 |
| 卡片 10px 圆角 + 1px 描边 | ✅ | `QFrame#providerCard { border-radius:10px; border:1px solid ...; }`（需 `WA_StyledBackground` 或 QFrame 子类） |
| 卡片/弹层阴影（shadow-sm/md/lg） | ❌ QSS 无 `box-shadow` | `QGraphicsDropShadowEffect`（每条边一个偏移，Qt 只支持单层模糊，多段阴影需自绘 `QPixmap` 九宫格）或重写 `paintEvent` 用 `QPainterPath` + 多层半透明描边模拟 |
| 窗口圆角 + 无边框 | ⚠️ 半 | `Qt::FramelessWindowHint` + `WA_TranslucentBackground` + 自绘 12px 圆角与阴影；Windows 需处理 DWM 阴影（`DwmExtendFrameIntoClientArea`）或直接直角 |
| 焦点环 2px + offset 2px | ❌ | 自绘：`QProxyStyle::drawPrimitive(PE_FrameFocusRect)`，或控件 `paintEvent` 里按 `hasFocus() && keyboardModality` 画 `QPen(ring,2)` 的 `drawRoundedRect(rect.adjusted(-2,-2,2,2))` |
| 150ms 颜色过渡 | ❌ QSS 无 transition | `QPropertyAnimation`/`QVariantAnimation` 驱动 `QPalette` 或自定义 property + `style()->polish()`；简单做法：只在 hover 用 `QEvent::Enter/Leave` 起 150ms 动画 |
| 按下 scale .96 | ❌ | 用 `QTransform` 缩放需自绘；替代方案：按下时换 `padding`/`margin` 或 `QGraphicsScale`，或直接省略（触感损失小） |
| 分段控件（轨道 + 滑动白块） | ❌ 单靠 QSS 不行 | 自绘 `QSzegmentedControl : QWidget`：`paintEvent` 画轨道（10px 圆角 `#EFEFF2`）+ 当前项白块（7px 圆角 + 阴影），用 `QVariantAnimation` 200ms 插值 `x/width` |
| 下划线页签滑动 | ❌ | 自绘 2px 下划线，200ms `QVariantAnimation` 插值 |
| 开关 30×18 | ❌ QSS 的 QCheckBox indicator 只能静态/两态贴图 | 自定义 `QAbstractButton`：`paintEvent` 画胶囊轨道 + 圆点；开态圆点 `translateX(12)`，150ms 动画 |
| 复选框 16px/1.5px 描边 + 选中对勾 | ⚠️ 部分 | QSS `indicator` 可用 `border:1.5px`（Qt 会取整为 1 或 2px）；更稳的是自绘 indicator 或用 `QProxyStyle::drawPrimitive(PE_IndicatorCheckBox)`；半像素描边在 100% DPI 下不可靠 |
| HoverTip 深色胶囊 | ✅ 外观可（`QToolTip` 全局样式） | `QToolTip { background:#18181B; color:#fff; border:none; border-radius:9px; padding:6px 12px; font-size:12px }`；Qt 的 tooltip 延迟用 `QToolTip::showText` 手动 0 延迟，或自绘 `QWidget` 浮层（圆角+阴影需自绘） |
| 滚动条 8px / 内缩 2px | ✅ | `QScrollBar:vertical { width:8px; background:transparent }`、`handle { border-radius:4px; background:#CFCFD6; margin:2px }`（margin 实现内缩；视觉宽 4px） |
| 菜单/对话框圆角 + 阴影 | ⚠️ | 菜单 QSS 可设圆角 + 透明背景，阴影需 `WA_TranslucentBackground` + 自绘；对话框同理 |
| 徽章 18px full 圆角 | ✅ | `QLabel { border-radius: 9px; padding: 0 6px; font-size:11px; min-height:18px }` |
| 图标（lucide 1.75/1.5 stroke） | ✅ | 内嵌 SVG（`QSvgRenderer`）或转 PNG @2x；统一 16/14/20 三档 |
| 字体栈 | ⚠️ | QSS `font-family` 只取第一个可用；建议 `QFont("Microsoft YaHei UI")` + `setStyleStrategy(PreferAntialias)`，行高用 `QFontMetrics` 手动排（QSS 无 line-height） |
| CSS 变量 | ❌ | Qt QSS **没有变量**，见 5.2 |
| 百分比/`color-mix` 透明度 | ⚠️ | 预计算为 `rgba()` 常量；Qt 支持 `rgba(r,g,b,a)` |

### 5.2 Token → QSS 生成方案（运行时模板替换）

Qt QSS 没有 CSS 变量，推荐「**一次定义 JSON token，编译期/启动期生成两套 QSS**」：

```
tokens.json（亮/暗两套，键名与本文档一致）
      │  python scripts/gen_qss.py --theme light --out resources/qss/light.qss
      ▼
template.qss（{{action.bg}} / {{bg.card}} / {{h.control}} … 占位）
      ▼  str.replace / Jinja2
light.qss / dark.qss  →  qrc 打包或 exe 同级 qss/
      ▼
ThemeManager::apply(theme) → qApp->setStyleSheet(qss)
```

实现要点：

1. **占位命名用点号**，与 token 表一一对应：`{{action.bg}}`、`{{action.hover}}`、`{{bg.sidebar}}`、`{{border.default}}`、`{{text.1}}`、`{{ring}}`、`{{radius.control}}`、`{{h.control}}`、`{{fs.body}}`。
2. 生成脚本做双层循环（light/dark × 密度 compact/comfortable），输出 `light_compact.qss` 等 4 份；密度切换只换 QSS，不重建控件。
3. **颜色尽量预计算成 rgba 字符串**（如暗色 `action.soft = rgba(249,115,21,0.12)`），避免 QSS 里做运算。
4. QSS 是全局字符串，热重载时 `qApp->setStyleSheet(newQss)` 会让所有控件重 polish；切换主题前用 `QSignalBlocker`/暂停动画（对应 Web 的 `.theme-switching`）。
5. 尺寸单位：CSS px ↔ Qt 逻辑 px 1:1；Qt6 高 DPI 自动缩放。QSS 中 `1.5px` 会取整，边框统一用 1px 或 2px，需要 1.5px 的地方自绘。
6. 图标尺寸/字重这类没法走 QSS 的（如 `stroke-width`），在代码里用常量类 `Tokens.h` 由脚本从同一份 JSON 生成 C++ 头文件，保证 QSS 与自绘代码同源。
7. 圆角/阴影需要自绘的组件（卡片、浮层、开关、分段），把 token 通过 `ThemeManager::token("shadow.md")` 暴露给 `paintEvent`，避免硬编码。
8. 建议先做 4 个自定义控件：`SegmentedControl`、`Switch`、`UnderlineTabs`、`ShadowFrame（卡片/弹层容器）`；其余用标准 QPushButton/QLineEdit/QComboBox + QSS 覆盖。

---

## 6. 参考截图清单

| 截图（路径） | 尺寸 | 展示了哪些组件 | 是否与 v4.0.31 一致 |
|---|---|---|---|
| `C:\Users\yxl\AppData\Local\Temp\ccs\img\rn-main.png` | 2144×1406（@2x，逻辑 1072×703） | **主界面**：macOS 红黄绿、侧栏（品牌行/折叠箭头、选中行 #E4E4EA、应用行 + 模式标签、分隔线、全局项、底栏应用/设置）、页头 52（应用图标 22 + 标题 16/600 + 副标题 13 + 橙色主按钮 116×32 + ⋯）、模式分段 298×36（直连/路由/聚合 + 6px 圆点）、状态文字、供应商卡片（普通/激活蓝底蓝边/「官方」「需要路由」徽章/额度双行/图标框 32/图标按钮）、8px 滚动条、悬停态 | ✅ 一致（唯一可用作像素测量的 v4 截图） |
| `C:\Users\yxl\AppData\Local\Temp\ccs\img\rn-usage.png` | 2144×1406 | **用量统计页**：页头（标题 + ? + 会话日志同步 + 描边按钮「立即同步」+ 下拉「自动刷新 30 秒」+ quiet 按钮「数据来源」）、应用筛选分段（图标+全部）、下拉框 3 个、4 个统计卡（24/32/600 数字）、热力图卡（Tokens/请求/成本 sm 分段）、下划线页签（请求日志/供应商/模型/定价）、数据表格（12px、tabular-nums、状态码徽章）、日期/状态筛选 | ✅ 一致 |
| `C:\Users\yxl\AppData\Local\Temp\ccs\img\rn-apps.png` | 2144×1422 | **应用页**：页头（图标 + 标题 + ? + 次文字「上次检查」+ 描边按钮「全部升级(3)」+「检查更新」+ ⋯）、提示槽、列表表头（应用/版本/显示 ?）、行分隔线、图标框 32、npm 徽章、路径 12px 次文字、描边「升级」按钮、neutral 开关（深灰开态）、行底 1px 分隔 | ✅ 一致 |
| `C:\Users\yxl\AppData\Local\Temp\ccs\img\rn-session.png` | 2144×1422 | **会话阅读页**：页头返回箭头 + 标题 + 上下条 + 橙色 split 按钮 + ⋯、路径 chip（`~` + 复制图标）、元信息行 12px、图标切换按钮、segmented「全部/对话/改动」、右侧 quiet 图标按钮组、右侧「对话目录」面板（>360 宽、卡片列表、悬停高亮）、用户气泡（右下、米色底）、步骤行（失败红字 badge、code block）、「↓ 最新」浮起按钮 | ✅ 一致 |
| `C:\Users\yxl\AppData\Local\Temp\ccs\main-zh.png` | 1870×1252 | **旧版主界面（v3.x）**：无侧栏、顶部横向媒体 tab（Claude/Codex/Gemini）、蓝色主色 +、蓝色链接、大阴影卡片、卡内「当前使用」徽章 | ❌ 旧版（蓝色主色/无侧栏），**不可用于 v4 数值**，仅作历史对照 |
| `C:\Users\yxl\AppData\Local\Temp\ccs\img\add-zh.png` | 1980×1284 | **旧版「添加供应商」对话框（v3.x）**：返回箭头 + 标题、预设供应商胶囊选择（选中红底）、图标预览框、表单字段（名称/备注/官网）、底部右对齐「取消 + 蓝色添加」 | ❌ 旧版（蓝色主按钮），v4 中该流程已改为对话框 + 主题橙，**不要照抄颜色** |

补充说明：
- 只有 `rn-*.png` 是 v4.0.31 的界面；`main-zh.png` / `add-zh.png` 来自旧版 README，若后续需要对话框/表单的 v4 视觉，需另抓 `docs/images/**` 或运行时截图。
- v4 截图均为 macOS @2x，Windows 差异：无红黄绿（用页头右侧 28×28 自绘窗口按钮）、窗口 min 1000×640、侧栏折叠轨 72（macOS 84）。
