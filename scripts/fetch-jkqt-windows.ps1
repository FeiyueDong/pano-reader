#Requires -Version 5.1
<#
.SYNOPSIS
    用 CMake + MSVC 构建 JKQTMathText / JKQTCommon 静态库，并安装到 build\deps\jkqt\usr。

.DESCRIPTION
    正式笔记模块 src\note\MarkdownRenderer.cpp 直接包含 <jkqtmathtext/jkqtmathtext.h>。
    Linux 上由发行版包（Arch: jkqtplotter）提供，Windows 上没有对应包，因此本脚本：

      1. 下载固定版本的 JKQtPlotter 源码（默认钉住一个 commit）；
      2. 只构建项目需要的两个静态库 JKQTCommon + JKQTMathText；
      3. 安装头文件和 .lib 到 build\deps\jkqt\usr，即 src\xmake.lua 默认查找的位置。

    安装后的布局（被 src/xmake.lua 的 jkqt_root 使用）：

        build\deps\jkqt\usr\include\jkqtmathtext\jkqtmathtext.h
        build\deps\jkqt\usr\include\jkqtcommon\...
        build\deps\jkqt\usr\lib\JKQTMathText6_RelWithDebInfo.lib
        build\deps\jkqt\usr\lib\JKQTCommon6_RelWithDebInfo.lib

    文件名里的 _RelWithDebInfo 来自 JKQtPlotter_BUILD_DECORATE_LIBNAMES_WITH_BUILDTYPE，
    与 Linux 发行版打包一致，因此 src/xmake.lua 里一套库名在两个平台都成立。

    脚本可以重复执行：源码或安装结果已存在且版本一致时跳过下载，-Force 强制重来。

.PARAMETER QtDir
    Qt SDK 目录，即传给 xmake 的 --qt 值。省略时依次取 $env:PANO_QT_PREFIX、
    .xmake\<plat>\<arch>\xmake.conf 中的 qt 配置项。

.PARAMETER Ref
    JKQtPlotter 的 git ref（tag / 分支 / commit）。

.PARAMETER BuildType
    CMake 构建类型，默认 RelWithDebInfo（决定库名后缀，需与 xmake 链接名一致）。

.PARAMETER Prefix
    安装前缀，默认 <仓库>\build\deps\jkqt\usr。

.PARAMETER Force
    忽略已有源码与安装结果，重新下载并构建。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\fetch-jkqt-windows.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\fetch-jkqt-windows.ps1 `
        -QtDir C:\Qt\6.12.0\msvc2022_64 -Force
#>
[CmdletBinding()]
param(
    [string]$QtDir,
    [string]$Ref = "9e9f0af8b140192e3de7ff866a0013e7da5e1e93",
    [string]$BuildType = "RelWithDebInfo",
    [string]$Prefix,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Stop-Fail {
    param([string]$Message)
    Write-Host ""
    Write-Host "错误: $Message" -ForegroundColor Red
    exit 1
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $repoRoot) { Stop-Fail "无法定位仓库根目录（脚本应位于 <仓库>\scripts 下）" }

$workDir  = Join-Path $repoRoot "build\deps\jkqt"
$srcDir   = Join-Path $workDir "src"
$buildDir = Join-Path $workDir "build"
if (-not $Prefix) { $Prefix = Join-Path $workDir "usr" }
if (-not (Test-Path -LiteralPath $workDir)) {
    New-Item -ItemType Directory -Path $workDir -Force | Out-Null
}

function Resolve-QtDir {
    param([string]$Explicit)

    if ($Explicit) {
        if (-not (Test-Path -LiteralPath $Explicit)) { Stop-Fail "指定的 Qt 目录不存在: $Explicit" }
        return (Resolve-Path -LiteralPath $Explicit).Path
    }
    if ($env:PANO_QT_PREFIX) {
        if (-not (Test-Path -LiteralPath $env:PANO_QT_PREFIX)) {
            Stop-Fail "PANO_QT_PREFIX 指向的目录不存在: $env:PANO_QT_PREFIX"
        }
        return (Resolve-Path -LiteralPath $env:PANO_QT_PREFIX).Path
    }

    $configDir = Join-Path $repoRoot ".xmake"
    if (Test-Path -LiteralPath $configDir) {
        $confs = Get-ChildItem -LiteralPath $configDir -Recurse -Depth 2 -Filter "xmake.conf" -File -ErrorAction SilentlyContinue
        foreach ($conf in $confs) {
            $hit = Select-String -LiteralPath $conf.FullName -Pattern '^\s*qt\s*=\s*"([^"]+)"' -ErrorAction SilentlyContinue
            if ($hit) {
                $candidate = $hit.Matches[0].Groups[1].Value
                if (Test-Path -LiteralPath $candidate) {
                    Write-Host "从 $($conf.FullName) 读取 Qt 目录: $candidate"
                    return $candidate
                }
            }
        }
    }
    return $null
}

function Find-VcVars {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($installPath) {
            $candidate = Join-Path $installPath.Trim() "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    foreach ($root in @("${env:ProgramFiles}\Microsoft Visual Studio", "C:\Programing\Microsoft Visual Studio")) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $hit = Get-ChildItem -LiteralPath $root -Recurse -Depth 6 -Filter "vcvars64.bat" -File -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

function Import-MsvcEnvironment {
    param([string]$VcVars)

    $lines = & cmd.exe /c "`"$VcVars`" >NUL 2>&1 && set"
    if ($LASTEXITCODE -ne 0 -or -not $lines) { Stop-Fail "调用失败: $VcVars" }
    foreach ($line in $lines) {
        $idx = $line.IndexOf("=")
        if ($idx -gt 0) {
            Set-Item -Path ("env:" + $line.Substring(0, $idx)) -Value $line.Substring($idx + 1)
        }
    }
}

function Get-JkqtSource {
    $stamp = Join-Path $srcDir ".jkqt-ref"
    if (-not $Force -and (Test-Path -LiteralPath $stamp)) {
        $current = (Get-Content -LiteralPath $stamp -TotalCount 1).Trim()
        if ($current -eq $Ref) {
            Write-Host "复用已下载源码: $srcDir ($Ref)"
            return
        }
    }

    $url = "https://codeload.github.com/jkriege2/JKQtPlotter/tar.gz/$Ref"
    $tarball = Join-Path $workDir "JKQtPlotter.tar.gz"
    $staging = Join-Path $workDir "staging"

    Write-Step "下载 $url"
    try {
        Invoke-WebRequest -Uri $url -OutFile $tarball -UseBasicParsing
    }
    catch {
        Stop-Fail "下载失败: $($_.Exception.Message)`n可以手动下载 $url 后解压到 $srcDir，再重新运行本脚本。"
    }

    Write-Step "解压源码"
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $staging -Force | Out-Null
    & tar.exe -xzf $tarball -C $staging
    if ($LASTEXITCODE -ne 0) { Stop-Fail "解压失败（需要 Windows 10 自带的 tar.exe）: $tarball" }

    $inner = Get-ChildItem -LiteralPath $staging -Directory | Select-Object -First 1
    if (-not $inner) { Stop-Fail "压缩包结构异常: $tarball" }

    Remove-Item -LiteralPath $srcDir -Recurse -Force -ErrorAction SilentlyContinue
    Move-Item -LiteralPath $inner.FullName -Destination $srcDir
    Set-Content -LiteralPath (Join-Path $srcDir ".jkqt-ref") -Value $Ref -Encoding ASCII
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $tarball -Force -ErrorAction SilentlyContinue
}

$expectedLibs = @(
    (Join-Path $Prefix ("lib\JKQTMathText6_{0}.lib" -f $BuildType)),
    (Join-Path $Prefix ("lib\JKQTCommon6_{0}.lib" -f $BuildType))
)
$expectedHeader = Join-Path $Prefix "include\jkqtmathtext\jkqtmathtext.h"

$alreadyInstalled = (Test-Path -LiteralPath $expectedHeader) -and
    (@($expectedLibs | Where-Object { -not (Test-Path -LiteralPath $_) }).Count -eq 0)
if ($alreadyInstalled -and -not $Force) {
    Write-Host ""
    Write-Host "JKQT 已安装: $Prefix" -ForegroundColor Green
    Write-Host "如需重建，加 -Force。"
    exit 0
}

if (-not $QtDir) { $QtDir = Resolve-QtDir -Explicit $null }
if (-not $QtDir) {
    Stop-Fail "找不到 Qt 目录。请用 -QtDir 指定，或先执行 xmake f --qt=<Qt 路径>，或设置 PANO_QT_PREFIX。"
}
if (-not (Test-Path -LiteralPath (Join-Path $QtDir "lib\cmake\Qt6\Qt6Config.cmake"))) {
    Stop-Fail "该目录不像 Qt6 SDK（缺少 lib\cmake\Qt6\Qt6Config.cmake）: $QtDir"
}

$cmakeCmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmakeCmd) { Stop-Fail "PATH 中找不到 cmake.exe，请先安装 CMake 3.23 以上版本。" }

$vcvars = Find-VcVars
if (-not $vcvars) { Stop-Fail "找不到 MSVC 的 vcvars64.bat，请安装 Visual Studio 的 C++ 桌面开发工作负载。" }

Write-Step "载入 MSVC 环境"
Write-Host "vcvars64.bat: $vcvars"
Import-MsvcEnvironment -VcVars $vcvars

$generatorArgs = @()
$ninjaCmd = Get-Command ninja.exe -ErrorAction SilentlyContinue
if ($ninjaCmd) {
    $generatorArgs = @("-G", "Ninja")
} else {
    Write-Host "未找到 ninja，改用 NMake Makefiles（编译会慢一些）。"
    $generatorArgs = @("-G", "NMake Makefiles")
}

Get-JkqtSource

Write-Step "配置 CMake"
Write-Host "源码: $srcDir"
Write-Host "构建: $buildDir"
Write-Host "安装: $Prefix"
Write-Host "Qt  : $QtDir"

$configureArgs = @(
    "-S", $srcDir,
    "-B", $buildDir,
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_PREFIX_PATH=$QtDir",
    "-DCMAKE_INSTALL_PREFIX=$Prefix",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DJKQtPlotter_BUILD_EXAMPLES=OFF",
    "-DJKQtPlotter_BUILD_TESTS=OFF",
    "-DJKQtPlotter_BUILD_TOOLS=OFF",
    "-DJKQtPlotter_BUILD_LIB_JKQTPLOTTER=OFF",
    "-DJKQtPlotter_BUILD_LIB_JKQTFASTPLOTTER=OFF",
    "-DJKQtPlotter_BUILD_LIB_JKQTMATH=OFF",
    "-DJKQtPlotter_BUILD_LIB_JKQTMATHTEXT=ON",
    "-DJKQtPlotter_BUILD_DECORATE_LIBNAMES_WITH_BUILDTYPE=ON",
    "-DJKQtPlotter_BUILD_WITH_PRECOMPILED_HEADERS=OFF"
) + $generatorArgs

& cmake.exe @configureArgs
if ($LASTEXITCODE -ne 0) { Stop-Fail "CMake 配置失败。" }

Write-Step "编译 JKQTCommon + JKQTMathText"
& cmake.exe --build $buildDir --target JKQTCommon6 JKQTMathText6 --config $BuildType --parallel
if ($LASTEXITCODE -ne 0) { Stop-Fail "编译失败。" }

Write-Step "安装到 $Prefix"
& cmake.exe --install $buildDir --config $BuildType
if ($LASTEXITCODE -ne 0) { Stop-Fail "安装失败。" }

Write-Step "校验安装结果"
$missing = @()
if (-not (Test-Path -LiteralPath $expectedHeader)) { $missing += $expectedHeader }
foreach ($lib in $expectedLibs) {
    if (-not (Test-Path -LiteralPath $lib)) { $missing += $lib }
}
if ($missing.Count -gt 0) {
    Stop-Fail ("缺少预期文件:`n  " + ($missing -join "`n  ") + "`n请检查 CMake 输出中的库名是否被改过。")
}

Write-Host ""
Write-Host "JKQT 安装完成:" -ForegroundColor Green
Write-Host "  $Prefix"
foreach ($lib in $expectedLibs) { Write-Host "  $lib" }
Write-Host ""
Write-Host "接下来构建 PANO:"
Write-Host "  xmake f -c -p windows -a x64 -m release --qt=`"$QtDir`""
Write-Host "  xmake -j4"
Write-Host ""
Write-Host "如需装到别处，设置 JKQT_ROOT 指向包含 include\ 和 lib\ 的前缀目录。"
