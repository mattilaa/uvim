$ErrorActionPreference = "Stop"

$BuildDir = "build"
$Jobs = ""

function Show-Usage {
    @"
Usage: ./bootstrap.ps1 [--build-dir DIR] [--jobs N]

Options:
  --build-dir DIR    CMake build directory (default: build)
  --build-dir=DIR    Same as --build-dir DIR
  -j, --jobs N       Parallel build jobs
  --jobs=N           Same as --jobs N
  -h, --help         Show this help
"@
}

function Need-Value($ArgsList, $Index, $Option) {
    if ($Index + 1 -ge $ArgsList.Count) {
        Write-Error "bootstrap.ps1: $Option requires a value"
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

$i = 0
while ($i -lt $args.Count) {
    $arg = $args[$i]
    switch -Regex ($arg) {
        '^(-h|--help)$' {
            Show-Usage
            exit 0
        }
        '^--build-dir$' {
            $BuildDir = Need-Value $args $i $arg
            $i += 2
            continue
        }
        '^--build-dir=.+$' {
            $BuildDir = $arg.Substring("--build-dir=".Length)
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
        default {
            Write-Error "bootstrap.ps1: unknown option: $arg"
            Show-Usage | Write-Error
            exit 2
        }
    }
}

$ConfigFile = Join-Path $BuildDir "uvim-config.conf"
if ([string]::IsNullOrEmpty($Jobs) -and (Test-Path -LiteralPath $ConfigFile)) {
    $Jobs = Get-ConfigValue $ConfigFile "jobs"
}

cmake -S . -B $BuildDir
if (![string]::IsNullOrEmpty($Jobs)) {
    Write-Host "cmake --build $BuildDir --target uvim-config --parallel $Jobs"
    cmake --build $BuildDir --target uvim-config --parallel $Jobs
} else {
    Write-Host "cmake --build $BuildDir --target uvim-config"
    cmake --build $BuildDir --target uvim-config
}
