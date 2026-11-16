# GUI smoke test of the file operations that moved to the shell STA (S-03): paste, rename,
# new folder and delete, driven with real keyboard input in session 1. Prints the folder state.
param([string]$Exe, [string]$Dir = 'C:\fsdev\gui-out\smoke', [string]$Out = 'C:\fsdev\gui-out')
$ErrorActionPreference = 'Stop'
Import-Module C:\fsdev\tools\Win1Ui.psm1 -DisableNameChecking
Get-Process FileScoutNative -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item -Recurse -Force $Dir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Dir | Out-Null
Set-Content (Join-Path $Dir 'alpha.txt') 'smoke'
function State { (Get-ChildItem $Dir -Force | Sort-Object Name | ForEach-Object { $_.Name }) -join ', ' }

$p = Start-Process $Exe -ArgumentList "`"$Dir`"" -PassThru
Start-Sleep 6
$w = Wait-Window { param($x) $x.Current.ProcessId -eq $p.Id } 10000
if (-not $w) { throw 'window not found' }
$w.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).SetWindowVisualState('Maximized')
Start-Sleep 2
$row = Find-Element $w -name 'alpha.txt' -timeoutMs 5000
if (-not $row) { Invoke-Click 400 175 } else { Click-Element $row }
Start-Sleep 1
"start: $(State)"

Send-Keys 0x11, 0x43; Start-Sleep 1; Send-Keys 0x11, 0x56; Start-Sleep 4      # Ctrl+C, Ctrl+V
"after paste: $(State)"

function Select-Row([string]$name) {
    # Details view shows names without the extension, list view with it. A closing dialog can
    # still hold an off-screen element with the same text, so take the first one on screen.
    $names = @($name, [IO.Path]::GetFileNameWithoutExtension($name))
    $deadline = (Get-Date).AddSeconds(6)
    while ((Get-Date) -lt $deadline) {
        $hit = $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) |
            Where-Object { $c = $_.Current; $names -contains $c.Name -and -not $c.IsOffscreen -and
                -not [double]::IsInfinity($c.BoundingRectangle.X) -and -not [double]::IsNaN($c.BoundingRectangle.X) } |
            Select-Object -First 1
        if ($hit) { Click-Element $hit; Start-Sleep 1; return }
        Start-Sleep -Milliseconds 300
    }
    Save-Screenshot (Join-Path $Out 'smoke-missing-row.png')
    throw "row $name not found"
}

Select-Row 'alpha.txt'
Send-Keys 0x71; Start-Sleep 2                                                     # F2
Save-Screenshot (Join-Path $Out 'smoke-rename-dialog.png')
Send-Text '^a'; Send-Text 'renamed.txt'; Start-Sleep 1; Send-Text '{ENTER}'; Start-Sleep 4
"after rename: $(State)"

Select-Row 'renamed.txt'
Send-Keys 0x11, 0x10, 0x4E; Start-Sleep 2                                         # Ctrl+Shift+N
Save-Screenshot (Join-Path $Out 'smoke-newfolder-dialog.png')
Send-Text '^a'; Send-Text 'NewDir'; Start-Sleep 1; Send-Text '{ENTER}'; Start-Sleep 4
"after new folder: $(State)"
Save-Screenshot (Join-Path $Out 'smoke-after-create.png')

Select-Row 'renamed.txt'
Send-Keys 0x2E; Start-Sleep 2                                                     # Del
Save-Screenshot (Join-Path $Out 'smoke-delete-dialog.png')
$btn = $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) |
    Where-Object { $_.Current.Name -eq 'Delete' -and $_.Current.ControlType.ProgrammaticName -eq 'ControlType.Button' } |
    Select-Object -First 1
if (-not $btn) { throw 'Delete button not found' }
Click-Element $btn; Start-Sleep 5                                                 # confirm (Cancel is the default)
"after delete: $(State)"
"responsive: $($p.Responding) exited: $($p.HasExited)"
Save-Screenshot (Join-Path $Out 'smoke-final.png')
if (-not $p.HasExited) { $p.Kill() }
