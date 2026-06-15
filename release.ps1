param(
    [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

& (Join-Path $Root "build.ps1")

$Dist = Join-Path $Root "dist"
Remove-Item $Dist -Recurse -Force -ErrorAction SilentlyContinue

foreach ($arch in @("x86", "x64")) {
    $src = Join-Path $Root "out\$arch"
    $pkg = Join-Path $Dist "WgcCapture-$Version-$arch"
    New-Item -ItemType Directory -Force -Path $pkg | Out-Null

    foreach ($f in @(
        "GraphicsCapture.dll", "GraphicsCapture.lib", "GraphicsCapture.h",
        "CaptureGw2.exe", "TestPrintWindow.exe", "TestDesktopDup.exe",
        "GCSessionExample.exe", "GCExample.exe"
    )) {
        Copy-Item (Join-Path $src $f) $pkg -Force
    }
    Copy-Item (Join-Path $Root "docs\API.md") $pkg -Force
    Copy-Item (Join-Path $Root "docs\易语言声明.md") $pkg -Force

    $zip = Join-Path $Dist "WgcCapture-$Version-$arch.zip"
    Compress-Archive -Path (Join-Path $pkg "*") -DestinationPath $zip -Force
}

Write-Host "Release packages:" -ForegroundColor Green
Get-ChildItem $Dist -Filter "*.zip" | ForEach-Object { Write-Host "  $($_.FullName)" }
