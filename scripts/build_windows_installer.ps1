# Builds the uVim Windows installer (uvim-v<version>-windows-x86_64-setup.exe)
# with Inno Setup from an already built uvim.exe, e.g. the one from
# scripts/build_portable.ps1:
#
#   ./scripts/build_portable.ps1 -Version v0.3.2
#   ./scripts/build_windows_installer.ps1 -Version v0.3.2
#
# Needs Inno Setup 6 (winget install JRSoftware.InnoSetup, or
# choco install innosetup); -InstallInnoSetup installs it with Chocolatey.
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$Binary = "build-portable/Release/uvim.exe",
    [string]$OutputDir = "dist",
    [switch]$InstallInnoSetup
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

$AppVersion = $Version.TrimStart("v")
if ($AppVersion -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
    throw "version must use vMAJOR.MINOR.BUGFIX: $Version"
}
if (!(Test-Path $Binary)) { throw "uvim.exe not found: $Binary (build it first)" }
$Binary = (Resolve-Path $Binary).Path
$BinaryVersion = ((& $Binary --version | Select-Object -First 1) -split " ")[1]
if ($BinaryVersion -ne $AppVersion) {
    throw "version v$AppVersion does not match the binary (v$BinaryVersion)"
}
New-Item -ItemType Directory -Force $OutputDir | Out-Null
$OutputDir = (Resolve-Path $OutputDir).Path

function Find-Iscc {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles,
                        (Join-Path $env:LOCALAPPDATA "Programs"))) {
        if (!$base) { continue }
        $candidate = Join-Path $base "Inno Setup 6\ISCC.exe"
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

$Iscc = Find-Iscc
if (!$Iscc -and $InstallInnoSetup) {
    Write-Host "==> Installing Inno Setup"
    choco install innosetup --yes --no-progress
    if ($LASTEXITCODE -ne 0) { throw "choco install innosetup failed" }
    $Iscc = Find-Iscc
}
if (!$Iscc) {
    throw "Inno Setup 6 (ISCC.exe) not found; install it with " +
          "'winget install JRSoftware.InnoSetup' or pass -InstallInnoSetup"
}

Write-Host "==> Building installer with $Iscc"
& $Iscc /Q "/DAppVersion=$AppVersion" "/DSourceDir=$RepoRoot" `
    "/DBinary=$Binary" "/DOutputDir=$OutputDir" `
    (Join-Path $RepoRoot "packaging\windows\uvim.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

$Installer = Join-Path $OutputDir "uvim-v$AppVersion-windows-x86_64-setup.exe"
if (!(Test-Path $Installer)) { throw "installer was not created: $Installer" }
$Hash = (Get-FileHash -Algorithm SHA256 $Installer).Hash.ToLower()
# LF line ending, like sha256sum/shasum, so `shasum -c` works anywhere.
[IO.File]::WriteAllText("$Installer.sha256", "$Hash  $(Split-Path -Leaf $Installer)`n")

Write-Host "==> Wrote $Installer"
Write-Host "==> Wrote $Installer.sha256"
