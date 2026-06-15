param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

function Build-Arch {
    param(
        [string]$Arch,
        [string]$GeneratorPlatform
    )

    $BuildDir = Join-Path $Root "build\$Arch"
    Write-Host "=== Building $Arch ($Configuration) ===" -ForegroundColor Cyan

    cmake -S $Root -B $BuildDir -A $GeneratorPlatform -DCMAKE_BUILD_TYPE=$Configuration
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    cmake --build $BuildDir --config $Configuration
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $OutDir = Join-Path $Root "out\$Arch"
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

    Copy-Item (Join-Path $BuildDir "bin\$Configuration\GraphicsCapture.dll") $OutDir -Force
    Copy-Item (Join-Path $BuildDir "lib\$Configuration\GraphicsCapture.lib") $OutDir -Force
    Copy-Item (Join-Path $BuildDir "bin\$Configuration\CaptureGw2.exe") $OutDir -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $BuildDir "bin\$Configuration\TestPrintWindow.exe") $OutDir -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $BuildDir "bin\$Configuration\TestDesktopDup.exe") $OutDir -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $Root "include\GraphicsCapture.h") $OutDir -Force

    Write-Host "Output: $OutDir" -ForegroundColor Green
}

Build-Arch -Arch "x64" -GeneratorPlatform "x64"
Build-Arch -Arch "x86" -GeneratorPlatform "Win32"

Write-Host "`nDone. Artifacts:" -ForegroundColor Green
Write-Host "  out\x64\GraphicsCapture.dll"
Write-Host "  out\x86\GraphicsCapture.dll"
