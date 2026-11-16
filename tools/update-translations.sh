#!/usr/bin/env bash
# update-translations.sh —— 更新 real-esrguisoft 英文翻译资产
#
# 说明：本项目 .qm 由 Windows 侧 Qt 6.12 lrelease 生成（Linux 通常没有 Qt6 linguist 工具）。
#   - 若本机有 lupdate/lrelease（Qt6），可直接运行本脚本；
#   - 否则请用 tools/update-translations.ps1 在 Windows MSYS2 UCRT64 中执行（环境见 docs/design/windows-build-env.md）。
# 脚本不自动提交；产出 resources/i18n/app_en.ts（lupdate 更新）与 app_en.qm（lrelease）。
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"
if ! command -v lupdate >/dev/null 2>&1 || ! command -v lrelease >/dev/null 2>&1; then
	echo "[SKIP] 本机缺少 Qt6 lupdate/lrelease。"
	echo "       请在 Windows MSYS2 UCRT64 上运行：powershell -NoProfile -ExecutionPolicy Bypass -File tools/update-translations.ps1"
	echo "       或参考 docs/design/windows-build-env.md（Windows 桥接 192.168.3.19 / MSYSTEM=UCRT64）。"
	exit 2
fi
echo "lupdate: $(lupdate -version 2>&1 | head -1)"
lupdate -no-obsolete src main.cpp -ts resources/i18n/app_en.ts
echo "unfinished: $(grep -c 'type="unfinished"' resources/i18n/app_en.ts || true)"
lrelease -silent resources/i18n/app_en.ts -qm resources/i18n/app_en.qm
echo "qm: $(stat -c%s resources/i18n/app_en.qm) bytes"
echo "完成：未自动提交；请人工检查后提交 resources/i18n/ 与 resources/resources.qrc"
