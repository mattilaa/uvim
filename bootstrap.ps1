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

function Test-Truthy($Value) {
    return $Value -eq "ON" -or $Value -eq "true" -or $Value -eq "yes" -or $Value -eq "1"
}

function Test-NinjaAvailable {
    return $null -ne (Get-Command ninja -ErrorAction SilentlyContinue)
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

function Confirm-YesNo($Prompt) {
    while ($true) {
        Write-Host -NoNewline "$Prompt "
        $answer = [Console]::In.ReadLine()
        if ($null -eq $answer) {
            return $false
        }
        if ($answer -eq "y" -or $answer -eq "Y") {
            return $true
        }
        if ($answer -eq "n" -or $answer -eq "N") {
            return $false
        }
    }
}

function Stop-IfNativeCommandFailed($CommandName) {
    if ($LASTEXITCODE -ne 0) {
        Write-Error "$CommandName failed with exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }
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

$UseNinja = $false
if (Test-NinjaAvailable) {
    if (Test-Path -LiteralPath $ConfigFile) {
        $NinjaConfig = Get-ConfigValue $ConfigFile "ninja_generator"
        $UseNinja = [string]::IsNullOrEmpty($NinjaConfig) -or (Test-Truthy $NinjaConfig)
    } else {
        $UseNinja = $true
    }
}

$ConfigureArgs = @("-S", ".", "-B", $BuildDir, "-DUVIM_BOOTSTRAP_CONFIG_ONLY=ON", "-DCMAKE_BUILD_TYPE=Release")
$ConfigureCommand = "cmake -S . -B $BuildDir -DUVIM_BOOTSTRAP_CONFIG_ONLY=ON -DCMAKE_BUILD_TYPE=Release"
if ($UseNinja) {
    $ConfigureArgs += @("-G", "Ninja")
    $ConfigureCommand += " -G Ninja"
}
Write-Host $ConfigureCommand
cmake @ConfigureArgs
Stop-IfNativeCommandFailed "cmake configure"
if (![string]::IsNullOrEmpty($Jobs)) {
    Write-Host "cmake --build $BuildDir --target uvim-config --parallel $Jobs"
    cmake --build $BuildDir --target uvim-config --parallel $Jobs
} else {
    Write-Host "cmake --build $BuildDir --target uvim-config"
    cmake --build $BuildDir --target uvim-config
}
Stop-IfNativeCommandFailed "cmake build"

$UvimConfig = Resolve-UvimConfig $BuildDir
if ([string]::IsNullOrEmpty($UvimConfig)) {
    Write-Error "bootstrap.ps1: cannot find uvim-config in $BuildDir after build"
    exit 1
}

if (Confirm-YesNo "Do you want to run uvim-config? (y/n)") {
    Write-Host $UvimConfig
    & $UvimConfig
    Stop-IfNativeCommandFailed "uvim-config"

    if (Confirm-YesNo "Do you want to build uVim? (y/n)") {
        $BuildScript = Join-Path "." "build.ps1"
        Write-Host "$BuildScript --build-dir $BuildDir"
        & $BuildScript --build-dir $BuildDir
        Stop-IfNativeCommandFailed "build.ps1"
    }
}
