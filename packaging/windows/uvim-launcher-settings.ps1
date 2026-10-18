# "uVim launcher settings": a small window to choose how the uVim shortcut
# opens uVim. Saves HKCU\Software\uVim\Launcher, read by uvim-launch.ps1.
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$Key = "HKCU:\Software\uVim"
$current = (Get-ItemProperty -Path $Key -Name Launcher -ErrorAction SilentlyContinue).Launcher
if (-not $current) { $current = "terminal" }
$hasTerminal = $null -ne (Get-Command wt.exe -ErrorAction SilentlyContinue)

$form = New-Object System.Windows.Forms.Form
$form.Text = "uVim launcher settings"
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$form.StartPosition = "CenterScreen"
$form.ClientSize = New-Object System.Drawing.Size(440, 220)
$form.Font = New-Object System.Drawing.Font("Segoe UI", 9)
$icon = Join-Path $PSScriptRoot "uvim.ico"
if (Test-Path $icon) { $form.Icon = New-Object System.Drawing.Icon($icon) }

$label = New-Object System.Windows.Forms.Label
$label.Text = "How should the uVim shortcut open uVim?"
$label.Location = New-Object System.Drawing.Point(16, 16)
$label.AutoSize = $true
$form.Controls.Add($label)

$terminal = New-Object System.Windows.Forms.RadioButton
$terminal.Text = "Windows Terminal window with the uVim profile (recommended)"
$terminal.Location = New-Object System.Drawing.Point(24, 48)
$terminal.Size = New-Object System.Drawing.Size(400, 24)
$terminal.Enabled = $hasTerminal
$form.Controls.Add($terminal)

$powershell = New-Object System.Windows.Forms.RadioButton
$powershell.Text = "Separate Windows PowerShell window"
$powershell.Location = New-Object System.Drawing.Point(24, 76)
$powershell.Size = New-Object System.Drawing.Size(400, 24)
$form.Controls.Add($powershell)

$note = New-Object System.Windows.Forms.Label
$note.Location = New-Object System.Drawing.Point(16, 112)
$note.Size = New-Object System.Drawing.Size(408, 48)
if ($hasTerminal) {
    $note.Text = "Windows Terminal renders uVim best (UTF-8, true colour). " +
                 "Both options start uVim in PowerShell with your profile."
}
else {
    $note.Text = "Windows Terminal is not installed, so uVim opens in a " +
                 "PowerShell window. Install it from the Microsoft Store to " +
                 "use the first option."
}
$form.Controls.Add($note)

if ($current -eq "terminal" -and $hasTerminal) { $terminal.Checked = $true }
else { $powershell.Checked = $true }

$save = New-Object System.Windows.Forms.Button
$save.Text = "Save"
$save.Location = New-Object System.Drawing.Point(252, 176)
$save.Size = New-Object System.Drawing.Size(80, 28)
$save.DialogResult = [System.Windows.Forms.DialogResult]::OK
$form.AcceptButton = $save
$form.Controls.Add($save)

$cancel = New-Object System.Windows.Forms.Button
$cancel.Text = "Cancel"
$cancel.Location = New-Object System.Drawing.Point(344, 176)
$cancel.Size = New-Object System.Drawing.Size(80, 28)
$cancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
$form.CancelButton = $cancel
$form.Controls.Add($cancel)

if ($form.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
    $value = if ($terminal.Checked) { "terminal" } else { "powershell" }
    # Not New-Item -Force: on the registry it recreates an existing key.
    if (!(Test-Path $Key)) { New-Item -Path $Key | Out-Null }
    Set-ItemProperty -Path $Key -Name Launcher -Value $value
}
