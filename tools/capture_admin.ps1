# ============================================================================
# 提权截图工具：在同一个管理员进程内完成「启动 → 截图 → 结束」
#
# 为什么需要：本机的 real-esrguisoft.exe 需要以管理员身份启动才能正常打开；
# 非管理员进程既无法结束提权进程，也会让调用方一直等待。
#
# 用法（外层用 -Verb RunAs 调用本脚本）：
#   powershell.exe -NoProfile -ExecutionPolicy Bypass `
#       -File tools\capture_admin.ps1 -Exe build\real-esrguisoft.exe `
#       -OutDir "$env:TEMP\resr-shots" -Name light-01 -Theme light
# ============================================================================

param(
    [string]$Exe = "",
    [string]$OutDir = "$env:TEMP\resr-shots",
    [string]$Name = "shot",
    [int]$WaitSeconds = 5,
    [ValidateSet("light", "dark", "follow")]
    [string]$Theme = "light"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)) "build\real-esrguisoft.exe"
}
if (-not (Test-Path $Exe)) {
    Write-Error "exe not found: $Exe"
    exit 2
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class ResrShot {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@

[ResrShot]::SetProcessDPIAware() | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 强制主题（ThemeManager 的截图钩子）
$env:RESR_UI_THEME = $Theme

Write-Host "[capture] exe=$Exe"
Write-Host "[capture] theme=$Theme out=$OutDir\$Name.png"

# 结束残留实例（本进程已提权，可以结束提权进程）
Get-Process -Name ([System.IO.Path]::GetFileNameWithoutExtension($Exe)) -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

$process = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds $WaitSeconds

$process.Refresh()
if ($process.HasExited) {
    Write-Error "process exited early: $($process.ExitCode)"
    exit 3
}

$hwnd = $process.MainWindowHandle
for ($i = 0; $i -lt 40 -and $hwnd -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 250
    $process.Refresh()
    $hwnd = $process.MainWindowHandle
}
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Error "no main window"
    $process | Stop-Process -Force
    exit 4
}

[ResrShot]::ShowWindow($hwnd, 9) | Out-Null
[ResrShot]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 800

$rect = New-Object ResrShot+RECT
[ResrShot]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
Write-Host "[capture] window ${w}x${h} at $($rect.Left),$($rect.Top)"

$bmp = New-Object System.Drawing.Bitmap($w, $h)
$graphics = [System.Drawing.Graphics]::FromImage($bmp)
$graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
$path = Join-Path $OutDir "$Name.png"
$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bmp.Dispose()

Write-Host "[capture] saved $path"

$process | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300
Write-Host "[capture] done"
