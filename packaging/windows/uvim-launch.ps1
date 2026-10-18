# Starts uVim in its own window, the way the user chose in the installer or
# in "uVim launcher settings" (HKCU\Software\uVim, value Launcher):
#   terminal    a new Windows Terminal window with the "uVim" profile
#   powershell  a separate Windows PowerShell console window
# Either way uVim runs inside PowerShell (with the user's PowerShell profile
# loaded), so the window stays at a PowerShell prompt after quitting uVim.
# Files passed to this script are opened in uVim.
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Files
)

$ErrorActionPreference = "Stop"

$Uvim = Join-Path $PSScriptRoot "uvim.exe"
$Mode = (Get-ItemProperty -Path "HKCU:\Software\uVim" -Name Launcher `
        -ErrorAction SilentlyContinue).Launcher
if (-not $Mode) { $Mode = "terminal" }

# The command run inside PowerShell. Passed base64 encoded so paths with
# spaces, quotes or ';' (a Windows Terminal separator) survive unchanged.
$quote = { param($s) "'" + ($s -replace "'", "''") + "'" }
$command = "[Console]::OutputEncoding = [Text.Encoding]::UTF8`n" +
           "`$Host.UI.RawUI.WindowTitle = 'uVim'`n" +
           "& " + (& $quote $Uvim)
foreach ($file in $Files) {
    # Windows PowerShell 5.1: no ?? operator.
    $resolved = Resolve-Path -LiteralPath $file -ErrorAction SilentlyContinue
    $path = if ($resolved) { $resolved.Path } else { $file }
    $command += " " + (& $quote $path)
}
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
$shell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
$shellArgs = @("-NoLogo", "-NoExit", "-EncodedCommand", $encoded)

$terminal = Get-Command wt.exe -ErrorAction SilentlyContinue
if ($Mode -eq "terminal" -and $terminal) {
    # -p uVim gives the window the uVim profile (name, icon, font); the
    # command line given here runs instead of the profile's own.
    Start-Process -FilePath $terminal.Source `
        -ArgumentList (@("-w", "new", "-p", "uVim", $shell) + $shellArgs)
}
else {
    Start-Process -FilePath $shell -ArgumentList $shellArgs `
        -WorkingDirectory $env:USERPROFILE
}
