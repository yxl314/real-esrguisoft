# 启动 real-esrguisoft 并截取主窗口（UI 自检 / 视觉回归用）
#
# 用法：
#   & tools\capture_ui.ps1 -Out "$env:TEMP\ui_shots\light.png" -Theme light -Page image
#   & tools\capture_ui.ps1 -Out "...\dark.png" -Theme dark -Page settings
#
# 实现要点：
#   - 主题 / 页面通过环境变量 RESR_UI_THEME / RESR_UI_PAGE 在进程内生效
#     （MainWindow / ThemeManager 的自检钩子，不依赖前台窗口与键盘注入）；
#   - 启动时重定向 stdio，避免 GUI 进程继承调用方管道导致卡死；
#   - 截图用 GetWindowRect + CopyFromScreen（进程内不 grab，避开第三方
#     Overlay DLL 在 Qt 退出 / 抓图路径上的崩溃）；
#   - 截完直接 TerminateProcess，不做优雅退出。
#
# 注意：单实例锁目录 build/singleproc 下的陈旧 lock 会导致进程判定为「二次启动」
#       而直接退出，脚本启动前会清掉它。

param(
    [string]$Exe = "C:\Users\yxl\Desktop\code\real-esrguisoft\build\real-esrguisoft.exe",
    [string]$Out = "$env:TEMP\ui_shots\shot.png",
    [ValidateSet("follow", "light", "dark")]
    [string]$Theme = "light",
    [ValidateSet("image", "video", "settings", "about")]
    [string]$Page = "image",
    [int]$WaitMs = 2600
)

$ErrorActionPreference = "Stop"
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class ResrCaptureApi {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@

[ResrCaptureApi]::SetProcessDPIAware() | Out-Null

if (-not (Test-Path $Exe)) { throw "exe 不存在: $Exe" }
$dir = Split-Path $Out -Parent
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

# 清掉可能残留的实例与陈旧单实例锁
Get-Process -Name ([System.IO.Path]::GetFileNameWithoutExtension($Exe)) -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 250
Get-ChildItem (Join-Path (Split-Path $Exe -Parent) "singleproc") -Filter "*.lock" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

$env:RESR_UI_THEME = $Theme
$env:RESR_UI_PAGE = $Page
Remove-Item Env:RESR_UI_SHOT -ErrorAction SilentlyContinue

$outLog = Join-Path $dir "app_stdout.log"
$errLog = Join-Path $dir "app_stderr.log"
$p = Start-Process -FilePath $Exe -PassThru -RedirectStandardOutput $outLog -RedirectStandardError $errLog
Start-Sleep -Milliseconds $WaitMs

$p.Refresh()
if ($p.HasExited) {
    throw "进程提前退出，exit code=$($p.ExitCode)（检查 $errLog）"
}

$hwnd = $p.MainWindowHandle
for ($i = 0; $i -lt 24 -and $hwnd -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 250
    $p.Refresh()
    $hwnd = $p.MainWindowHandle
}
if ($hwnd -eq [IntPtr]::Zero) {
    $p | Stop-Process -Force
    throw "找不到主窗口句柄"
}

Start-Sleep -Milliseconds 600
$rect = New-Object ResrCaptureApi+RECT
[ResrCaptureApi]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
if ($w -le 0 -or $h -le 0) { $p | Stop-Process -Force; throw "窗口尺寸异常: ${w}x${h}" }

$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()

Write-Host "saved=$Out size=${w}x${h} theme=$Theme page=$Page"

$p | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300
Write-Host "done"
