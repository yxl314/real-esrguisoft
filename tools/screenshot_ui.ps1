# 启动程序并截取主窗口（自检用）
param(
    [string]$Exe = "C:\Users\yxl\Desktop\code\real-esrguisoft\build\real-esrguisoft.exe",
    [string]$Out = "$env:TEMP\ui_shots\shot.png",
    [int]$WaitMs = 2500,
    [switch]$KeepAlive
)

$ErrorActionPreference = "Stop"
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinApi {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$proc = Get-Process -Name (Split-Path $Exe -LeafBase) -ErrorAction SilentlyContinue
if (-not $proc) {
    $proc = Start-Process -FilePath $Exe -PassThru
}
Start-Sleep -Milliseconds $WaitMs

$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) {
    # 窗口可能是无边框 + 延迟显示，轮询几次
    for ($i = 0; $i -lt 20; $i++) {
        Start-Sleep -Milliseconds 250
        $proc.Refresh()
        if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $hwnd = $proc.MainWindowHandle; break }
    }
}
if ($hwnd -eq [IntPtr]::Zero) { throw "找不到主窗口句柄（进程未启动或已退出）" }

[void][WinApi]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 500

$rect = New-Object WinApi+RECT
[void][WinApi]::GetWindowRect($hwnd, [ref]$rect)
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
if ($w -le 0 -or $h -le 0) { throw "窗口尺寸异常: ${w}x${h}" }

$dir = Split-Path $Out -Parent
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }

$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()

Write-Output "saved=$Out size=${w}x${h} pid=$($proc.Id)"

if (-not $KeepAlive) {
    $proc.CloseMainWindow() | Out-Null
    Start-Sleep -Milliseconds 300
    if (-not $proc.HasExited) { $proc.Kill() }
}
