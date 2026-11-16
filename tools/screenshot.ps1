param(
    [string]$Exe = "C:\Users\yxl\Desktop\code\real-esrguisoft\build\real-esrguisoft.exe",
    [string]$OutDir = "$env:TEMP\resr-shots",
    [int]$WaitSeconds = 4,
    [string]$Name = "shot"
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Shot {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@

[Win32Shot]::SetProcessDPIAware() | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

if (-not (Test-Path $Exe)) {
    Write-Error "exe not found: $Exe"
    exit 2
}

# 清理同进程
Get-Process -Name ([System.IO.Path]::GetFileNameWithoutExtension($Exe)) -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

$p = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds $WaitSeconds
$p.Refresh()
if ($p.HasExited) {
    Write-Error "process exited early with code $($p.ExitCode)"
    exit 3
}

$hwnd = $p.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) {
    for ($i = 0; $i -lt 20 -and $hwnd -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 250
        $p.Refresh()
        $hwnd = $p.MainWindowHandle
    }
}
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Error "no main window handle"
    $p | Stop-Process -Force
    exit 4
}

[Win32Shot]::ShowWindow($hwnd, 9) | Out-Null   # SW_RESTORE
[Win32Shot]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 600

$rect = New-Object Win32Shot+RECT
[Win32Shot]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
Write-Host "window rect: $($rect.Left),$($rect.Top) ${w}x${h}"

$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
$path = Join-Path $OutDir "$Name.png"
$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()

Write-Host "saved: $path"

$p | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300
Write-Host "done"
