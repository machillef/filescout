# Screenshots of the shipped look: single pane and dual pane, maximised, one row selected.
param([string]$Exe, [string]$Folder = 'C:\Windows', [string]$Out = 'C:\fsdev\gui-out\looks')
Import-Module C:\fsdev\tools\Win1Ui.psm1 -DisableNameChecking
New-Item -ItemType Directory -Force $Out | Out-Null
Get-Process FileScoutNative -ErrorAction SilentlyContinue | Stop-Process -Force
$p = Start-Process $Exe -ArgumentList "`"$Folder`"" -PassThru
Start-Sleep 6
$w = Wait-Window { param($x) $x.Current.ProcessId -eq $p.Id } 10000
Save-Screenshot (Join-Path $Out 'final-window.png')
try { $w.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).SetWindowVisualState('Maximized') } catch { }
Start-Sleep 3
Invoke-Click 700 330
Start-Sleep 2
Save-Screenshot (Join-Path $Out 'final-single.png')
$dual = Find-Element $w -name 'Toggle dual pane' -timeoutMs 5000
if ($dual) { Click-Element $dual; Start-Sleep 4 }
Save-Screenshot (Join-Path $Out 'final-dual.png')
if (-not $p.HasExited) { $p.Kill() }
"done"
