# Builds a portable uVim ZIP archive for Windows x64 (the Windows counterpart
# of scripts/build_portable.sh): uvim.exe, themes, README, license, a
# dependency note and a SHA-256 checksum.
#
#   ./scripts/build_portable.ps1 [-Profile full] [-Version v0.3.0]
#                                [-BuildDir build-portable] [-OutputDir dist]
#                                [-Jobs N] [-Force]
param(
    [ValidateSet("vi-real", "vi-min", "minimal", "basic", "full")]
    [Alias("Profile")]
    [string]$BuildProfile = "full",
    [string]$Version = "",
    [string]$BuildDir = "build-portable",
    [string]$OutputDir = "dist",
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param([string]$Name, [scriptblock]$Command)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot
New-Item -ItemType Directory -Force $BuildDir, $OutputDir | Out-Null
$BuildDir = (Resolve-Path $BuildDir).Path
$OutputDir = (Resolve-Path $OutputDir).Path

Write-Host "==> Bootstrapping uvim-config"
Invoke-Checked "cmake (bootstrap)" {
    cmake -S $RepoRoot -B $BuildDir -A x64 -DUVIM_BOOTSTRAP_CONFIG_ONLY=ON
}
Invoke-Checked "cmake --build uvim-config" {
    cmake --build $BuildDir --config Release --target uvim-config --parallel $Jobs
}
$UvimConfig = Join-Path $BuildDir "Release/uvim-config.exe"
if (!(Test-Path $UvimConfig)) { throw "uvim-config was not built" }

$CacheFile = Join-Path $BuildDir "portable_cache.cmake"
Write-Host "==> Generating portable $BuildProfile configuration"
Invoke-Checked "uvim-config" {
    & $UvimConfig --preset $BuildProfile --config Release --platform WIN32 `
        --optimization O2 --jobs $Jobs --source-dir $RepoRoot `
        --build-dir $BuildDir --output $CacheFile `
        --disable tests --disable compile-commands --disable auto-build-number `
        --disable sanitizers --disable debug-logging --disable debug-lsp `
        --enable lto --enable gc-sections --enable static-link
}

Write-Host "==> Building uVim"
Invoke-Checked "cmake (configure)" {
    cmake -C $CacheFile -S $RepoRoot -B $BuildDir -A x64 `
        -DUVIM_BOOTSTRAP_CONFIG_ONLY=OFF
}
Invoke-Checked "cmake --build uvim" {
    cmake --build $BuildDir --config Release --target uvim --parallel $Jobs
}
$Binary = Join-Path $BuildDir "Release/uvim.exe"
if (!(Test-Path $Binary)) { throw "expected binary is missing: $Binary" }

$DetectedVersion = "v" + ((& $Binary --version | Select-Object -First 1) -split " ")[1]
$PublicVersion = $Version
if ($PublicVersion -eq "") { $PublicVersion = $DetectedVersion }
if (!$PublicVersion.StartsWith("v")) { $PublicVersion = "v$PublicVersion" }
# A release tag must name the version the binary was built as.
if ($PublicVersion -ne $DetectedVersion) {
    throw "version $PublicVersion does not match the built uvim " +
          "($DetectedVersion); bump the version in CMakeLists.txt"
}
if ($PublicVersion -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+$') {
    throw "version must use vMAJOR.MINOR.BUGFIX: $PublicVersion"
}

$BundleName = "uvim-$PublicVersion-windows-x86_64"
$Archive = Join-Path $OutputDir "$BundleName.zip"
$Checksum = "$Archive.sha256"
if ((Test-Path $Archive) -or (Test-Path $Checksum)) {
    if (!$Force) { throw "output already exists; pass -Force to replace it: $Archive" }
    Remove-Item -Force -ErrorAction SilentlyContinue $Archive, $Checksum
}

$Stage = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString())
$Bundle = Join-Path $Stage $BundleName
New-Item -ItemType Directory -Force $Bundle | Out-Null
try {
    Copy-Item $Binary (Join-Path $Bundle "uvim.exe")
    Copy-Item (Join-Path $RepoRoot "LICENSE"), (Join-Path $RepoRoot "README.md") $Bundle
    Copy-Item -Recurse (Join-Path $RepoRoot "themes") (Join-Path $Bundle "themes")
    @"
uvim.exe is linked with the static MSVC runtime; it needs no Visual C++
redistributable. It uses only Windows system DLLs.
"@ | Set-Content -Encoding utf8 (Join-Path $Bundle "DEPENDENCIES.txt")
    @"
uVim $PublicVersion portable build
Platform: windows
Architecture: x86_64
Profile: $BuildProfile

Run directly:
  .\uvim.exe [file]

Optional system programs are discovered through PATH and are not bundled:
Git, ripgrep/fzf, formatters, clipboard helpers, and language servers.

Generate a user configuration and copy the bundled themes with:
  .\uvim.exe --init-config
"@ | Set-Content -Encoding utf8 (Join-Path $Bundle "PORTABLE.txt")

    Write-Host "==> Creating $Archive"
    Compress-Archive -Path $Bundle -DestinationPath $Archive
    $Hash = (Get-FileHash -Algorithm SHA256 $Archive).Hash.ToLower()
    # LF line ending, like sha256sum/shasum, so `shasum -c` works anywhere.
    [IO.File]::WriteAllText($Checksum, "$Hash  $(Split-Path -Leaf $Archive)`n")
}
finally {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $Stage
}

Write-Host "==> Wrote $Archive"
Write-Host "==> Wrote $Checksum"
