$ErrorActionPreference = "Stop"

$SourceDir = "."
$BuildDir = "build"
$ConfigFile = ""
$Target = ""
$Jobs = ""
$Install = ""
$InstallDir = ""

function Show-Usage {
    @"
Usage: ./build.ps1 [options]

Options:
  -S, --source-dir DIR    Source directory (default: .)
  -B, --build-dir DIR     CMake build directory (default: build)
      --config-file FILE  uvim-config.conf to import (default: BUILD_DIR/uvim-config.conf)
  -j, --jobs N            Override saved parallel build jobs
      --target NAME       Build a specific CMake target
  -i, --install           Install after build
      --no-install        Do not install after build
  -h, --help              Show this help
"@
}

function Need-Value($ArgsList, $Index, $Option) {
    if ($Index + 1 -ge $ArgsList.Count) {
        Write-Error "build.ps1: $Option requires a value"
        exit 2
    }
    return $ArgsList[$Index + 1]
}

function Get-ConfigValue($Path, $Key) {
    if (!(Test-Path -LiteralPath $Path)) {
        return ""
    }

    $prefix = "$Key="
    $match = Get-Content -LiteralPath $Path |
        Where-Object { $_.StartsWith($prefix) } |
        Select-Object -Last 1
    if ($null -eq $match) {
        return ""
    }
    return $match.Substring($prefix.Length)
}

function Resolve-UvimConfig($Directory) {
    $exe = Join-Path $Directory "uvim-config.exe"
    if (Test-Path -LiteralPath $exe) {
        return $exe
    }

    $plain = Join-Path $Directory "uvim-config"
    if (Test-Path -LiteralPath $plain) {
        return $plain
    }

    return ""
}

$i = 0
while ($i -lt $args.Count) {
    $arg = $args[$i]
    switch -Regex ($arg) {
        '^(-h|--help)$' {
            Show-Usage
            exit 0
        }
        '^(-S|--source-dir)$' {
            $SourceDir = Need-Value $args $i $arg
            $i += 2
            continue
        }
        '^--source-dir=.+$' {
            $SourceDir = $arg.Substring("--source-dir=".Length)
            $i += 1
            continue
        }
        '^(-B|--build-dir)$' {
            $BuildDir = Need-Value $args $i $arg
            $i += 2
            continue
        }
        '^--build-dir=.+$' {
            $BuildDir = $arg.Substring("--build-dir=".Length)
            $i += 1
            continue
        }
        '^--config-file$' {
            $ConfigFile = Need-Value $args $i $arg
            $i += 2
            continue
        }
        '^--config-file=.+$' {
            $ConfigFile = $arg.Substring("--config-file=".Length)
            $i += 1
            continue
        }
        '^(-j|--jobs)$' {
            $Jobs = Need-Value $args $i $arg
            $i += 2
            continue
        }
        '^--jobs=.+$' {
            $Jobs = $arg.Substring("--jobs=".Length)
            $i += 1
            continue
        }
        '^--target$' {
            $Target = Need-Value $args $i $arg
            $i += 2
            continue
        }
        '^--target=.+$' {
            $Target = $arg.Substring("--target=".Length)
            $i += 1
            continue
        }
        '^(-i|--install)$' {
            $Install = "true"
            $i += 1
            continue
        }
        '^--no-install$' {
            $Install = "false"
            $i += 1
            continue
        }
        default {
            Write-Error "build.ps1: unknown option: $arg"
            Show-Usage | Write-Error
            exit 2
        }
    }
}

if ([string]::IsNullOrEmpty($ConfigFile)) {
    $ConfigFile = Join-Path $BuildDir "uvim-config.conf"
}

if (!(Test-Path -LiteralPath $ConfigFile)) {
    Write-Error "build.ps1: missing config file: $ConfigFile"
    Write-Error "Run ./bootstrap.ps1 and ./build/uvim-config first, or pass --config-file."
    exit 1
}

$UvimConfig = Resolve-UvimConfig $BuildDir
if ([string]::IsNullOrEmpty($UvimConfig)) {
    Write-Host "build.ps1: uvim-config is missing; bootstrapping uvim-config first"
    & (Join-Path "." "bootstrap.ps1") --build-dir $BuildDir
    $UvimConfig = Resolve-UvimConfig $BuildDir
}
if ([string]::IsNullOrEmpty($UvimConfig)) {
    Write-Error "build.ps1: cannot find uvim-config in $BuildDir after bootstrap"
    exit 1
}

$CacheFile = Join-Path $BuildDir "uvim_config_cache.cmake"

if ([string]::IsNullOrEmpty($Jobs)) {
    $Jobs = Get-ConfigValue $ConfigFile "jobs"
}

$InstallDir = Get-ConfigValue $ConfigFile "install_dir"
if ([string]::IsNullOrEmpty($InstallDir)) {
    $homeDir = if (![string]::IsNullOrEmpty($env:USERPROFILE)) { $env:USERPROFILE } else { $HOME }
    $InstallDir = Join-Path $homeDir ".local\bin"
}

if ([string]::IsNullOrEmpty($Install)) {
    $Install = Get-ConfigValue $ConfigFile "install_after_build"
}

Write-Host "$UvimConfig --import $ConfigFile --source-dir $SourceDir --build-dir $BuildDir --install-dir $InstallDir --output $CacheFile"
& $UvimConfig --import $ConfigFile --source-dir $SourceDir --build-dir $BuildDir --install-dir $InstallDir --output $CacheFile

Write-Host "cmake -C $CacheFile -S $SourceDir -B $BuildDir"
cmake -C $CacheFile -S $SourceDir -B $BuildDir

$BuildArgs = @("--build", $BuildDir)
$BuildCommand = "cmake --build $BuildDir"
if (![string]::IsNullOrEmpty($Target)) {
    $BuildArgs += @("--target", $Target)
    $BuildCommand += " --target $Target"
}
if (![string]::IsNullOrEmpty($Jobs)) {
    $BuildArgs += @("--parallel", $Jobs)
    $BuildCommand += " --parallel $Jobs"
}
Write-Host $BuildCommand
cmake @BuildArgs

if ($Install -eq "true" -or $Install -eq "ON" -or $Install -eq "1") {
    Write-Host "cmake --install $BuildDir --component uvim"
    Write-Host "install destination: $InstallDir"
    cmake --install $BuildDir --component uvim
}
