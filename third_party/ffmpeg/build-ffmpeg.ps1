<#
.SYNOPSIS
    在 Windows / MSYS2 环境下构建内嵌 FFmpeg 静态库。

.DESCRIPTION
    本脚本是 build-ffmpeg.sh 的 PowerShell 包装器：定位 MSYS2 的 bash，
    转到 third_party/ffmpeg 目录后调用同名 shell 脚本完成 configure/make/make install。
    产物目录：third_party/ffmpeg/build-ffmpeg-<版本>-<平台>/install/

    说明：FFmpeg 官方构建系统是 POSIX shell + make，Windows 上必须借助 MSYS2；
    脚本只负责环境发现与参数透传，真正的构建逻辑集中在 build-ffmpeg.sh。

.EXAMPLE
    .\build-ffmpeg.ps1
    .\build-ffmpeg.ps1 -LGPLOnly -Jobs 8
    .\build-ffmpeg.ps1 -Version 9.0.2 -Clean

.NOTES
    许可证：默认启用 x264/x265（GPL），对外分发须遵守 GPL；
    需要 LGPL 分发时使用 -LGPLOnly（H.264/H.265 编码不可用）。
#>
param(
    [string]$Version = "9.0.2",
    [switch]$LGPLOnly,
    [switch]$Clean,
    [int]$Jobs = 0,
    [string]$MsysRoot = "C:\msys64"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$shellScript = Join-Path $scriptDir "build-ffmpeg.sh"
if (-not (Test-Path $shellScript)) {
    throw "找不到构建脚本：$shellScript"
}

$bashPath = Join-Path $MsysRoot "usr\bin\bash.exe"
if (-not (Test-Path $bashPath)) {
    throw "找不到 MSYS2 bash：$bashPath。请先安装 MSYS2，或用 -MsysRoot 指定安装目录。"
}

# Windows 路径 -> MSYS 路径（C:\a\b -> /c/a/b）
function ConvertTo-MsysPath {
    param([string]$WinPath)
    $full = [System.IO.Path]::GetFullPath($WinPath)
    $drive = $full.Substring(0, 1).ToLower()
    $rest = $full.Substring(2).Replace("\", "/")
    return "/$drive$rest"
}

$msysScriptDir = ConvertTo-MsysPath $scriptDir
$arguments = @("--version", $Version)
if ($LGPLOnly) { $arguments += "--lgpl-only" }
if ($Clean) { $arguments += "--clean" }
if ($Jobs -gt 0) { $arguments += @("--jobs", "$Jobs") }
$argumentLine = ($arguments | ForEach-Object { "'" + $_ + "'" }) -join " "

# UCRT64 工具链由 build-ffmpeg.sh 内部处理（自动设置 MSYSTEM=UCRT64 与 PATH）。
# 显式用 MSYS2 的 bash 执行脚本，避免 Windows PATH 里的其它 bash 抢先命中。
$command = "cd '${msysScriptDir}' && /usr/bin/bash ./build-ffmpeg.sh ${argumentLine}"

Write-Host "调用：$bashPath -lc `"$command`""
& $bashPath -lc $command
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg 构建失败（退出码 $LASTEXITCODE）"
}
