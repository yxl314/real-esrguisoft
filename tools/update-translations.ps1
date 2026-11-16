# update-translations.ps1 —— Windows 侧更新 real-esrguisoft 英文翻译资产（lupdate + lrelease）
#
# 用法（MSYS2 UCRT64 环境，确保 lupdate/lrelease 6.x 在 PATH）：
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/update-translations.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/update-translations.ps1 -NoRelease
#
# 说明：
#   - 运行 in-place 更新 resources/i18n/app_en.ts（.ts 为翻译源，lrelease 产出 .qm 一并更新）；
#   - 不自动提交；提交前请人工检查 .ts 的 unfinished=0 与占位符；
#   - Linux 侧没有 Qt6 linguist 工具，请在 Windows MSYS2 执行本脚本（详见 docs/design/windows-build-env.md）。
param([switch]$NoRelease)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo
Write-Host "repo: $repo"
$lupdate = Get-Command lupdate -ErrorAction SilentlyContinue
$lrelease = Get-Command lrelease -ErrorAction SilentlyContinue
if (-not $lupdate) { Write-Error 'lupdate 不在 PATH：请使用 MSYS2 UCRT64 环境（export MSYSTEM=UCRT64）'; exit 2 }
Write-Host "lupdate: $(& $lupdate.Source -version 2>&1 | Select-Object -First 1)"
if ($lrelease) { Write-Host "lrelease: $(& $lrelease.Source -version 2>&1 | Select-Object -First 1)" }

Write-Host '== lupdate =='
& $lupdate.Source -no-obsolete src main.cpp -ts resources/i18n/app_en.ts
if ($LASTEXITCODE -ne 0) { Write-Error "lupdate 失败 rc=$LASTEXITCODE"; exit $LASTEXITCODE }
$ts = 'resources/i18n/app_en.ts'
$unfinished = (Select-String -Path $ts -Pattern 'type="unfinished"' -SimpleMatch).Count
Write-Host "unfinished=$unfinished（>0 表示仍有待翻译条目）"
if (-not $NoRelease) {
    if (-not $lrelease) { Write-Error 'lrelease 不在 PATH，无法生成 .qm'; exit 3 }
    Write-Host '== lrelease =='
    & $lrelease.Source -silent $ts -qm resources/i18n/app_en.qm
    if ($LASTEXITCODE -ne 0) { Write-Error "lrelease 失败 rc=$LASTEXITCODE"; exit $LASTEXITCODE }
    Write-Host "qm: $((Get-Item resources/i18n/app_en.qm).Length) bytes"
}
Write-Host '完成：未自动提交，请人工检查后提交 resources/i18n/ 与 resources/resources.qrc'
